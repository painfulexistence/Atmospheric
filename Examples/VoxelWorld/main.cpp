#include "Atmospheric.hpp"
#include "Atmospheric/camera_component.hpp"

// ─── Local components ─────────────────────────────────────────────────────────

// World-axis fly camera. WASD move, R/F up/down, IJKL pitch/yaw.
class FlyCameraComponent : public Component {
    float _moveSpeed, _lookSpeed;
    CameraComponent* _camera = nullptr;
public:
    FlyCameraComponent(GameObject* go, float moveSpeed = 20.0f, float lookSpeed = 1.5f)
        : _moveSpeed(moveSpeed), _lookSpeed(lookSpeed) { gameObject = go; }
    std::string GetName() const override { return "FlyCameraComponent"; }
    void OnAttach() override { _camera = gameObject->GetComponent<CameraComponent>(); }
    void OnTick(float dt) override {
        auto* input = gameObject->GetApp()->GetInput();
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

// ─────────────────────────────────────────────────────────────────────────────
// Camera navigation is delegated to FlyCameraComponent above. OnUpdate only
// streams/renders the voxel world around the camera and handles ESC.
class VoxelWorldApp : public Application {
    using Application::Application;

    VoxelWorld _world;

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        LoadScene(SceneDef{});

        mainCamera->gameObject->SetPosition(glm::vec3(200.0f, 80.0f, 200.0f));
        mainCamera->gameObject->SetRotation(glm::vec3(glm::radians(-20.0f), 0.0f, 0.0f));

        // WASD move, RF up/down, IJKL look — handled by the component.
        mainCamera->gameObject->AddComponent<FlyCameraComponent>(/*moveSpeed=*/20.0f, /*lookSpeed=*/1.5f);

        _world.Init(this, /*seed=*/1337);

        Renderer* renderer = GetGraphicsServer()->renderer;
        if (auto* bloom = renderer->GetPass<BloomPass>()) {
            bloom->enabled       = true;
            bloom->threshold     = 0.6f;
            bloom->bloomStrength = 0.06f;
        }

        console.Info("VoxelWorld loaded. WASD move, RF up/down, IJKL look, ESC quit.");
    }

    void OnUpdate(float dt, float /*time*/) override {
        glm::vec3 pos = mainCamera->gameObject->GetPosition();

        _world.Update(dt, pos);

        glm::mat4 viewProj = mainCamera->GetProjectionMatrix() * mainCamera->GetViewMatrix();
        _world.SubmitRenderCommands(GetGraphicsServer()->renderer, viewProj, pos);

        if (input.IsKeyPressed(Key::ESCAPE)) {
            Quit();
        }
    }
};

#ifdef __EMSCRIPTEN__
static const std::vector<std::string> kAssets = {
    "assets/textures/default_diff.ktx2",
    "assets/textures/default_norm.ktx2",
    "assets/textures/default_ao.ktx2",
    "assets/textures/default_rough.ktx2",
    "assets/textures/default_metallic.ktx2",
};

static void StartGame();

int main(int argc, char* argv[]) {
    FileSystem::Get().Prefetch(kAssets, StartGame);
    return 0;
}

static void StartGame() {
    static VoxelWorldApp game({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
}
#else
int main(int argc, char* argv[]) {
    VoxelWorldApp game({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
}
#endif
