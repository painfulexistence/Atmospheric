#include "voxel_world.hpp"
#include "application.hpp"
#include "asset_manager.hpp"
#include "game_object.hpp"
#include "graphics_server.hpp"
#include "frustum.hpp"
#include "light_component.hpp"
#include "material.hpp"
#include "sun_component.hpp"

#include "FastNoiseLite.h"

#include <algorithm>
#include <cmath>

void VoxelWorld::Init(Application* app, int seed) {
    _gfx  = app->GetGraphicsServer();
    _seed = seed;

    const int total = WORLD_X * WORLD_Y * WORLD_Z;
    _chunks.reserve(total);

    for (int cx = 0; cx < WORLD_X; ++cx) {
        for (int cy = 0; cy < WORLD_Y; ++cy) {
            for (int cz = 0; cz < WORLD_Z; ++cz) {
                glm::vec3 worldPos = glm::vec3(cx, cy, cz) *
                                     static_cast<float>(VoxelChunkComponent::SIZE);
                GameObject* go = app->CreateGameObject(worldPos);
                go->SetName("VoxelChunk_" + std::to_string(cx) + "_" +
                            std::to_string(cy) + "_" + std::to_string(cz));

                auto* comp = new VoxelChunkComponent(go, _gfx, glm::ivec3(cx, cy, cz));
                go->AddComponent(comp);
                _chunks.push_back(comp);
            }
        }
    }

    GenerateTerrain();
    LinkNeighbors();
    RebuildDirtyChunks();

    // Sun GameObject: owns the directional light + visual billboard params
    GameObject* sunGO = app->CreateGameObject(glm::vec3(0));
    sunGO->SetName("Sun");
    sunGO->AddComponent(new LightComponent(sunGO, LightProps{
        .type      = LightType::Directional,
        .ambient   = glm::vec3(1.0f),
        .diffuse   = glm::vec3(1.0f),
        .specular  = glm::vec3(1.0f),
        .direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.5f)), // VX default
        .intensity = 1.0f,
        .castShadow = false,
    }));
    sunGO->AddComponent(new SunComponent());  // default color/radius/height match VX

    // Single water plane covering the whole world at WATER_LINE, matching VX
    float worldW = WORLD_X * VoxelChunkComponent::SIZE;
    float worldD = WORLD_Z * VoxelChunkComponent::SIZE;
    _waterMesh = AssetManager::Get().CreatePlaneMeshSubdivided("VoxelWaterPlane", worldW, worldD, 64);

    _waterMat = new Material(MaterialProps{});
    _waterMat->renderQueue = RenderQueue::Transparent;
    _waterMesh->SetMaterial(_waterMat);
}

void VoxelWorld::Update(float /*dt*/, const glm::vec3& /*cameraPos*/) {
    RebuildDirtyChunks();
}

void VoxelWorld::SubmitRenderCommands(Renderer* renderer,
                                      const glm::mat4& viewProj,
                                      const glm::vec3& cameraPos)
{
    Frustum frustum(viewProj);

    for (auto* chunk : _chunks) {
        Mesh* mesh = chunk->GetMesh();
        if (!mesh || !mesh->UsesRenderMesh()) continue;

        if (!frustum.IntersectsSphere(chunk->GetBoundingSphereCenter(),
                                       VoxelChunkComponent::BSPHERE_RADIUS)) continue;

        glm::vec3 wp = chunk->GetWorldPos();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), wp);

        RenderCommand cmd{ .mesh = mesh, .transform = model };
        renderer->SubmitCommand(cmd);
    }

    // Water plane at WATER_LINE (slight z-offset to avoid z-fighting, matching VX)
    if (_waterMesh) {
        float cx = (WORLD_X * VoxelChunkComponent::SIZE) * 0.5f;
        float cz = (WORLD_Z * VoxelChunkComponent::SIZE) * 0.5f;
        glm::mat4 waterModel = glm::translate(glm::mat4(1.0f),
                                              glm::vec3(cx, WATER_LINE + 0.05f, cz));
        renderer->SubmitCommand({ .mesh = _waterMesh, .transform = waterModel });
    }
}

uint8_t VoxelWorld::GetVoxel(int wx, int wy, int wz) const {
    int cx = wx / VoxelChunkComponent::SIZE, lx = wx % VoxelChunkComponent::SIZE;
    int cy = wy / VoxelChunkComponent::SIZE, ly = wy % VoxelChunkComponent::SIZE;
    int cz = wz / VoxelChunkComponent::SIZE, lz = wz % VoxelChunkComponent::SIZE;
    VoxelChunkComponent* c = GetChunk(cx, cy, cz);
    return c ? c->GetVoxel(lx, ly, lz) : 0;
}

void VoxelWorld::SetVoxel(int wx, int wy, int wz, uint8_t type) {
    int cx = wx / VoxelChunkComponent::SIZE, lx = wx % VoxelChunkComponent::SIZE;
    int cy = wy / VoxelChunkComponent::SIZE, ly = wy % VoxelChunkComponent::SIZE;
    int cz = wz / VoxelChunkComponent::SIZE, lz = wz % VoxelChunkComponent::SIZE;
    VoxelChunkComponent* c = GetChunk(cx, cy, cz);
    if (c) c->SetVoxel(lx, ly, lz, type);
}

VoxelChunkComponent* VoxelWorld::GetChunk(int cx, int cy, int cz) const {
    int idx = ChunkIndex(cx, cy, cz);
    if (idx < 0) return nullptr;
    return _chunks[idx];
}

int VoxelWorld::ChunkIndex(int cx, int cy, int cz) const {
    if (cx < 0 || cx >= WORLD_X) return -1;
    if (cy < 0 || cy >= WORLD_Y) return -1;
    if (cz < 0 || cz >= WORLD_Z) return -1;
    return cx * WORLD_Y * WORLD_Z + cy * WORLD_Z + cz;
}

void VoxelWorld::GenerateTerrain() {
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

    // Match VX: height = noise * 32 + 32, voxel_id = wy + 1 (palette driven by height)
    const int worldYVoxels = WORLD_Y * VoxelChunkComponent::SIZE;

    for (int cx = 0; cx < WORLD_X; ++cx) {
        for (int cz = 0; cz < WORLD_Z; ++cz) {
            for (int lx = 0; lx < VoxelChunkComponent::SIZE; ++lx) {
                for (int lz = 0; lz < VoxelChunkComponent::SIZE; ++lz) {
                    int wx = cx * VoxelChunkComponent::SIZE + lx;
                    int wz = cz * VoxelChunkComponent::SIZE + lz;

                    float h = heightNoise.GetNoise((float)wx, (float)wz);
                    int height = std::clamp((int)(h * 32.0f + 32.0f), 0, worldYVoxels - 1);

                    for (int wy = 0; wy < height; ++wy) {
                        float cv = caveNoise.GetNoise((float)wx, (float)wy, (float)wz);
                        if (cv > 0.55f && wy > 4) continue;

                        int cy = wy / VoxelChunkComponent::SIZE;
                        int ly = wy % VoxelChunkComponent::SIZE;
                        VoxelChunkComponent* chunk = GetChunk(cx, cy, cz);
                        if (!chunk) continue;

                        // voxel_id = wy + 1, matching VX's height-based palette
                        chunk->SetVoxel(lx, ly, lz, (uint8_t)std::min(wy + 1, 255));
                    }
                }
            }
        }
    }
}

void VoxelWorld::LinkNeighbors() {
    for (int cx = 0; cx < WORLD_X; ++cx) {
        for (int cy = 0; cy < WORLD_Y; ++cy) {
            for (int cz = 0; cz < WORLD_Z; ++cz) {
                VoxelChunkComponent* chunk = GetChunk(cx, cy, cz);
                if (!chunk) continue;

                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        if (dx == 0 && dz == 0) continue;
                        chunk->SetNeighbor(dx, dz, GetChunk(cx + dx, cy, cz + dz));
                    }
                }
            }
        }
    }
}

void VoxelWorld::RebuildDirtyChunks() {
    for (auto* chunk : _chunks) {
        if (chunk->IsDirty()) {
            chunk->RebuildMesh();
        }
    }
}
