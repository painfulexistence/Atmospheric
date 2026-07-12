#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"
#include "Atmospheric/material.hpp"
#include "Atmospheric/mesh.hpp"
#include "Atmospheric/mesh_builder.hpp"
#include "Atmospheric/mesh_component.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

// 10km x 10km streamed open-world terrain.
//
// The whole world is prewarmed at the coarsest LOD during load (full horizon
// on frame one, Ghost-of-Tsushima-style fast boot), then TerrainStreamer
// refines concentric detail rings around the camera on worker threads while
// you fly — no hitches, no pop-in holes, Bullet colliders following the
// camera tile.
//
// Controls: WASD move, arrows look, X sprint (x50), Z slow, R/F up/down,
//           G toggle ground-clamp, T teleport +2km (streaming stress test),
//           P cycle terrain palette, L LOD tint (colors each detail ring),
//           I wireframe, ESC quit.

// Keeps the fly camera above the streamed terrain. Ticks after the
// CameraController3D on the same GameObject (components tick in add order),
// so the clamp applies to this frame's movement.
class GroundClampComponent : public Component {
public:
    GroundClampComponent(GameObject* owner, StreamingTerrainComponent* terrain) : _terrain(terrain) {
        gameObject = owner;
    }
    std::string GetName() const override {
        return "GroundClamp";
    }
    void OnAttach() override {
    }
    void OnDetach() override {
    }
    void OnTick(float /*dt*/) override {
        if (!enabled) return;
        glm::vec3 pos = gameObject->GetPosition();
        pos.y = std::max(pos.y, _terrain->GetHeight(pos.x, pos.z) + 2.0f);
        gameObject->SetPosition(pos);
    }

    bool enabled = true;

private:
    StreamingTerrainComponent* _terrain;
};

class TerrainStreamingDemo : public Application {
    using Application::Application;

    // The terrain is a reusable engine component now: it owns the streamer and
    // drives it from its own OnTick, so the app just holds a handle for height
    // queries / debug toggles.
    StreamingTerrainComponent* _terrain = nullptr;
    CameraComponent* _cam = nullptr;
    GameObject* _camGO = nullptr;
    GroundClampComponent* _groundClamp = nullptr;
    MeshHandle _treeMesh;
    MeshHandle _rockMesh;

    bool _wireframe = false;
    float _statsTimer = 0.0f;
    std::chrono::steady_clock::time_point _bootTime;
    bool _reportedStreamed = false;

    void OnInit() override {
        _bootTime = std::chrono::steady_clock::now();
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _cam = mainCamera;
        _camGO = _cam->gameObject;
        // Wide fov + far plane past the world diagonal (14.5km) so the whole
        // streamed horizon is in view. fov is in degrees.
        _cam->SetPerspective(
            55.0f, static_cast<float>(INIT_SCREEN_WIDTH) / static_cast<float>(INIT_SCREEN_HEIGHT), 0.5f, 30000.0f
        );

        // Sun
        auto* sunGO = CreateGameObject(glm::vec3(0.0f));
        sunGO->SetName("Sun");
        sunGO->AddComponent(new LightComponent(
            sunGO,
            LightProps{
                .type = LightType::Directional,
                .ambient = glm::vec3(1.0f),
                .diffuse = glm::vec3(1.0f),
                .specular = glm::vec3(1.0f),
                .direction = glm::normalize(glm::vec3(-0.4f, -1.0f, 0.35f)),
                .intensity = 1.0f,
                .castShadow = false,
            }
        ));

        // Shared entity meshes: one mesh + material per type, so every spawned
        // instance batches into the same draw.
        auto& am = AssetManager::Get();
        Mesh* tree = MeshBuilder::CreateCube(1.0f);
        tree->SetMaterial(am.GetMaterialHandle(
            am.CreateMaterial("ent_tree_mat", MaterialProps{ .diffuse = glm::vec3(0.16f, 0.42f, 0.18f) })
        ));
        _treeMesh = am.CreateMesh("ent_tree", tree);
        Mesh* rock = MeshBuilder::CreateSphere(0.5f, 10);
        rock->SetMaterial(am.GetMaterialHandle(
            am.CreateMaterial("ent_rock_mat", MaterialProps{ .diffuse = glm::vec3(0.45f, 0.44f, 0.42f) })
        ));
        _rockMesh = am.CreateMesh("ent_rock", rock);

        // The terrain GameObject roots every tile/collider/entity; the
        // component's OnAttach prewarms the world (synchronous, ~1 frame) and
        // its OnTick streams thereafter — no per-frame work left in the app.
        auto* terrainGO = CreateGameObject(glm::vec3(0.0f));
        terrainGO->SetName("Terrain");
        _terrain = static_cast<StreamingTerrainComponent*>(
            terrainGO->AddComponent<StreamingTerrainComponent>(StreamingTerrainProps{
                .worldSize = 10240.0f,
                .tileSize = 512.0f,
                // Balanced against the noise frequency below: perceived
                // steepness is heightScale * frequency, so halving the
                // wavelength (0.00035 -> 0.0007) must roughly halve the
                // amplitude or every slope doubles (and the tree-scatter
                // slope rules stop matching).
                .heightScale = 500.0f,
                .tileHeightRes = 512,// 1 m/texel in the LOD0 ring
                .tileMeshRes = 64,
                .lodCount = 4,
                .lod0RadiusTiles = 2,
                .paletteIndex = 3,// forest
                // ~1.4km feature wavelength: enough distinct ridges/valleys
                // across 10km that traversal reads as covering ground.
                .noise = { .resolution = 0, .seed = 20260705, .frequency = 0.0007f, .octaves = 9 },
        // Baked-tile cache: first run generates + stores, every run
        // after boots from pure IO (watch "cache" in the stats line).
        // Resolved against the engine base path (SDL_GetBasePath — next
        // to the executable) so it doesn't depend on the working dir.
        // Emscripten's default FS is RAM-backed, so skip it there and
        // keep generating (ship a preloaded pyramid instead).
#if !defined(__EMSCRIPTEN__)
                .cacheDir = FileSystem::Get().BasePath() + "cache/terrain",
#endif
                // .layers / .splatFn: plug Gaea-style splat + detail textures here.
                .colliderRadiusTiles = 1,
                // Deterministic scatter: slope/height rules decide trees vs
                // rocks; the streamer spawns/pools them as the ring moves.
                .placeEntitiesFn =
                    [](const TerrainTileContext& ctx) {
                        std::vector<TerrainEntityPlacement> out;
                        uint32_t rng =
                            static_cast<uint32_t>(ctx.coord.x * 73856093 ^ ctx.coord.y * 19349663 ^ ctx.seed);
                        auto rand01 = [&rng] {
                            rng = rng * 1664525u + 1013904223u;
                            return static_cast<float>(rng >> 8) * (1.0f / 16777216.0f);
                        };
                        for (int i = 0; i < 90; ++i) {
                            const float wx = ctx.worldMin.x + rand01() * (ctx.worldMax.x - ctx.worldMin.x);
                            const float wz = ctx.worldMin.y + rand01() * (ctx.worldMax.y - ctx.worldMin.y);
                            const float y = ctx.HeightAt(wx, wz);
                            const float dhdx = (ctx.HeightAt(wx + 4.0f, wz) - ctx.HeightAt(wx - 4.0f, wz)) / 8.0f;
                            const float dhdz = (ctx.HeightAt(wx, wz + 4.0f) - ctx.HeightAt(wx, wz - 4.0f)) / 8.0f;
                            const float slope = std::sqrt(dhdx * dhdx + dhdz * dhdz);
                            const float hn = y / ctx.heightScale;
                            if (slope < 0.35f && hn > 0.25f && hn < 0.62f) {
                                const float s = 3.0f + rand01() * 5.0f;// tree
                                out.push_back({ { wx, y + 0.5f * s, wz }, rand01() * 6.2832f, s, 0 });
                            } else if (slope >= 0.35f && slope < 1.1f && rand01() < 0.35f) {
                                const float s = 1.0f + rand01() * 3.0f;// rock
                                out.push_back({ { wx, y + 0.2f * s, wz }, rand01() * 6.2832f, s, 1 });
                            }
                        }
                        return out;
                    },
                .spawnEntityFn =
                    [this](Application* app, const TerrainEntityPlacement& p) {
                        auto* go = app->CreateGameObject(glm::vec3(0.0f));
                        go->SetName(p.type == 0 ? "tree" : "rock");
                        go->AddComponent<MeshComponent>(p.type == 0 ? _treeMesh : _rockMesh);
                        return go;
                    },
                .entityRadiusTiles = 3,
                // Golden pampas grass in a streamed ring around the camera —
                // watch cells build in as you sprint (GoT-style wind sway).
                .grassDensity = 12.0f,
                .grassRadius = 80.0f,
                .grassBladeHeight = 1.5f,
                .grassWindStrength = 0.4f,
                .grassWindSpeed = 1.8f,
            })
        );

        const auto bootMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _bootTime).count();
        ConsoleSubsystem::Get()->Info(
            "TerrainStreaming: full 10.24km x 10.24km horizon ready in " + std::to_string(bootMs) + "ms"
        );

        // Spawn high enough for an establishing vista over the valley — at
        // ground+40 you only see the local mountainside.
        const float groundY = _terrain->GetHeight(0.0f, 0.0f);
        _camGO->SetPosition(glm::vec3(0.0f, groundY + 200.0f, 0.0f));

        // Unified fly camera. 12 m/s base ~ a sprinting character (crossing
        // the world takes ~14 min, BotW-like), X sprint = 600 m/s for
        // streaming stress tests. Added before the ground clamp so the clamp
        // ticks after the movement.
        _camGO->AddComponent<CameraController3D>(
            /*moveSpeed=*/12.0f, /*lookSpeed=*/1.5f, /*slowMultiplier=*/0.2f, /*fastMultiplier=*/50.0f
        );
        _groundClamp = static_cast<GroundClampComponent*>(_camGO->AddComponent<GroundClampComponent>(_terrain));

        ConsoleSubsystem::Get()->Info(
            "WASD move, arrows look, X sprint, Z slow, R/F up/down, G ground-clamp, T teleport, P palette, L LOD "
            "tint, I wireframe, ESC quit."
        );
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (!_cam || !_terrain) return;
        auto* input = InputSubsystem::Get();

        // Movement/look/sprint live in CameraController3D; the ground clamp in
        // GroundClampComponent. Only demo-specific keys remain here.
        TerrainStreamer& terrain = _terrain->Streamer();
        if (input->IsKeyPressed(Key::T)) {
            // Teleport 2km along the view direction, kept inside the world —
            // stepping past the edge leaves nothing but void in front of you.
            const float bound = 0.5f * terrain.Props().worldSize - terrain.Props().tileSize;
            glm::vec3 fwd = _cam->GetEyeDirection();
            fwd.y = 0.0f;
            fwd = glm::length(fwd) > 1e-3f ? glm::normalize(fwd) : glm::vec3(1, 0, 0);
            glm::vec3 pos = _camGO->GetPosition() + fwd * 2000.0f;
            pos.x = std::clamp(pos.x, -bound, bound);
            pos.z = std::clamp(pos.z, -bound, bound);
            pos.y = terrain.GetHeight(pos.x, pos.z) + 200.0f;
            _camGO->SetPosition(pos);
            ConsoleSubsystem::Get()->Info(
                "Teleported to (" + std::to_string(static_cast<int>(pos.x)) + ", "
                + std::to_string(static_cast<int>(pos.z)) + ")"
            );
        }
        if (input->IsKeyPressed(Key::G) && _groundClamp) _groundClamp->enabled = !_groundClamp->enabled;
        if (input->IsKeyPressed(Key::P)) {
            terrain.SetPalette(terrain.GetPalette() + 1);
            ConsoleSubsystem::Get()->Info("Terrain palette " + std::to_string(terrain.GetPalette()));
        }
        if (input->IsKeyPressed(Key::L)) {
            terrain.SetLodTintDebug(!terrain.GetLodTintDebug());
            ConsoleSubsystem::Get()->Info(
                terrain.GetLodTintDebug() ? "LOD tint ON — each color is a detail ring; tiles recolor as they refine"
                                          : "LOD tint OFF"
            );
        }
        if (input->IsKeyPressed(Key::I)) {
            _wireframe = !_wireframe;
            GraphicsSubsystem::Get()->renderer->EnableWireframe(_wireframe);
        }
        if (input->IsKeyPressed(Key::ESCAPE)) Quit();

        // Streaming is driven by StreamingTerrainComponent::OnTick now; the app
        // only reads stats.
        const auto& stats = _terrain->GetStats();
        if (!_reportedStreamed && stats.initialLoadDone) {
            _reportedStreamed = true;
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _bootTime)
                    .count();
            ConsoleSubsystem::Get()->Info(
                "TerrainStreaming: every tile at its target LOD after " + std::to_string(ms) + "ms"
            );
        }
        _statsTimer += dt;
        if (_statsTimer >= 5.0f) {
            _statsTimer = 0.0f;
            const glm::vec3 pos = _camGO->GetPosition();
            const float bound = 0.5f * terrain.Props().worldSize;
            const bool inside = std::abs(pos.x) <= bound && std::abs(pos.z) <= bound;
            ConsoleSubsystem::Get()->Info(
                "cam (" + std::to_string(static_cast<int>(pos.x)) + ", " + std::to_string(static_cast<int>(pos.z))
                + (inside ? ") " : ") OUTSIDE WORLD ") + "tiles " + std::to_string(stats.loadedTiles) + " visible "
                + std::to_string(stats.visibleTiles) + " pending " + std::to_string(stats.pendingJobs) + " entities "
                + std::to_string(stats.activeEntities) + " grass " + std::to_string(stats.grassCells) + "c/"
                + std::to_string(stats.grassBlades / 1000) + "k cache " + std::to_string(stats.cacheHits) + "/"
                + std::to_string(stats.cacheHits + stats.cacheMisses) + " heightmapMB "
                + std::to_string(stats.gpuHeightmapBytes / (1024 * 1024))
            );
        }
    }
};

int main(int argc, char* argv[]) {
    TerrainStreamingDemo game(
        {
            .windowTitle = "Atmospheric — Terrain Streaming (10km x 10km)",
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
