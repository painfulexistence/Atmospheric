#include "Atmospheric.hpp"
#include "components.hpp"

// Camera navigation is delegated to FlyCameraComponent (components.hpp). OnUpdate only
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
