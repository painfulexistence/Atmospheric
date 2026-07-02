#include "voxel_world.hpp"
#include "job_system.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "frustum.hpp"
#include "light_component.hpp"
#include "sun_component.hpp"

#include "FastNoiseLite.h"

#include <algorithm>
#include <cmath>

// ── helpers ──────────────────────────────────────────────────────────────────

static int WorldToChunkCoord(float w) {
    return (int)std::floor(w / VoxelChunkComponent::SIZE);
}

// ── public ───────────────────────────────────────────────────────────────────

void VoxelWorld::Init(Application* app, int seed, GameObject* root) {
    _app  = app;
    _gfx  = app->GetGraphicsServer();
    _seed = seed;
    _root = root;

    // Warm up chunk pool: pre-allocate the full view volume so initial load
    // doesn't hit any system allocator pressure.
    const int totalSlots = (2*VIEW_X+1) * WORLD_Y * (2*VIEW_Z+1);
    _pool.reserve(totalSlots);

    // Load initial view volume around the origin.
    for (int dx = -VIEW_X; dx <= VIEW_X; ++dx) {
        for (int cy = 0; cy < WORLD_Y; ++cy) {
            for (int dz = -VIEW_Z; dz <= VIEW_Z; ++dz) {
                LoadChunk({dx, cy, dz});
            }
        }
    }
    _lastCamChunk = {0, 0, 0};

    LinkNeighbors();
    RebuildDirtyChunks();

    // Sun
    GameObject* sunGO = app->CreateGameObject(glm::vec3(0));
    sunGO->SetName("Sun");
    sunGO->parent = _root;
    sunGO->AddComponent(new LightComponent(sunGO, LightProps{
        .type      = LightType::Directional,
        .ambient   = glm::vec3(1.0f),
        .diffuse   = glm::vec3(1.0f),
        .specular  = glm::vec3(1.0f),
        .direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f)),
        .intensity = 1.0f,
        .castShadow = false,
    }));
    sunGO->AddComponent(new SunComponent());
}

void VoxelWorld::Update(float /*dt*/, const glm::vec3& cameraPos) {
    glm::ivec3 camChunk{WorldToChunkCoord(cameraPos.x), 0,
                         WorldToChunkCoord(cameraPos.z)};

    if (infiniteMode) {
        // Unload chunks that have drifted outside the view + margin.
        if (camChunk.x != _lastCamChunk.x || camChunk.z != _lastCamChunk.z) {
            _lastCamChunk = camChunk;

            std::vector<glm::ivec3> toUnload;
            toUnload.reserve(64);
            for (auto& [pos, chunk] : _chunkMap) {
                if (std::abs(pos.x - camChunk.x) > VIEW_X + UNLOAD_MARGIN ||
                    std::abs(pos.z - camChunk.z) > VIEW_Z + UNLOAD_MARGIN) {
                    toUnload.push_back(pos);
                }
            }
            for (auto& pos : toUnload) UnloadChunk(pos);
        }

        // Load up to LOAD_PER_FRAME new chunks per Update(), closest first.
        std::vector<glm::ivec3> needed;
        for (int dx = -VIEW_X; dx <= VIEW_X; ++dx) {
            for (int cy = 0; cy < WORLD_Y; ++cy) {
                for (int dz = -VIEW_Z; dz <= VIEW_Z; ++dz) {
                    glm::ivec3 pos{camChunk.x + dx, cy, camChunk.z + dz};
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

void VoxelWorld::SubmitRenderCommands(Renderer* renderer,
                                      const glm::mat4& viewProj,
                                      const glm::vec3& cameraPos)
{
    Frustum frustum(viewProj);

    for (auto& [pos, chunk] : _chunkMap) {
        Mesh* mesh = chunk->GetMesh();
        if (!mesh || !mesh->UsesRenderMesh()) continue;

        if (!frustum.IntersectsSphere(chunk->GetBoundingSphereCenter(),
                                       VoxelChunkComponent::BSPHERE_RADIUS)) continue;

        glm::vec3 wp = chunk->GetWorldPos();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), wp);

        RenderCommand cmd{ .mesh = chunk->GetMeshHandle(), .transform = model };
        renderer->SubmitCommand(cmd);
    }
}

uint8_t VoxelWorld::GetVoxel(int wx, int wy, int wz) const {
    int cx = (int)std::floor((float)wx / VoxelChunkComponent::SIZE);
    int cy = (int)std::floor((float)wy / VoxelChunkComponent::SIZE);
    int cz = (int)std::floor((float)wz / VoxelChunkComponent::SIZE);
    int lx = wx - cx * VoxelChunkComponent::SIZE;
    int ly = wy - cy * VoxelChunkComponent::SIZE;
    int lz = wz - cz * VoxelChunkComponent::SIZE;
    VoxelChunkComponent* c = GetChunk(cx, cy, cz);
    return c ? c->GetVoxel(lx, ly, lz) : 0;
}

void VoxelWorld::SetVoxel(int wx, int wy, int wz, uint8_t type) {
    int cx = (int)std::floor((float)wx / VoxelChunkComponent::SIZE);
    int cy = (int)std::floor((float)wy / VoxelChunkComponent::SIZE);
    int cz = (int)std::floor((float)wz / VoxelChunkComponent::SIZE);
    int lx = wx - cx * VoxelChunkComponent::SIZE;
    int ly = wy - cy * VoxelChunkComponent::SIZE;
    int lz = wz - cz * VoxelChunkComponent::SIZE;
    VoxelChunkComponent* c = GetChunk(cx, cy, cz);
    if (c) c->SetVoxel(lx, ly, lz, type);
}

// ── private ──────────────────────────────────────────────────────────────────

VoxelChunkComponent* VoxelWorld::GetChunk(int cx, int cy, int cz) const {
    auto it = _chunkMap.find({cx, cy, cz});
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
    glm::vec3 worldPos = glm::vec3(pos) * (float)VoxelChunkComponent::SIZE;
    GameObject* go = _app->CreateGameObject(worldPos);
    go->SetName("VoxelChunk_" + std::to_string(pos.x) + "_" +
                std::to_string(pos.y) + "_" + std::to_string(pos.z));
    go->parent = _root;
    auto* comp = new VoxelChunkComponent(go, _gfx, pos);
    go->AddComponent(comp);
    return comp;
}

void VoxelWorld::LoadChunk(glm::ivec3 pos) {
    if (pos.y < 0 || pos.y >= WORLD_Y) return;
    VoxelChunkComponent* chunk = AcquireSlot(pos);
    _chunkMap[pos] = chunk;
    GenerateChunkTerrain(chunk);
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
                VoxelChunkComponent* nb = GetChunk(pos.x+dx, pos.y+dy, pos.z+dz);
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

            float h = heightNoise.GetNoise((float)wx, (float)wz);
            int height = std::clamp((int)(h * 32.0f + 32.0f), 0, worldYVoxels - 1);

            for (int wy = cp.y * VoxelChunkComponent::SIZE;
                 wy < (cp.y + 1) * VoxelChunkComponent::SIZE && wy < height; ++wy) {
                float cv = caveNoise.GetNoise((float)wx, (float)wy, (float)wz);
                if (cv > 0.55f && wy > 4) continue;
                int ly = wy - cp.y * VoxelChunkComponent::SIZE;
                chunk->SetVoxel(lx, ly, lz, (uint8_t)std::min(wy + 1, 255));
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
                    chunk->SetNeighbor(dx, dy, dz,
                        GetChunk(pos.x+dx, pos.y+dy, pos.z+dz));
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
