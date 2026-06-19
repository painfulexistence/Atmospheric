#include "Atmospheric.hpp"

class ProceduralTerrainDemo : public Application {
    using Application::Application;

    CameraComponent* _cam = nullptr;
    GameObject*      _camGO = nullptr;
    float _moveSpeed   = 20.0f;
    float _slowSpeed   =  4.0f;
    bool  _slow        = false;
    bool  _wireframe   = false;

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        LoadScene(SceneDef{});

        _cam   = mainCamera;
        _camGO = _cam->gameObject;
        _camGO->SetPosition(glm::vec3(0.0f, 64.0f, 0.0f));

        auto hf = std::make_shared<NoiseHeightField>(NoiseHeightFieldParams{
            .resolution = 256,
            .seed       = 42,
            .frequency  = 0.004f,
            .octaves    = 8,
            .lacunarity = 2.0f,
            .gain       = 0.5f,
        });

        const float worldSize  = 1024.0f;
        const float heightScale = 64.0f;

        auto* terrain = CreateGameObject(glm::vec3(0.0f, -10.0f, 0.0f));
        terrain->AddComponent<TerrainMeshComponent>(
            GetGraphicsServer(), hf,
            TerrainMeshProps{
                .worldSize          = worldSize,
                .resolution         = 256,
                .heightScale        = heightScale,
                .tessellationFactor = 16.0f,
            }
        );
        terrain->AddComponent<HeightFieldColliderComponent>(
            hf,
            HeightFieldColliderProps{
                .worldSize   = worldSize,
                .heightScale = heightScale,
                .minHeight   = -64.0f,
                .maxHeight   =  64.0f,
            }
        );

        console.Info("ProceduralTerrain loaded. WASD move, Arrow keys look, Z slow, SPACE wireframe, ESC quit.");
    }

    void OnUpdate(float dt, float /*time*/) override {
        _slow = input.IsKeyDown(Key::Z);

        if (input.IsKeyDown(Key::UP))    _cam->Pitch( CAMERA_ANGULAR_OFFSET);
        if (input.IsKeyDown(Key::DOWN))  _cam->Pitch(-CAMERA_ANGULAR_OFFSET);
        if (input.IsKeyDown(Key::RIGHT)) _cam->Yaw(   CAMERA_ANGULAR_OFFSET);
        if (input.IsKeyDown(Key::LEFT))  _cam->Yaw(  -CAMERA_ANGULAR_OFFSET);

        const float speed  = (_slow ? _slowSpeed : _moveSpeed) * dt;
        glm::vec3   pos    = _camGO->GetPosition();
        glm::vec3   fwd    = _cam->GetEyeDirection();

        if (input.IsKeyDown(Key::W)) pos += fwd * speed;
        if (input.IsKeyDown(Key::S)) pos -= fwd * speed;
        if (input.IsKeyDown(Key::A)) pos -= glm::normalize(glm::cross(fwd, glm::vec3(0,1,0))) * speed;
        if (input.IsKeyDown(Key::D)) pos += glm::normalize(glm::cross(fwd, glm::vec3(0,1,0))) * speed;
        if (input.IsKeyDown(Key::R)) pos.y += speed;
        if (input.IsKeyDown(Key::F)) pos.y -= speed;
        _camGO->SetPosition(pos);

        if (input.IsKeyPressed(Key::SPACE)) {
            _wireframe = !_wireframe;
            GetGraphicsServer()->renderer->EnableWireframe(_wireframe);
        }
        if (input.IsKeyPressed(Key::ESCAPE)) Quit();
    }
};

int main(int argc, char* argv[]) {
    ProceduralTerrainDemo game({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
}
