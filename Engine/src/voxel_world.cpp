#include "voxel_world.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "frustum.hpp"
#include "game_object.hpp"
#include "graphics_subsystem.hpp"
#include "job_system.hpp"
#include "light_component.hpp"
#include "renderer.hpp"
#include "sun_component.hpp"

#include "FastNoiseLite.h"

#include <algorithm>
#include <cmath>

// ── helpers ──────────────────────────────────────────────────────────────────

static int WorldToChunkCoord(float w) {
    return static_cast<int>(std::floor(w / VoxelChunkComponent::SIZE));
}

// ── public ───────────────────────────────────────────────────────────────────

void VoxelWorld::Init(Application* app, int seed, GameObject* root, MaterialHandle voxelMaterial) {
    _app = app;
    _gfx = GraphicsSubsystem::Get();
    _seed = seed;
    _root = root;
    _voxelMaterial = voxelMaterial;

    // Warm up chunk pool: pre-allocate the full view volume so initial load
    // doesn't hit any system allocator pressure.
    const int totalSlots = (2 * VIEW_X + 1) * WORLD_Y * (2 * VIEW_Z + 1);
    _pool.reserve(totalSlots);

    // Load initial view volume around the origin.
    for (int dx = -VIEW_X; dx <= VIEW_X; ++dx) {
        for (int cy = 0; cy < WORLD_Y; ++cy) {
            for (int dz = -VIEW_Z; dz <= VIEW_Z; ++dz) {
                LoadChunk({ dx, cy, dz });
            }
        }
    }
    _lastCamChunk = { 0, 0, 0 };

    LinkNeighbors();
    RebuildDirtyChunks();

    // Sun
    GameObject* sunGO = app->CreateGameObject(glm::vec3(0));
    sunGO->SetName("Sun");
    sunGO->parent = _root;
    sunGO->AddComponent(new LightComponent(
        sunGO,
        LightProps{
            .type = LightType::Directional,
            .ambient = glm::vec3(1.0f),
            .diffuse = glm::vec3(1.0f),
            .specular = glm::vec3(1.0f),
            .direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f)),
            .intensity = 1.0f,
            .castShadow = false,
        }
    ));
    sunGO->AddComponent(new SunComponent(glm::vec3(1.0f, 0.4f, 0.0f) * 50.0f, /*billboardRadius=*/10.0f));
}

void VoxelWorld::Update(float /*dt*/, const glm::vec3& cameraPos) {
    glm::ivec3 camChunk{ WorldToChunkCoord(cameraPos.x), 0, WorldToChunkCoord(cameraPos.z) };

    if (infiniteMode) {
        // Unload chunks that have drifted outside the view + margin.
        if (camChunk.x != _lastCamChunk.x || camChunk.z != _lastCamChunk.z) {
            _lastCamChunk = camChunk;

            std::vector<glm::ivec3> toUnload;
            toUnload.reserve(64);
            for (auto& [pos, chunk] : _chunkMap) {
                if (std::abs(pos.x - camChunk.x) > VIEW_X + UNLOAD_MARGIN
                    || std::abs(pos.z - camChunk.z) > VIEW_Z + UNLOAD_MARGIN) {
                    toUnload.push_back(pos);
                }
            }
            for (auto& pos : toUnload)
                UnloadChunk(pos);
        }

        // Load up to LOAD_PER_FRAME new chunks per Update(), closest first.
        std::vector<glm::ivec3> needed;
        for (int dx = -VIEW_X; dx <= VIEW_X; ++dx) {
            for (int cy = 0; cy < WORLD_Y; ++cy) {
                for (int dz = -VIEW_Z; dz <= VIEW_Z; ++dz) {
                    glm::ivec3 pos{ camChunk.x + dx, cy, camChunk.z + dz };
                    if (_chunkMap.find(pos) == _chunkMap.end()) {
                        needed.push_back(pos);
                    }
                }
            }
        }

        if (!needed.empty()) {
            std::sort(needed.begin(), needed.end(), [&](const glm::ivec3& a, const glm::ivec3& b) {
                int da = std::abs(a.x - camChunk.x) + std::abs(a.z - camChunk.z);
                int db = std::abs(b.x - camChunk.x) + std::abs(b.z - camChunk.z);
                return da < db;
            });

            int loaded = 0;
            for (auto& pos : needed) {
                if (loaded >= LOAD_PER_FRAME) break;
                LoadChunk(pos);
                ++loaded;
            }
            LinkNeighbors();
        }
    }

    RebuildDirtyChunks();
}

void VoxelWorld::SubmitRenderCommands(Renderer* renderer, const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    Frustum frustum(viewProj);

    // Auxiliary views (portal recursion levels, water reflection) re-render
    // these chunks from other viewpoints. Per-view culling: keep a chunk if
    // it's visible in the main frustum OR any aux frustum, rather than
    // submitting the whole streamed set whenever an aux view is active.
    std::vector<Frustum> auxFrusta;
    if (renderer) {
        for (const glm::mat4& vp : renderer->GetAuxViewProjs())
            auxFrusta.emplace_back(vp);
    }

    for (auto& [pos, chunk] : _chunkMap) {
        Mesh* mesh = chunk->GetMesh();
        if (!mesh || !mesh->UsesRenderMesh()) continue;

        const glm::vec3 bsCenter = chunk->GetBoundingSphereCenter();
        bool visible = frustum.IntersectsSphere(bsCenter, VoxelChunkComponent::BSPHERE_RADIUS);
        for (const Frustum& aux : auxFrusta) {
            if (visible) break;
            visible = aux.IntersectsSphere(bsCenter, VoxelChunkComponent::BSPHERE_RADIUS);
        }
        if (!visible) continue;

        glm::vec3 wp = chunk->GetWorldPos();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), wp);

        RenderCommand cmd{ .mesh = chunk->GetMeshHandle(), .transform = model };
        renderer->SubmitCommand(cmd);
    }
}

uint8_t VoxelWorld::GetVoxel(int wx, int wy, int wz) const {
    int cx = static_cast<int>(std::floor(static_cast<float>(wx) / VoxelChunkComponent::SIZE));
    int cy = static_cast<int>(std::floor(static_cast<float>(wy) / VoxelChunkComponent::SIZE));
    int cz = static_cast<int>(std::floor(static_cast<float>(wz) / VoxelChunkComponent::SIZE));
    int lx = wx - cx * VoxelChunkComponent::SIZE;
    int ly = wy - cy * VoxelChunkComponent::SIZE;
    int lz = wz - cz * VoxelChunkComponent::SIZE;
    VoxelChunkComponent* c = GetChunk(cx, cy, cz);
    return c ? c->GetVoxel(lx, ly, lz) : 0;
}

void VoxelWorld::SetVoxel(int wx, int wy, int wz, uint8_t type) {
    int cx = static_cast<int>(std::floor(static_cast<float>(wx) / VoxelChunkComponent::SIZE));
    int cy = static_cast<int>(std::floor(static_cast<float>(wy) / VoxelChunkComponent::SIZE));
    int cz = static_cast<int>(std::floor(static_cast<float>(wz) / VoxelChunkComponent::SIZE));
    int lx = wx - cx * VoxelChunkComponent::SIZE;
    int ly = wy - cy * VoxelChunkComponent::SIZE;
    int lz = wz - cz * VoxelChunkComponent::SIZE;
    VoxelChunkComponent* c = GetChunk(cx, cy, cz);
    if (c) c->SetVoxel(lx, ly, lz, type);
}

bool VoxelWorld::RaycastVoxel(const glm::vec3& ro, const glm::vec3& rd, float maxDist, glm::vec3& outHitWorld) const {
    // Amanatides & Woo DDA over the 1 m world voxel grid.
    int cx = static_cast<int>(std::floor(ro.x));
    int cy = static_cast<int>(std::floor(ro.y));
    int cz = static_cast<int>(std::floor(ro.z));
    const int sx = rd.x > 0.0f ? 1 : (rd.x < 0.0f ? -1 : 0);
    const int sy = rd.y > 0.0f ? 1 : (rd.y < 0.0f ? -1 : 0);
    const int sz = rd.z > 0.0f ? 1 : (rd.z < 0.0f ? -1 : 0);
    const float big = 1e30f;
    const float idx = rd.x != 0.0f ? 1.0f / rd.x : big;
    const float idy = rd.y != 0.0f ? 1.0f / rd.y : big;
    const float idz = rd.z != 0.0f ? 1.0f / rd.z : big;
    const float tdx = std::abs(idx);
    const float tdy = std::abs(idy);
    const float tdz = std::abs(idz);
    // Distance to the first voxel boundary on each axis (big if the ray is flat there).
    float tmx = sx != 0 ? (static_cast<float>(cx + (sx > 0 ? 1 : 0)) - ro.x) * idx : big;
    float tmy = sy != 0 ? (static_cast<float>(cy + (sy > 0 ? 1 : 0)) - ro.y) * idy : big;
    float tmz = sz != 0 ? (static_cast<float>(cz + (sz > 0 ? 1 : 0)) - ro.z) * idz : big;
    float t = 0.0f;
    const int maxSteps = static_cast<int>(maxDist * 3.0f) + 3;
    for (int i = 0; i < maxSteps; i++) {
        if (GetVoxel(cx, cy, cz) != 0) {
            outHitWorld = ro + rd * t;
            return true;
        }
        if (tmx <= tmy && tmx <= tmz) {
            cx += sx;
            t = tmx;
            tmx += tdx;
        } else if (tmy <= tmz) {
            cy += sy;
            t = tmy;
            tmy += tdy;
        } else {
            cz += sz;
            t = tmz;
            tmz += tdz;
        }
        if (t > maxDist) break;
    }
    return false;
}

void VoxelWorld::CarveSphere(const glm::vec3& center, float radius) {
    if (radius <= 0.0f) return;
    const int lox = static_cast<int>(std::floor(center.x - radius));
    const int loy = static_cast<int>(std::floor(center.y - radius));
    const int loz = static_cast<int>(std::floor(center.z - radius));
    const int hix = static_cast<int>(std::floor(center.x + radius));
    const int hiy = static_cast<int>(std::floor(center.y + radius));
    const int hiz = static_cast<int>(std::floor(center.z + radius));
    const float r2 = radius * radius;
    for (int wy = loy; wy <= hiy; wy++) {
        for (int wz = loz; wz <= hiz; wz++) {
            for (int wx = lox; wx <= hix; wx++) {
                const float dx = static_cast<float>(wx) + 0.5f - center.x;
                const float dy = static_cast<float>(wy) + 0.5f - center.y;
                const float dz = static_cast<float>(wz) + 0.5f - center.z;
                if (dx * dx + dy * dy + dz * dz > r2) continue;
                if (GetVoxel(wx, wy, wz) != 0) SetVoxel(wx, wy, wz, 0);// SetVoxel marks the containing chunk dirty
            }
        }
    }
    // Mark every chunk overlapping the carve AABB (expanded by one voxel) dirty,
    // so boundary neighbours re-mesh: a carved chunk-edge voxel exposes a face on
    // the adjacent chunk that SetVoxel alone would not flag. RebuildDirtyChunks()
    // (in Update) rebuilds the greedy meshes.
    const int S = VoxelChunkComponent::SIZE;
    auto chunkOf = [S](int w) { return static_cast<int>(std::floor(static_cast<float>(w) / static_cast<float>(S))); };
    for (int cyi = chunkOf(loy - 1); cyi <= chunkOf(hiy + 1); cyi++) {
        for (int czi = chunkOf(loz - 1); czi <= chunkOf(hiz + 1); czi++) {
            for (int cxi = chunkOf(lox - 1); cxi <= chunkOf(hix + 1); cxi++) {
                if (VoxelChunkComponent* c = GetChunk(cxi, cyi, czi)) c->MarkDirty();
            }
        }
    }
    giDirty = true;// carving changed the voxels; the VCT grid must re-inject
}

// ── private ──────────────────────────────────────────────────────────────────

VoxelChunkComponent* VoxelWorld::GetChunk(int cx, int cy, int cz) const {
    auto it = _chunkMap.find({ cx, cy, cz });
    return (it != _chunkMap.end()) ? it->second : nullptr;
}

VoxelChunkComponent* VoxelWorld::AcquireSlot(glm::ivec3 pos) {
    if (!_pool.empty()) {
        VoxelChunkComponent* slot = _pool.back();
        _pool.pop_back();
        slot->Relocate(pos);
        return slot;
    }
    // Pool is empty — allocate a new GameObject + component.
    glm::vec3 worldPos = glm::vec3(pos) * static_cast<float>(VoxelChunkComponent::SIZE);
    GameObject* go = _app->CreateGameObject(worldPos);
    go->SetName("VoxelChunk_" + std::to_string(pos.x) + "_" + std::to_string(pos.y) + "_" + std::to_string(pos.z));
    go->parent = _root;
    auto* comp = new VoxelChunkComponent(go, _gfx, pos, _voxelMaterial);
    go->AddComponent(comp);
    return comp;
}

void VoxelWorld::LoadChunk(glm::ivec3 pos) {
    if (pos.y < 0 || pos.y >= WORLD_Y) return;
    VoxelChunkComponent* chunk = AcquireSlot(pos);
    _chunkMap[pos] = chunk;
    GenerateChunkTerrain(chunk);
    giDirty = true;// new voxels streamed in; refresh the VCT grid
}

void VoxelWorld::UnloadChunk(glm::ivec3 pos) {
    auto it = _chunkMap.find(pos);
    if (it == _chunkMap.end()) return;
    VoxelChunkComponent* chunk = it->second;
    _chunkMap.erase(it);

    // Disconnect from all neighbors so they don't reference stale data.
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                VoxelChunkComponent* nb = GetChunk(pos.x + dx, pos.y + dy, pos.z + dz);
                if (nb) nb->SetNeighbor(-dx, -dy, -dz, nullptr);
            }
        }
    }

    // Return to pool for reuse.
    _pool.push_back(chunk);
}

void VoxelWorld::GenerateChunkTerrain(VoxelChunkComponent* chunk) {
    FastNoiseLite heightNoise;
    heightNoise.SetSeed(_seed);
    heightNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    heightNoise.SetFrequency(0.0035f);
    heightNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    heightNoise.SetFractalOctaves(8);
    heightNoise.SetFractalLacunarity(2.0f);
    heightNoise.SetFractalGain(0.5f);

    FastNoiseLite caveNoise;
    caveNoise.SetSeed(_seed + 1);
    caveNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    caveNoise.SetFrequency(0.04f);

    const int worldYVoxels = WORLD_Y * VoxelChunkComponent::SIZE;
    glm::ivec3 cp = chunk->GetChunkPos();

    for (int lx = 0; lx < VoxelChunkComponent::SIZE; ++lx) {
        for (int lz = 0; lz < VoxelChunkComponent::SIZE; ++lz) {
            int wx = cp.x * VoxelChunkComponent::SIZE + lx;
            int wz = cp.z * VoxelChunkComponent::SIZE + lz;

            float h = heightNoise.GetNoise(static_cast<float>(wx), static_cast<float>(wz));
            int height = std::clamp(static_cast<int>(h * 32.0f + 32.0f), 0, worldYVoxels - 1);

            for (int wy = cp.y * VoxelChunkComponent::SIZE; wy < (cp.y + 1) * VoxelChunkComponent::SIZE && wy < height;
                 ++wy) {
                float cv = caveNoise.GetNoise(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz));
                if (cv > 0.55f && wy > 4) continue;
                int ly = wy - cp.y * VoxelChunkComponent::SIZE;
                chunk->SetVoxel(lx, ly, lz, static_cast<uint8_t>(std::min(wy + 1, 255)));
            }
        }
    }
}

void VoxelWorld::LinkNeighbors() {
    for (auto& [pos, chunk] : _chunkMap) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    chunk->SetNeighbor(dx, dy, dz, GetChunk(pos.x + dx, pos.y + dy, pos.z + dz));
                }
            }
        }
    }
}

void VoxelWorld::RebuildDirtyChunks() {
    struct MeshTask {
        VoxelChunkComponent* chunk;
        std::vector<VoxelVertex> vertices;
    };
    std::vector<std::shared_ptr<MeshTask>> pendingTasks;

    for (auto& [pos, chunk] : _chunkMap) {
        if (chunk->IsDirty()) {
            auto task = std::make_shared<MeshTask>();
            task->chunk = chunk;
            pendingTasks.push_back(task);

            JobSystem::Get()->Execute([task](int /*threadIndex*/) {
                task->vertices = task->chunk->GenerateMeshData();
            });
        }
    }

    if (!pendingTasks.empty()) {
        JobSystem::Get()->Wait();
        for (auto& task : pendingTasks) {
            task->chunk->UploadMesh(task->vertices);
        }
    }
}
