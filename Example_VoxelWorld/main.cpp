#include "Atmospheric.hpp"

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

        const float moveSpeed = 20.0f;
        const float lookSpeed = 1.5f; // radians/sec

        // IJKL look — use CameraComponent's own angle state
        if (input.IsKeyDown(Key::I)) mainCamera->Pitch( lookSpeed * dt);
        if (input.IsKeyDown(Key::K)) mainCamera->Pitch(-lookSpeed * dt);
        if (input.IsKeyDown(Key::J)) mainCamera->Yaw(-lookSpeed * dt);
        if (input.IsKeyDown(Key::L)) mainCamera->Yaw( lookSpeed * dt);

        // WASD: world-axis movement (not linked to view direction)
        if (input.IsKeyDown(Key::W)) pos.z -= moveSpeed * dt;
        if (input.IsKeyDown(Key::S)) pos.z += moveSpeed * dt;
        if (input.IsKeyDown(Key::A)) pos.x -= moveSpeed * dt;
        if (input.IsKeyDown(Key::D)) pos.x += moveSpeed * dt;
        if (input.IsKeyDown(Key::R)) pos.y += moveSpeed * dt;
        if (input.IsKeyDown(Key::F)) pos.y -= moveSpeed * dt;

        mainCamera->gameObject->SetPosition(pos);

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
