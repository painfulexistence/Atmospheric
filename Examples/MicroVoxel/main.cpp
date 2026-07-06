#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"

// Micro voxel rendering demo: a raymarched 12.8m diorama of 5cm voxels
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
        // The demo volume spans x,z in [-6.4, 6.4]; terrain tops out near y=5.6.
        mainCamera->gameObject->SetPosition(glm::vec3(0.0f, 9.0f, 20.0f));
        mainCamera->Pitch(glm::radians(-14.0f));
        mainCamera->gameObject->AddComponent<CameraController3D>(/*moveSpeed=*/6.0f, /*lookSpeed=*/1.5f);

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
