#include "Atmospheric.hpp"

class TerrainDemo : public Application {
    using Application::Application;

    CameraComponent* _cam = nullptr;
    GameObject*      _camGO = nullptr;
    float _moveSpeed   = 20.0f;
    float _slowSpeed   =  4.0f;
    bool  _slow        = false;
    bool  _wireframe   = false;

    GameObject* _proceduralTerrain = nullptr;
    GameObject* _heightmapTerrain  = nullptr;
    bool        _showProcedural    = true;

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    GameObject* CreateTerrain(const std::string& name, const std::shared_ptr<HeightField>& hf) {
        const float worldSize   = 1024.0f;
        const float heightScale = 64.0f;

        auto* terrain = CreateGameObject(glm::vec3(0.0f, -10.0f, 0.0f));
        terrain->SetName(name);
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
        return terrain;
    }

    void OnLoad() override {
        _cam   = mainCamera;
        _camGO = _cam->gameObject;
        _camGO->SetPosition(glm::vec3(0.0f, 64.0f, 0.0f));

        // Procedural Noise HeightField
        _proceduralTerrain = CreateTerrain("ProceduralTerrain", std::make_shared<NoiseHeightField>(NoiseHeightFieldParams{
            .resolution = 256,
            .seed       = 42,
            .frequency  = 0.004f,
            .octaves    = 8,
            .lacunarity = 2.0f,
            .gain       = 0.5f,
        }));

        // Image-based HeightField. 16-bit sources (16-bit PNG, .r16/.raw,
        // .r32) keep full precision end-to-end; 8-bit images also work.
        _heightmapTerrain = CreateTerrain("HeightmapTerrain",
            std::make_shared<ImageHeightField>("assets/textures/test_heightmap_16bit.r16"));

        // ── Using WorldCreator / Gaea exports ────────────────────────────────
        // 1. Export the heightmap as 16-bit PNG or RAW (.r16/.r32), square.
        // 2. Set heightScale to the export's height range (in meters) and
        //    worldSize to its world extent.
        // 3. Optionally export color/normal/AO/splat maps and pass them via
        //    TerrainMeshProps:
        //
        // auto hf = std::make_shared<ImageHeightField>("assets/textures/gaea_height.r16");
        // auto* terrain = CreateGameObject(glm::vec3(0.0f, -10.0f, 0.0f));
        // terrain->AddComponent<TerrainMeshComponent>(
        //     GetGraphicsServer(), hf,
        //     TerrainMeshProps{
        //         .worldSize     = 2048.0f,
        //         .resolution    = 256,
        //         .heightScale   = 200.0f,
        //         .colorMapPath  = "assets/textures/gaea_texture.png",   // Gaea "Texture" node
        //         .normalMapPath = "assets/textures/gaea_normals.png",   // Gaea "Normals" node
        //         .aoMapPath     = "assets/textures/gaea_ao.png",
        //         .splatMapPath  = "assets/textures/gaea_splat.png",     // masks packed into RGBA
        //         .layers = {
        //             { .albedoPath = "assets/textures/grass.png", .tiling = 64.0f },
        //             { .albedoPath = "assets/textures/rock.png",
        //               .normalPath = "assets/textures/rock_n.png", .tiling = 48.0f },
        //             { .albedoPath = "assets/textures/snow.png",  .tiling = 64.0f },
        //         },
        //     }
        // );
        // terrain->AddComponent<HeightFieldColliderComponent>(hf,
        //     HeightFieldColliderProps{ .worldSize = 2048.0f, .heightScale = 200.0f,
        //                               .minHeight = -200.0f, .maxHeight = 200.0f,
        //                               .resolution = 256 /* decimated physics grid */ });

        _proceduralTerrain->SetActive(_showProcedural);
        _heightmapTerrain->SetActive(!_showProcedural);

        console.Info("Terrain loaded. WASD move, Arrow keys look, Z slow, SPACE/LMB switch terrain, T wireframe, ESC quit.");
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

        if (input.IsKeyPressed(Key::SPACE) || input.IsMouseButtonPressed()) {
            _showProcedural = !_showProcedural;
            _proceduralTerrain->SetActive(_showProcedural);
            _heightmapTerrain->SetActive(!_showProcedural);
            console.Info(_showProcedural ? "Switched to procedural noise terrain"
                                         : "Switched to heightmap terrain");
        }
        if (input.IsKeyPressed(Key::T)) {
            _wireframe = !_wireframe;
            GetGraphicsServer()->renderer->EnableWireframe(_wireframe);
        }
        if (input.IsKeyPressed(Key::ESCAPE)) Quit();
    }
};

int main(int argc, char* argv[]) {
    TerrainDemo game({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
}
