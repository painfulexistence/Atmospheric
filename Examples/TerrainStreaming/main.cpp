#include "Atmospheric.hpp"
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
// Controls: WASD move, arrows look, E sprint (x20), Z slow, R/F up/down,
//           G toggle ground-clamp, T teleport +2km (streaming stress test),
//           I wireframe (shows the LOD rings), ESC quit.
class TerrainStreamingDemo : public Application {
    using Application::Application;

    TerrainStreamer _terrain;
    CameraComponent* _cam = nullptr;
    GameObject* _camGO = nullptr;
    GameObject* _terrainRoot = nullptr;

    float _moveSpeed = 60.0f;
    bool _clampToGround = true;
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
        _cam->SetPerspective(
            50.0f, static_cast<float>(INIT_SCREEN_WIDTH) / static_cast<float>(INIT_SCREEN_HEIGHT), 0.5f, 30000.0f
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

        _terrainRoot = CreateGameObject(glm::vec3(0.0f));
        _terrainRoot->SetName("TerrainRoot");

        _terrain.Init(
            this,
            TerrainStreamerProps{
                .worldSize = 10240.0f,
                .tileSize = 512.0f,
                .heightScale = 900.0f,
                .tileHeightRes = 512,// 1 m/texel in the LOD0 ring
                .tileMeshRes = 64,
                .lodCount = 4,
                .lod0RadiusTiles = 2,
                .paletteIndex = 3,// forest
                .noise = { .resolution = 0, .seed = 20260705, .frequency = 0.00035f, .octaves = 9 },
                // .layers / .splatFn: plug Gaea-style splat + detail textures here.
                .colliderRadiusTiles = 1,
            },
            _terrainRoot
        );

        const auto bootMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _bootTime)
                .count();
        ConsoleSubsystem::Get()->Info(
            "TerrainStreaming: full 10.24km x 10.24km horizon ready in " + std::to_string(bootMs) + "ms"
        );

        const float groundY = _terrain.GetHeight(0.0f, 0.0f);
        _camGO->SetPosition(glm::vec3(0.0f, groundY + 40.0f, 0.0f));

        ConsoleSubsystem::Get()->Info(
            "WASD move, arrows look, E sprint, Z slow, R/F up/down, G ground-clamp, T teleport, I wireframe, ESC "
            "quit."
        );
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (!_cam) return;
        auto* input = InputSubsystem::Get();

        if (input->IsKeyDown(Key::UP)) _cam->Pitch(CAMERA_ANGULAR_OFFSET);
        if (input->IsKeyDown(Key::DOWN)) _cam->Pitch(-CAMERA_ANGULAR_OFFSET);
        if (input->IsKeyDown(Key::RIGHT)) _cam->Yaw(CAMERA_ANGULAR_OFFSET);
        if (input->IsKeyDown(Key::LEFT)) _cam->Yaw(-CAMERA_ANGULAR_OFFSET);

        float speed = _moveSpeed;
        if (input->IsKeyDown(Key::E)) speed *= 20.0f;// ~1.2 km/s streaming stress test
        if (input->IsKeyDown(Key::Z)) speed *= 0.1f;
        const float step = speed * dt;

        glm::vec3 pos = _camGO->GetPosition();
        const glm::vec3 fwd = _cam->GetEyeDirection();
        const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
        if (input->IsKeyDown(Key::W)) pos += fwd * step;
        if (input->IsKeyDown(Key::S)) pos -= fwd * step;
        if (input->IsKeyDown(Key::A)) pos -= right * step;
        if (input->IsKeyDown(Key::D)) pos += right * step;
        if (input->IsKeyDown(Key::R)) pos.y += step;
        if (input->IsKeyDown(Key::F)) pos.y -= step;

        if (input->IsKeyPressed(Key::T)) pos += glm::vec3(2000.0f, 0.0f, 0.0f);
        if (input->IsKeyPressed(Key::G)) _clampToGround = !_clampToGround;
        if (input->IsKeyPressed(Key::I)) {
            _wireframe = !_wireframe;
            GraphicsSubsystem::Get()->renderer->EnableWireframe(_wireframe);
        }
        if (input->IsKeyPressed(Key::ESCAPE)) Quit();

        if (_clampToGround) {
            const float ground = _terrain.GetHeight(pos.x, pos.z);
            pos.y = std::max(pos.y, ground + 2.0f);
        }
        _camGO->SetPosition(pos);

        const glm::mat4 viewProj = _cam->GetProjectionMatrix() * _cam->GetViewMatrix();
        _terrain.Update(pos, viewProj);

        const auto& stats = _terrain.GetStats();
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
            ConsoleSubsystem::Get()->Info(
                "tiles " + std::to_string(stats.loadedTiles) + " visible " + std::to_string(stats.visibleTiles)
                + " pending " + std::to_string(stats.pendingJobs) + " heightmapMB "
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
