#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"

// Micro voxel rendering demo: a raymarched 25.6m volume of 10cm voxels
// (procedural terrain + caves + ore + floating crystals), depth-composited
// with the rasterized scene by MicroVoxelPass. Contrast with the VoxelWorld
// example, which greedy-meshes 1m macro voxels into triangles — here no
// triangles are generated at all; every pixel raymarches the volume with a
// two-level DDA (brick skip + per-voxel walk).
class MicroVoxelApp : public Application {
    using Application::Application;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        // The demo volume spans x,z in [-12.8, 12.8] and y in [0, 25.6].
        mainCamera->gameObject->SetPosition(glm::vec3(0.0f, 24.0f, 44.0f));
        mainCamera->Pitch(glm::radians(-16.0f));
        mainCamera->gameObject->AddComponent<CameraController3D>(/*moveSpeed=*/12.0f, /*lookSpeed=*/1.5f);

        Renderer* renderer = GraphicsSubsystem::Get()->renderer.get();
        if (auto* microVoxel = renderer->GetPass<MicroVoxelPass>()) {
            microVoxel->GenerateDemoVolume(/*seed=*/1337u);
        }

        ConsoleSubsystem::Get()->Info("MicroVoxel loaded. WASD move, RF up/down, Arrow keys look, Z slow, ESC quit.");
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
