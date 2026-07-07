#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"
#include "components.hpp"

// Camera navigation is CameraController3D (engine-level); world streaming and
// rendering are VoxelWorldComponent. OnUpdate only handles ESC.
class VoxelWorldApp : public Application {
    using Application::Application;

    void OnInit() override {
        ComponentFactory::Register("VoxelWorld", [](GameObject* o, Deserializer& d) -> Component* {
            int seed = 42;
            d.Read("seed", seed);
            return new VoxelWorldComponent(o, seed);
        });
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {

        mainCamera->gameObject->SetPosition(glm::vec3(200.0f, 80.0f, 200.0f));
        mainCamera->Pitch(glm::radians(-20.0f));
        mainCamera->Yaw(glm::radians(-75.0f));
        mainCamera->gameObject->AddComponent<CameraController3D>(/*moveSpeed=*/20.0f, /*lookSpeed=*/1.5f);

        auto* worldObj = CreateGameObject();
        worldObj->AddComponent<VoxelWorldComponent>(/*seed=*/1337);

        Renderer* renderer = GraphicsSubsystem::Get()->renderer.get();
        if (auto* bloom = renderer->GetPass<BloomPass>()) {
            bloom->enabled = true;
            bloom->threshold = 0.6f;
            bloom->bloomStrength = 0.06f;
        }

        Log::Info("VoxelWorld loaded. WASD move, RF up/down, Arrow keys look, Z slow, ESC quit.");
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
    static VoxelWorldApp game(
        {
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
}
#else
int main(int argc, char* argv[]) {
    VoxelWorldApp game(
        {
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
#endif
