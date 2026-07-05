#include "Atmospheric.hpp"
#include "Atmospheric/camera_component.hpp"

// Micro voxel rendering demo: a raymarched 25.6m volume of 10cm voxels
// (procedural terrain + caves + ore + floating crystals), depth-composited
// with the rasterized scene by MicroVoxelPass. Contrast with the VoxelWorld
// example, which greedy-meshes 1m macro voxels into triangles — here no
// triangles are generated at all; every pixel raymarches the volume with a
// two-level DDA (brick skip + per-voxel walk).

// World-axis fly camera. WASD move, R/F up/down, IJKL pitch/yaw.
// (Local copy of the VoxelWorld example's FlyCameraComponent.)
class FlyCameraComponent : public Component {
    float _moveSpeed, _lookSpeed;
    CameraComponent* _camera = nullptr;

public:
    FlyCameraComponent(GameObject* go, float moveSpeed = 10.0f, float lookSpeed = 1.5f)
      : _moveSpeed(moveSpeed), _lookSpeed(lookSpeed) {
        gameObject = go;
    }
    std::string GetName() const override {
        return "FlyCameraComponent";
    }
    void OnAttach() override {
        _camera = gameObject->GetComponent<CameraComponent>();
    }
    void OnTick(float dt) override {
        auto* input = InputSubsystem::Get();
        if (!input) return;
        const float move = _moveSpeed * dt, look = _lookSpeed * dt;
        if (_camera) {
            if (input->IsKeyDown(Key::I)) _camera->Pitch(look);
            if (input->IsKeyDown(Key::K)) _camera->Pitch(-look);
            if (input->IsKeyDown(Key::J)) _camera->Yaw(-look);
            if (input->IsKeyDown(Key::L)) _camera->Yaw(look);
        }
        glm::vec3 pos = gameObject->GetPosition();
        if (input->IsKeyDown(Key::W)) pos.z -= move;
        if (input->IsKeyDown(Key::S)) pos.z += move;
        if (input->IsKeyDown(Key::A)) pos.x -= move;
        if (input->IsKeyDown(Key::D)) pos.x += move;
        if (input->IsKeyDown(Key::R)) pos.y += move;
        if (input->IsKeyDown(Key::F)) pos.y -= move;
        gameObject->SetPosition(pos);
    }
};

class MicroVoxelApp : public Application {
    using Application::Application;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        // The demo volume spans x,z in [-12.8, 12.8] and y in [0, 25.6].
        mainCamera->gameObject->SetPosition(glm::vec3(0.0f, 24.0f, 44.0f));
        mainCamera->Pitch(glm::radians(-16.0f));
        mainCamera->gameObject->AddComponent<FlyCameraComponent>(/*moveSpeed=*/12.0f, /*lookSpeed=*/1.5f);

        Renderer* renderer = GraphicsSubsystem::Get()->renderer.get();
        if (auto* microVoxel = renderer->GetPass<MicroVoxelPass>()) {
            microVoxel->GenerateDemoVolume(/*seed=*/1337u);
        }

        ConsoleSubsystem::Get()->Info("MicroVoxel loaded. WASD move, RF up/down, IJKL look, ESC quit.");
        ConsoleSubsystem::Get()->Info("The terrain block ahead is raymarched 10cm voxels — no triangles.");
    }

    void OnUpdate(float /*dt*/, float /*time*/) override {
        if (InputSubsystem::Get()->IsKeyPressed(Key::ESCAPE)) Quit();
    }
};

#ifdef __EMSCRIPTEN__
static const std::vector<std::string> kAssets = {
    "assets/textures/default_diff.ktx2",  "assets/textures/default_norm.ktx2",     "assets/textures/default_ao.ktx2",
    "assets/textures/default_rough.ktx2", "assets/textures/default_metallic.ktx2",
};

static void StartGame();

int main(int argc, char* argv[]) {
    FileSystem::Get().Prefetch(kAssets, StartGame);
    return 0;
}

static void StartGame() {
    static MicroVoxelApp game(
        {
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
}
#else
int main(int argc, char* argv[]) {
    MicroVoxelApp game(
        {
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
#endif
