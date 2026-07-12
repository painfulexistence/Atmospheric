#include "terrain_streamer.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "console_subsystem.hpp"
#include "frustum.hpp"
#include "game_object.hpp"
#include "height_field_collider_component.hpp"
#include "job_system.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "mesh_builder.hpp"
#include "mesh_component.hpp"
#include "mesh_instancer.hpp"
#include "rigidbody_component.hpp"
#include "terrain_tile_cache.hpp"

#include "FastNoiseLite.h"

#include <algorithm>
#include <cmath>

// ── per-LOD geometry ─────────────────────────────────────────────────────────

int TerrainStreamer::HeightRes(int lod) const {
    return std::max(8, _props.tileHeightRes >> lod);
}

int TerrainStreamer::MeshRes(int lod) const {
    return std::max(2, _props.tileMeshRes >> lod);
}

float TerrainStreamer::SkirtDepth(int lod) const {
    if (_props.skirtDepth > 0.0f) return _props.skirtDepth;
    // Deep enough to cover the height error between this LOD and its
    // neighbors: a few texels of slope plus a slice of the height range.
    return 4.0f * _props.tileSize / static_cast<float>(HeightRes(lod)) + 0.01f * _props.heightScale;
}

float TerrainStreamer::GutterWorldSize(int lod) const {
    // Heightmaps carry a 1-texel gutter on every side (grid is (n+3) texels
    // for n segments), so the texture spans slightly more world than the tile.
    // Tile materials report this as worldSize so shader-side normal derivation
    // stays exact.
    const int n = HeightRes(lod);
    return _props.tileSize * static_cast<float>(n + 3) / static_cast<float>(n);
}

glm::ivec2 TerrainStreamer::WorldToTile(const glm::vec3& worldPos) const {
    const float half = 0.5f * _props.worldSize;
    int tx = static_cast<int>(std::floor((worldPos.x + half) / _props.tileSize));
    int tz = static_cast<int>(std::floor((worldPos.z + half) / _props.tileSize));
    return { std::clamp(tx, 0, _tilesPerSide - 1), std::clamp(tz, 0, _tilesPerSide - 1) };
}

glm::vec2 TerrainStreamer::TileOrigin(glm::ivec2 coord) const {
    const float half = 0.5f * _props.worldSize;
    return { coord.x * _props.tileSize - half, coord.y * _props.tileSize - half };
}

int TerrainStreamer::RingThreshold(int lod) const {
    return _props.lod0RadiusTiles << lod;
}

int TerrainStreamer::DesiredLod(glm::ivec2 coord, glm::ivec2 camTile) const {
    const int cheb = std::max(std::abs(coord.x - camTile.x), std::abs(coord.y - camTile.y));
    for (int lod = 0; lod < _props.lodCount - 1; ++lod) {
        if (cheb <= RingThreshold(lod)) return lod;
    }
    return _props.lodCount - 1;
}

// ── init ─────────────────────────────────────────────────────────────────────

void TerrainStreamer::Init(Application* app, const StreamingTerrainProps& props, GameObject* root) {
    _app = app;
    _props = props;
    _root = root;

    _props.lodCount = std::max(1, _props.lodCount);
    _tilesPerSide = std::max(1, static_cast<int>(std::round(_props.worldSize / _props.tileSize)));
    _props.worldSize = _tilesPerSide * _props.tileSize;
    _maxInFlight = static_cast<int>(std::max(4u, JobSystem::Get()->GetThreadCount() * 2));
    _pool.resize(_props.lodCount);
    _stats.tilesPerSide = _tilesPerSide;

    if (!_props.heightFn) {
        auto noise = std::make_shared<FastNoiseLite>();
        noise->SetSeed(_props.noise.seed);
        noise->SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
        noise->SetFrequency(_props.noise.frequency);
        noise->SetFractalType(FastNoiseLite::FractalType_FBm);
        noise->SetFractalOctaves(_props.noise.octaves);
        noise->SetFractalLacunarity(_props.noise.lacunarity);
        noise->SetFractalGain(_props.noise.gain);
        _heightFn = [noise](float wx, float wz) { return noise->GetNoise(wx, wz) * 0.5f + 0.5f; };
    } else {
        _heightFn = _props.heightFn;
    }

    // Disk tile cache: hash every input that shapes the generated heights so
    // stale bakes can never be replayed after a parameter change.
    if (!_props.cacheDir.empty()) {
        _cache = std::make_shared<TerrainTileCache>(_props.cacheDir);
        uint32_t h = 0;
        h = TerrainTileCache::HashCombine(h, &_props.noise.seed, sizeof(_props.noise.seed));
        h = TerrainTileCache::HashCombine(h, &_props.noise.frequency, sizeof(_props.noise.frequency));
        h = TerrainTileCache::HashCombine(h, &_props.noise.octaves, sizeof(_props.noise.octaves));
        h = TerrainTileCache::HashCombine(h, &_props.noise.lacunarity, sizeof(_props.noise.lacunarity));
        h = TerrainTileCache::HashCombine(h, &_props.noise.gain, sizeof(_props.noise.gain));
        h = TerrainTileCache::HashCombine(h, &_props.tileSize, sizeof(_props.tileSize));
        h = TerrainTileCache::HashCombine(h, &_props.tileHeightRes, sizeof(_props.tileHeightRes));
        h = TerrainTileCache::HashCombine(h, &_props.cacheVersion, sizeof(_props.cacheVersion));
        const bool customFn = static_cast<bool>(_props.heightFn);
        h = TerrainTileCache::HashCombine(h, &customFn, sizeof(customFn));
        _cacheHash = h;
    }

    // Resolve shared layer textures once; tile materials reference the same
    // handles. Layer tiling is specified as repeats per tile and rescaled to
    // the gutter texture span per LOD so detail phase is continuous across
    // tile borders.
    auto& am = AssetManager::Get();
    const int layerCount = std::min(static_cast<int>(_props.layers.size()), TerrainMaterial::MAX_LAYERS);
    for (int i = 0; i < layerCount; ++i) {
        ResolvedLayer layer;
        // Pre-created handles (procedural generators) win over disk paths.
        if (_props.layers[i].albedo.IsValid())
            layer.albedo = _props.layers[i].albedo;
        else if (!_props.layers[i].albedoPath.empty())
            layer.albedo = am.CreateTexture(_props.layers[i].albedoPath);
        if (_props.layers[i].normal.IsValid())
            layer.normal = _props.layers[i].normal;
        else if (!_props.layers[i].normalPath.empty())
            layer.normal = am.CreateTexture(_props.layers[i].normalPath);
        layer.tilesPerTile = _props.layers[i].tiling;
        _layers.push_back(layer);
    }

    // Shared grass material: per-blade variation lives in vertex attributes,
    // so every grass cell can share one material (one batch state).
    if (_props.grassDensity > 0.0f) {
        _grassMaterial = am.CreateGrassMaterial();
        _grassMaterial->rootColor = _props.grassRootColor;
        _grassMaterial->tipColor = _props.grassTipColor;
        _grassMaterial->windDir = _props.grassWindDir;
        _grassMaterial->windStrength = _props.grassWindStrength;
        _grassMaterial->windSpeed = _props.grassWindSpeed;
        _grassMaterial->fadeStart = _props.grassRadius * 0.65f;
        _grassMaterial->fadeEnd = _props.grassRadius;
        _grassMaterial->fogColor = _props.fogColor;
        _grassMaterial->fogDensity = _props.fogDensity;
    }

    // Prewarm: the whole world at the coarsest LOD, generated in parallel and
    // integrated synchronously, so frame one already shows the full horizon.
    const int coarsest = _props.lodCount - 1;
    for (int tz = 0; tz < _tilesPerSide; ++tz) {
        for (int tx = 0; tx < _tilesPerSide; ++tx) {
            RequestTile({ tx, tz }, coarsest);
        }
    }
    JobSystem::Get()->Wait();
    IntegrateFinishedJobs(-1, { _tilesPerSide / 2, _tilesPerSide / 2 });
    ConsoleSubsystem::Get()->Info(
        "TerrainStreamer: prewarmed " + std::to_string(_tilesPerSide * _tilesPerSide) + " tiles ("
        + std::to_string(static_cast<int>(_props.worldSize)) + "m x "
        + std::to_string(static_cast<int>(_props.worldSize)) + "m) at LOD " + std::to_string(coarsest)
    );
}

// ── per-frame update ─────────────────────────────────────────────────────────

void TerrainStreamer::Update(const glm::vec3& cameraPos, const glm::mat4& viewProj) {
    if (!_app) return;
    const glm::ivec2 camTile = WorldToTile(cameraPos);

    IntegrateFinishedJobs(_props.uploadsPerFrame, camTile);

    // Scan for tiles that are missing or off their desired LOD and kick jobs,
    // closest first, until the in-flight cap is reached. Missing tiles beat
    // LOD changes; downgrades carry a 1-ring hysteresis to avoid thrashing at
    // ring borders.
    if (static_cast<int>(_inFlight.size()) < _maxInFlight) {
        struct Candidate {
            glm::ivec2 coord;
            int lod;
            int priority;// lower = sooner
        };
        std::vector<Candidate> candidates;
        for (int tz = 0; tz < _tilesPerSide; ++tz) {
            for (int tx = 0; tx < _tilesPerSide; ++tx) {
                const glm::ivec2 coord{ tx, tz };
                if (_inFlight.find(coord) != _inFlight.end()) continue;
                const int desired = DesiredLod(coord, camTile);
                const int cheb = std::max(std::abs(tx - camTile.x), std::abs(tz - camTile.y));
                auto it = _tiles.find(coord);
                if (it == _tiles.end()) {
                    candidates.push_back({ coord, desired, cheb });
                } else if (it->second->lod > desired) {
                    candidates.push_back({ coord, desired, 1000 + cheb });// upgrade
                } else if (it->second->lod < desired && cheb > RingThreshold(it->second->lod) + 1) {
                    candidates.push_back({ coord, desired, 2000 + cheb });// downgrade (frees detail memory)
                }
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            return a.priority < b.priority;
        });
        for (const auto& c : candidates) {
            if (static_cast<int>(_inFlight.size()) >= _maxInFlight) break;
            RequestTile(c.coord, c.lod);
        }
        _stats.initialLoadDone = candidates.empty() && _inFlight.empty();
    }

    CullTiles(viewProj);
    UpdateColliders(camTile);
    UpdateEntities(camTile);
    UpdateGrass(cameraPos);

    _stats.loadedTiles = static_cast<int>(_tiles.size());
    _stats.pendingJobs = static_cast<int>(_inFlight.size());
    _stats.activeEntities = _activeEntities;
    _stats.grassCells = static_cast<int>(_grassCells.size());
    _stats.grassBlades = _grassBladesLive;
    _stats.cacheHits = _cacheHits.load(std::memory_order_relaxed);
    _stats.cacheMisses = _cacheMisses.load(std::memory_order_relaxed);
}

float TerrainStreamer::GetHeight(float wx, float wz) const {
    return _heightFn ? _heightFn(wx, wz) * _props.heightScale : 0.0f;
}

void TerrainStreamer::SetLodTintDebug(bool enabled) {
    _lodTintDebug = enabled;
    for (auto& slot : _allSlots) {
        if (!slot->material) continue;
        slot->material->paletteIndex = enabled ? slot->lod % 6 : _props.paletteIndex;
        // Detail layers override the palette in terrain.frag, so tint mode
        // suspends them (and restores on the way out) or L would be a no-op
        // on textured terrain.
        slot->material->layerCount = enabled ? 0 : static_cast<int>(_layers.size());
    }
}

void TerrainStreamer::SetPalette(int index) {
    // Six fallback height-palettes (see terrain.frag); wrap so the caller can
    // cycle freely. Stored on _props so tiles streamed in later inherit it.
    _props.paletteIndex = ((index % 6) + 6) % 6;
    // LOD-tint mode owns paletteIndex while active — don't stomp its colours;
    // the new palette takes effect when the user turns tint back off.
    if (_lodTintDebug) return;
    for (auto& slot : _allSlots) {
        if (slot->material) slot->material->paletteIndex = _props.paletteIndex;
    }
}

// ── streaming internals ──────────────────────────────────────────────────────

void TerrainStreamer::RequestTile(glm::ivec2 coord, int lod) {
    auto job = std::make_shared<GenJob>();
    job->coord = coord;
    job->lod = lod;
    _inFlight[coord] = job;

    const int n = HeightRes(lod);
    const int w = n + 3;
    const float step = _props.tileSize / static_cast<float>(n);
    const glm::vec2 origin = TileOrigin(coord);
    const glm::vec2 gutterMin = origin - glm::vec2(1.5f * step);
    const glm::vec2 gutterMax = origin + glm::vec2(_props.tileSize + 1.5f * step);
    // Copies so the worker never touches `this`.
    auto heightFn = _heightFn;
    auto splatFn = _props.splatFn;
    const int splatRes = _props.splatRes;
    const bool wantSplat = static_cast<bool>(splatFn) && !_layers.empty();

    auto cache = _cache;
    const uint32_t cacheHash = _cacheHash;
    auto* hits = &_cacheHits;
    auto* misses = &_cacheMisses;

    JobSystem::Get()->Execute([job,
                               heightFn,
                               splatFn,
                               wantSplat,
                               splatRes,
                               n,
                               w,
                               step,
                               origin,
                               gutterMin,
                               gutterMax,
                               cache,
                               cacheHash,
                               hits,
                               misses](int /*threadIndex*/) {
        // Cache first (pure IO, Ghost-of-Tsushima path); synthesize on miss
        // and bake the result so the next boot never generates this tile.
        const bool cached = cache && cache->Load(job->coord.x, job->coord.y, job->lod, cacheHash, w, job->heights);
        if (!cached) {
            job->heights.resize(static_cast<size_t>(w) * w);
            for (int j = 0; j < w; ++j) {
                const float wz = origin.y + (j - 1) * step;
                for (int i = 0; i < w; ++i) {
                    const float wx = origin.x + (i - 1) * step;
                    job->heights[static_cast<size_t>(j) * w + i] = std::clamp(heightFn(wx, wz), 0.0f, 1.0f);
                }
            }
            if (cache) cache->Store(job->coord.x, job->coord.y, job->lod, cacheHash, w, job->heights);
        }
        if (cache) (cached ? hits : misses)->fetch_add(1, std::memory_order_relaxed);
        if (wantSplat) job->splat = splatFn(gutterMin, gutterMax, splatRes, heightFn);
        job->done.store(true, std::memory_order_release);
    });
}

void TerrainStreamer::IntegrateFinishedJobs(int budget, glm::ivec2 /*camTile*/) {
    std::vector<glm::ivec2> integrated;
    for (auto& [coord, job] : _inFlight) {
        if (budget >= 0 && static_cast<int>(integrated.size()) >= budget) break;
        if (!job->done.load(std::memory_order_acquire)) continue;

        auto it = _tiles.find(coord);
        TileSlot* previous = (it != _tiles.end()) ? it->second : nullptr;
        if (previous && previous->lod == job->lod) {
            // Same-LOD refresh (rare): update the existing slot in place.
            ApplyJobToSlot(*job, previous);
        } else {
            TileSlot* slot = AcquireSlot(job->lod);
            ApplyJobToSlot(*job, slot);
            _tiles[coord] = slot;
            if (previous) ReleaseSlot(previous);// only after the replacement is live — no holes
        }
        integrated.push_back(coord);
    }
    for (const auto& coord : integrated)
        _inFlight.erase(coord);
}

void TerrainStreamer::ApplyJobToSlot(GenJob& job, TileSlot* slot) {
    auto& am = AssetManager::Get();
    const int w = HeightRes(job.lod) + 3;
    const glm::vec2 origin = TileOrigin(job.coord);
    const glm::vec2 center = origin + glm::vec2(0.5f * _props.tileSize);

    slot->coord = job.coord;
    slot->go->SetPosition(glm::vec3(center.x, 0.0f, center.y));
    slot->go->SetActive(true);
    am.UpdateHeightmapTexture(slot->heightTex, job.heights, w, w);
    if (!job.splat.empty()) {
        slot->material->splatMap = am.CreateOrUpdateTextureRGBA8(
            "terrainstream_splat_L" + std::to_string(slot->lod) + "_" + std::to_string(slot->slotIndex),
            job.splat.data(),
            _props.splatRes,
            _props.splatRes
        );
    }
    // LOD0 tiles keep their CPU grid as the collider/height-query source.
    slot->cpuGrid = (job.lod == 0) ? std::move(job.heights) : std::vector<float>{};
}

TerrainStreamer::TileSlot* TerrainStreamer::AcquireSlot(int lod) {
    if (!_pool[lod].empty()) {
        TileSlot* slot = _pool[lod].back();
        _pool[lod].pop_back();
        return slot;
    }

    auto& am = AssetManager::Get();
    auto owned = std::make_unique<TileSlot>();
    TileSlot* slot = owned.get();
    slot->lod = lod;
    slot->slotIndex = static_cast<int>(_allSlots.size());
    const std::string name = "terrainstream_L" + std::to_string(lod) + "_" + std::to_string(slot->slotIndex);

    GameObject* go = _app->CreateGameObject(glm::vec3(0.0f));
    go->SetName(name);
    go->parent = _root;
    slot->go = go;

    Mesh* mesh = MeshBuilder::CreateTerrainTile(
        _props.tileSize, MeshRes(lod), HeightRes(lod), SkirtDepth(lod), _props.heightScale
    );
    slot->mesh = am.CreateMesh(name, mesh);

    TerrainMaterial* mat = am.CreateTerrainMaterial();
    mat->heightScale = _props.heightScale;
    mat->tessellationFactor = _props.tessellationFactor;
    mat->worldSize = GutterWorldSize(lod);
    mat->paletteIndex = _lodTintDebug ? lod % 6 : _props.paletteIndex;
    mat->fogDensity = _props.fogDensity;
    mat->fogColor = _props.fogColor;
    mat->renderState.cull = CullMode::None;// skirts must render from both sides
    mat->layerCount = _lodTintDebug ? 0 : static_cast<int>(_layers.size());
    for (size_t i = 0; i < _layers.size(); ++i) {
        mat->layers[i].albedoMap = _layers[i].albedo;
        mat->layers[i].normalMap = _layers[i].normal;
        mat->layers[i].tiling = _layers[i].tilesPerTile * GutterWorldSize(lod) / _props.tileSize;
    }

    const int w = HeightRes(lod) + 3;
    std::vector<float> zeros(static_cast<size_t>(w) * w, 0.0f);
    slot->heightTex = am.CreateHeightmapTexture(name, zeros, w, w);
    mat->heightMap = slot->heightTex;
    slot->material = mat;
    mesh->SetMaterial(am.GetMaterialHandle(mat));
    go->AddComponent<MeshComponent>(slot->mesh);

    _stats.gpuHeightmapBytes += static_cast<size_t>(w) * w * 2;
    _allSlots.push_back(std::move(owned));
    return slot;
}

void TerrainStreamer::ReleaseSlot(TileSlot* slot) {
    slot->go->SetActive(false);
    slot->cpuGrid.clear();
    slot->cpuGrid.shrink_to_fit();
    _pool[slot->lod].push_back(slot);
}

void TerrainStreamer::CullTiles(const glm::mat4& viewProj) {
    Frustum frustum(viewProj);
    const float half = 0.5f * _props.tileSize;
    int visible = 0;
    for (auto& [coord, slot] : _tiles) {
        const glm::vec2 origin = TileOrigin(coord);
        const glm::vec3 center(origin.x + half, 0.5f * _props.heightScale, origin.y + half);
        const float radius = glm::length(glm::vec3(half, 0.5f * _props.heightScale + SkirtDepth(slot->lod), half));
        const bool inView = frustum.IntersectsSphere(center, radius);
        slot->go->SetActive(inView);
        if (inView) ++visible;
    }
    _stats.visibleTiles = visible;
}

// ── collider ring ────────────────────────────────────────────────────────────

void TerrainStreamer::UpdateColliders(glm::ivec2 camTile) {
    const int r = _props.colliderRadiusTiles;
    if (r < 0) return;

    auto wanted = [&](glm::ivec2 c) {
        return std::max(std::abs(c.x - camTile.x), std::abs(c.y - camTile.y)) <= r && c.x >= 0 && c.y >= 0
               && c.x < _tilesPerSide && c.y < _tilesPerSide;
    };

    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            const glm::ivec2 coord{ camTile.x + dx, camTile.y + dz };
            if (!wanted(coord)) continue;
            bool assigned = false;
            for (const auto& slot : _colliders) {
                if (slot.coord == coord) {
                    assigned = true;
                    break;
                }
            }
            if (assigned) continue;

            // The collider needs the tile's CPU grid, which only LOD0 tiles keep.
            auto it = _tiles.find(coord);
            if (it == _tiles.end() || it->second->lod != 0 || it->second->cpuGrid.empty()) continue;

            // Reuse a slot whose tile left the ring, else grow the pool.
            ColliderSlot* free = nullptr;
            for (auto& slot : _colliders) {
                if (!wanted(slot.coord)) {
                    free = &slot;
                    break;
                }
            }
            if (!free) {
                _colliders.emplace_back();
                free = &_colliders.back();
            }
            AssignCollider(*free, it->second);
        }
    }
}

// ── entity streaming ─────────────────────────────────────────────────────────

void TerrainStreamer::UpdateEntities(glm::ivec2 camTile) {
    if (!_props.placeEntitiesFn || _props.entityRadiusTiles < 0) return;
    if (!_props.spawnEntityFn && _props.entityMeshes.empty()) return;// no way to realize placements
    const int r = _props.entityRadiusTiles;

    // Release tiles that left the ring (+1 hysteresis) back into the pools.
    std::vector<glm::ivec2> departed;
    for (auto& [coord, spawned] : _entityTiles) {
        if (std::max(std::abs(coord.x - camTile.x), std::abs(coord.y - camTile.y)) > r + 1) departed.push_back(coord);
    }
    for (const auto& coord : departed) {
        for (auto& entity : _entityTiles[coord]) {
            entity.go->SetActive(false);
            _entityPool[entity.type].push_back(entity.go);
            _activeEntities -= entity.count;
        }
        _entityTiles.erase(coord);
    }

    // Populate tiles inside the ring, nearest first, budgeted per frame.
    struct Missing {
        glm::ivec2 coord;
        int cheb;
    };
    std::vector<Missing> missing;
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            const glm::ivec2 coord{ camTile.x + dx, camTile.y + dz };
            if (coord.x < 0 || coord.y < 0 || coord.x >= _tilesPerSide || coord.y >= _tilesPerSide) continue;
            if (_entityTiles.find(coord) != _entityTiles.end()) continue;
            missing.push_back({ coord, std::max(std::abs(dx), std::abs(dz)) });
        }
    }
    std::sort(missing.begin(), missing.end(), [](const Missing& a, const Missing& b) { return a.cheb < b.cheb; });

    int populated = 0;
    for (const auto& m : missing) {
        if (populated >= std::max(1, _props.entityTilesPerFrame)) break;
        ++populated;

        const glm::vec2 origin = TileOrigin(m.coord);
        TerrainTileContext ctx;
        ctx.coord = m.coord;
        ctx.worldMin = origin;
        ctx.worldMax = origin + glm::vec2(_props.tileSize);
        ctx.heightScale = _props.heightScale;
        ctx.seed = _props.noise.seed;
        ctx.height01 = &_heightFn;

        // Types with a prototype in entityMeshes accumulate into one instance
        // cloud per (tile, type); everything else spawns a GameObject each via
        // spawnEntityFn (the pre-instancing path, for entities that need their
        // own components).
        auto isInstanced = [this](int type) {
            return type >= 0 && type < static_cast<int>(_props.entityMeshes.size())
                   && _props.entityMeshes[type].IsValid();
        };

        std::vector<SpawnedEntity> spawned;
        std::unordered_map<int, std::vector<glm::mat4>> clouds;
        for (const auto& placement : _props.placeEntitiesFn(ctx)) {
            if (isInstanced(placement.type)) {
                // World-space TRS; the cloud GameObject sits at the origin so
                // these pass through MeshInstancer's goTransform * local as-is.
                clouds[placement.type].push_back(
                    glm::translate(glm::mat4(1.0f), placement.position)
                    * glm::rotate(glm::mat4(1.0f), placement.yaw, glm::vec3(0.0f, 1.0f, 0.0f))
                    * glm::scale(glm::mat4(1.0f), glm::vec3(placement.scale))
                );
                continue;
            }
            if (!_props.spawnEntityFn) continue;
            GameObject* go = nullptr;
            auto& pool = _entityPool[placement.type];
            if (!pool.empty()) {
                go = pool.back();
                pool.pop_back();
            } else {
                go = _props.spawnEntityFn(_app, placement);
                if (!go) continue;
                go->parent = _root;
            }
            go->SetPosition(placement.position);
            go->SetRotation(glm::vec3(0.0f, placement.yaw, 0.0f));
            go->SetScale(glm::vec3(placement.scale));
            go->SetActive(true);
            spawned.push_back({ placement.type, go, 1 });
            ++_activeEntities;
        }

        for (auto& [type, transforms] : clouds) {
            const int count = static_cast<int>(transforms.size());
            GameObject* go = nullptr;
            auto& pool = _entityPool[type];
            if (!pool.empty()) {
                go = pool.back();
                pool.pop_back();
            } else {
                go = _app->CreateGameObject(glm::vec3(0.0f));
                go->SetName(fmt::format("entity_cloud_{}", type));
                go->parent = _root;
                go->AddComponent<MeshInstancer>(MeshInstancerProps{ .prototype = _props.entityMeshes[type] });
            }
            if (auto* instancer = go->GetComponent<MeshInstancer>()) {
                instancer->SetTransforms(std::move(transforms));
            }
            go->SetActive(true);
            spawned.push_back({ type, go, count });
            _activeEntities += count;
        }
        // Record even when empty so barren tiles aren't re-scattered per frame.
        _entityTiles[m.coord] = std::move(spawned);
    }
}

void TerrainStreamer::AssignCollider(ColliderSlot& slot, TileSlot* tile) {
    const int n = HeightRes(0);
    const int w = n + 3;
    // Interior (n+1)^2 samples span exactly tileSize (the gutter is skipped).
    std::vector<float> interior(static_cast<size_t>(n + 1) * (n + 1));
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            interior[static_cast<size_t>(j) * (n + 1) + i] = tile->cpuGrid[static_cast<size_t>(j + 1) * w + (i + 1)];

    const glm::vec2 origin = TileOrigin(tile->coord);
    const glm::vec3 center(origin.x + 0.5f * _props.tileSize, 0.0f, origin.y + 0.5f * _props.tileSize);

    if (!slot.go) {
        slot.heightGrid = std::make_shared<StreamedHeightGrid>();
        slot.heightGrid->Assign(std::move(interior), n + 1, n + 1);
        slot.go = _app->CreateGameObject(center);
        slot.go->SetName("terrainstream_collider_" + std::to_string(&slot - _colliders.data()));
        slot.go->parent = _root;
        // btHeightfieldTerrainShape spans (res-1) cells at worldSize/res
        // scaling; stretch worldSize so the collider covers the tile exactly.
        const int res = _props.colliderResolution;
        slot.collider = static_cast<HeightFieldColliderComponent*>(slot.go->AddComponent<HeightFieldColliderComponent>(
            slot.heightGrid,
            HeightFieldColliderProps{
                .worldSize = _props.tileSize * static_cast<float>(res) / static_cast<float>(res - 1),
                .heightScale = _props.heightScale,
                .minHeight = 0.0f,
                .maxHeight = _props.heightScale,
                .resolution = res,
            }
        ));
        slot.rigidbody = slot.go->GetComponent<RigidbodyComponent>();
    } else {
        slot.heightGrid->Assign(std::move(interior), n + 1, n + 1);
        slot.collider->SyncFromHeightField();
        slot.go->SetPosition(center);
        // minHeight=0 / maxHeight=heightScale keeps Bullet's vertical centring
        // equal to the mesh's, so no extra Y offset is needed here.
        if (slot.rigidbody) slot.rigidbody->SetWorldTransform(center, glm::vec3(0.0f));
    }
    slot.coord = tile->coord;
}

// ── grass ring ───────────────────────────────────────────────────────────────

namespace {

    // Tiny self-contained hash noise for the grass patchiness mask — runs on
    // worker threads, no shared state, deterministic across platforms.
    inline float GrassHash(int x, int y, int seed) {
        uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u
                     + static_cast<uint32_t>(seed) * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
    }

    inline float GrassValueNoise(float x, float y, int seed) {
        const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
        const float fx = x - xi, fy = y - yi;
        const float u = fx * fx * (3.0f - 2.0f * fx), v = fy * fy * (3.0f - 2.0f * fy);
        const float a = GrassHash(xi, yi, seed), b = GrassHash(xi + 1, yi, seed);
        const float c = GrassHash(xi, yi + 1, seed), d = GrassHash(xi + 1, yi + 1, seed);
        return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
    }

    inline float GrassRand(uint32_t& state) {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (1.0f / 16777215.0f);
    }

}// namespace

void TerrainStreamer::UpdateGrass(const glm::vec3& cameraPos) {
    if (_props.grassDensity <= 0.0f || !_grassMaterial) return;

    const float cell = _props.grassCellSize;
    const glm::ivec2 camCell{ static_cast<int>(std::floor(cameraPos.x / cell)),
                              static_cast<int>(std::floor(cameraPos.z / cell)) };
    const int cr = std::max(1, static_cast<int>(std::ceil(_props.grassRadius / cell - 0.5f)));

    // Release cells that left the ring (+1 hysteresis) back into the pool.
    std::vector<glm::ivec2> departed;
    for (auto& [coord, gc] : _grassCells) {
        if (std::max(std::abs(coord.x - camCell.x), std::abs(coord.y - camCell.y)) > cr + 1) departed.push_back(coord);
    }
    for (const auto& coord : departed) {
        GrassCell* gc = _grassCells[coord];
        gc->go->SetActive(false);
        _grassCells.erase(coord);
        _grassPool.push_back(gc);
    }

    // Integrate one finished cell mesh per frame (a cell upload is ~1-3MB).
    for (auto it = _grassInFlight.begin(); it != _grassInFlight.end(); ++it) {
        if (!it->second->done.load(std::memory_order_acquire)) continue;
        GrassJob& job = *it->second;
        GrassCell* gc = AcquireGrassCell();
        gc->coord = job.coord;
        gc->mesh->UploadGrassInstances(job.instances);
        gc->go->SetPosition(glm::vec3(job.coord.x * cell, 0.0f, job.coord.y * cell));
        // Fully-masked cells stay inactive (nothing to draw) but still occupy
        // their _grassCells entry so the job isn't re-kicked every frame.
        gc->go->SetActive(!job.instances.empty());
        if (GrassCell* old = _grassCells.count(job.coord) ? _grassCells[job.coord] : nullptr) {
            old->go->SetActive(false);
            _grassPool.push_back(old);
        }
        _grassCells[job.coord] = gc;
        _grassInFlight.erase(it);
        break;
    }

    // Live blade count for the stats line (cells are few; recount is trivial).
    _grassBladesLive = 0;
    for (auto& [coord, gc] : _grassCells)
        _grassBladesLive += static_cast<int>(gc->mesh->instanceCount);

    // Kick worker jobs for missing cells, nearest first, two in flight max.
    if (_grassInFlight.size() >= 2) return;
    std::vector<glm::ivec2> missing;
    for (int dz = -cr; dz <= cr; ++dz) {
        for (int dx = -cr; dx <= cr; ++dx) {
            const glm::ivec2 coord{ camCell.x + dx, camCell.y + dz };
            if (_grassCells.count(coord) || _grassInFlight.count(coord)) continue;
            missing.push_back(coord);
        }
    }
    std::sort(missing.begin(), missing.end(), [&](const glm::ivec2& a, const glm::ivec2& b) {
        const glm::ivec2 da = a - camCell, db = b - camCell;
        return da.x * da.x + da.y * da.y < db.x * db.x + db.y * db.y;
    });

    for (const auto& coord : missing) {
        if (_grassInFlight.size() >= 2) break;
        auto job = std::make_shared<GrassJob>();
        job->coord = coord;
        _grassInFlight[coord] = job;

        // Copies for the worker — it must never touch `this`.
        auto heightFn = _heightFn;
        const float cellSize = cell, heightScale = _props.heightScale;
        const float density = _props.grassDensity, avgHeight = _props.grassBladeHeight;
        const float maxSlope = _props.grassMaxSlope, worldHalf = 0.5f * _props.worldSize;
        const glm::vec2 band = _props.grassHeightBand;
        const float coverage = std::clamp(_props.grassCoverage, 0.0f, 1.0f);
        const int seed = _props.noise.seed;

        JobSystem::Get()->Execute([job,
                                   heightFn,
                                   cellSize,
                                   heightScale,
                                   density,
                                   avgHeight,
                                   maxSlope,
                                   worldHalf,
                                   band,
                                   coverage,
                                   seed](int /*threadIndex*/) {
            const glm::vec2 origin(job->coord.x * cellSize, job->coord.y * cellSize);
            // Cap generous now that each blade is 32 bytes, not ~500.
            const int target = std::min(60000, static_cast<int>(density * cellSize * cellSize));
            job->instances.reserve(static_cast<size_t>(target));
            uint32_t rng = static_cast<uint32_t>(job->coord.x * 73856093 ^ job->coord.y * 19349663 ^ seed);

            for (int i = 0; i < target; ++i) {
                const float lx = GrassRand(rng) * cellSize;
                const float lz = GrassRand(rng) * cellSize;
                const float wx = origin.x + lx, wz = origin.y + lz;
                if (std::abs(wx) > worldHalf || std::abs(wz) > worldHalf) continue;

                // Patchiness: grass grows in drifts, not as a uniform carpet.
                // The drift probability is lifted toward 1 by grassCoverage so
                // thin drifts fill in without losing the variation (coverage=0
                // keeps the original bald-gap look, 1 = full carpet in-band).
                const float patch = GrassValueNoise(wx * 0.045f, wz * 0.045f, seed);
                const float driftProb = (patch - 0.25f) * 1.8f;
                if (GrassRand(rng) > driftProb + coverage * (1.0f - driftProb)) continue;

                const float h01 = heightFn(wx, wz);
                if (h01 < band.x || h01 > band.y) continue;
                const float y = h01 * heightScale;
                // Slope gate (skip entirely when maxSlope is huge — "ignore slope").
                if (maxSlope < 100.0f) {
                    const float dhx = (heightFn(wx + 2.0f, wz) - h01) * heightScale * 0.5f;
                    const float dhz = (heightFn(wx, wz + 2.0f) - h01) * heightScale * 0.5f;
                    if (dhx * dhx + dhz * dhz > maxSlope * maxSlope) continue;
                }

                // One instance carries everything unique about this blade;
                // grass.vert reconstructs the curved, wind-swayed geometry.
                GrassInstance inst;
                inst.root = glm::vec3(lx, y, lz);
                inst.facing = GrassRand(rng) * 6.28318f;
                inst.length = avgHeight * (0.65f + 0.7f * GrassRand(rng)) * (0.6f + 0.4f * patch);
                inst.lean = 0.05f + 0.30f * GrassRand(rng);
                inst.phase = GrassRand(rng) * 6.28318f;
                inst.hue = GrassRand(rng);
                job->instances.push_back(inst);
            }
            job->done.store(true, std::memory_order_release);
        });
    }
}

TerrainStreamer::GrassCell* TerrainStreamer::AcquireGrassCell() {
    if (!_grassPool.empty()) {
        GrassCell* gc = _grassPool.back();
        _grassPool.pop_back();
        return gc;
    }
    auto& am = AssetManager::Get();
    auto owned = std::make_unique<GrassCell>();
    GrassCell* gc = owned.get();
    const std::string name = "grasscell_" + std::to_string(_allGrassCells.size());

    GameObject* go = _app->CreateGameObject(glm::vec3(0.0f));
    go->SetName(name);
    go->parent = _root;
    go->SetActive(false);// stays hidden until the first mesh upload
    gc->go = go;

    auto* mesh = new Mesh(MeshType::GRASS);
    mesh->updateFreq = UpdateFrequency::Dynamic;
    const float c = _props.grassCellSize, top = _props.heightScale + _props.grassBladeHeight;
    mesh->SetBoundingBox(
        { { glm::vec3(c, top, c),
            glm::vec3(0, top, c),
            glm::vec3(0, 0, c),
            glm::vec3(c, 0, c),
            glm::vec3(c, top, 0),
            glm::vec3(0, top, 0),
            glm::vec3(0, 0, 0),
            glm::vec3(c, 0, 0) } }
    );
    mesh->InitGrassInstanced();
    MeshHandle handle = am.CreateMesh(name, mesh);
    mesh->SetMaterial(am.GetMaterialHandle(_grassMaterial));
    go->AddComponent<MeshComponent>(handle);
    gc->mesh = mesh;

    _allGrassCells.push_back(std::move(owned));
    return gc;
}
