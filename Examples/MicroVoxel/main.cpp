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
        ConsoleSubsystem::Get()->Info("The terrain block ahead is raymarched 5cm voxels — no triangles.");
        ConsoleSubsystem::Get()->Info(
            "Debug: 0=final 1=albedo 2=normals 3=AO 4=shadow 5=GI 6=material | G/O/H toggle GI/AO/shadow."
        );
    }

    void OnUpdate(float /*dt*/, float /*time*/) override {
        auto* input = InputSubsystem::Get();
        if (input->IsKeyPressed(Key::ESCAPE)) Quit();

        // Debug hotkeys: 0-6 view individual shading terms in isolation,
        // G/O/H toggle GI / AO / the sun shadow ray (see microvoxel.frag).
        Renderer* renderer = GraphicsSubsystem::Get()->renderer.get();
        auto* mv = renderer ? renderer->GetPass<MicroVoxelPass>() : nullptr;
        if (!mv) return;
        auto* console = ConsoleSubsystem::Get();
        auto setDebug = [&](int mode, const char* name) {
            mv->debugMode = mode;
            console->Info(std::string("MicroVoxel debug view: ") + name);
        };
        if (input->IsKeyPressed(Key::Num0)) setDebug(0, "final shading");
        if (input->IsKeyPressed(Key::Num1)) setDebug(1, "albedo");
        if (input->IsKeyPressed(Key::Num2)) setDebug(2, "normals");
        if (input->IsKeyPressed(Key::Num3)) setDebug(3, "ambient occlusion");
        if (input->IsKeyPressed(Key::Num4)) setDebug(4, "sun shadow");
        if (input->IsKeyPressed(Key::Num5)) setDebug(5, "GI buffer");
        if (input->IsKeyPressed(Key::Num6)) setDebug(6, "material index");
        if (input->IsKeyPressed(Key::G)) {
            mv->giStrength = (mv->giStrength > 0.0f) ? 0.0f : 1.0f;
            console->Info(mv->giStrength > 0.0f ? "MicroVoxel GI: on" : "MicroVoxel GI: off (flat ambient)");
        }
        if (input->IsKeyPressed(Key::O)) {
            mv->aoStrength = (mv->aoStrength > 0.0f) ? 0.0f : 0.7f;
            console->Info(mv->aoStrength > 0.0f ? "MicroVoxel AO: on" : "MicroVoxel AO: off");
        }
        if (input->IsKeyPressed(Key::H)) {
            mv->shadowEnabled = !mv->shadowEnabled;
            console->Info(mv->shadowEnabled ? "MicroVoxel sun shadow: on" : "MicroVoxel sun shadow: off");
        }
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
