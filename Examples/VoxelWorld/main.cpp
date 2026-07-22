#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"
#include "Atmospheric/voxel_chunk_component.hpp"
#include "Atmospheric/voxel_world_component.hpp"
#include "Atmospheric/water_component.hpp"
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
// SDL_main.h renames main() to SDL_main so SDLActivity/UIKit can invoke it.
#include "Atmospheric/touch_controls_component.hpp"
#include <SDL3/SDL_main.h>
#endif

// Camera navigation is CameraController3D; world streaming and rendering are
// VoxelWorldComponent (both engine-level). Everything demo-specific — water
// plane, camera setup, sprint tuning, hotkeys — lives here.
class VoxelWorldApp : public Application {
    using Application::Application;

    static constexpr float kWaterLine = 32.0f;

    bool _wireframe = false;
    VoxelWorldComponent* _world = nullptr;
    GameObject* _waterGO = nullptr;
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    TouchControlsComponent* _touchControls = nullptr;
#endif

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {

        mainCamera->gameObject->SetPosition(glm::vec3(200.0f, 80.0f, 200.0f));
        mainCamera->Pitch(glm::radians(-20.0f));
        mainCamera->Yaw(glm::radians(-75.0f));
        // Sprint capped at 10x: VoxelWorld::LOAD_PER_FRAME meshes only 8 chunks
        // per tick, so anything much faster outruns streaming and leaves the
        // camera hovering over unloaded space.
        mainCamera->gameObject->AddComponent<CameraController3D>(
            /*moveSpeed=*/20.0f, /*lookSpeed=*/1.5f, /*slowMultiplier=*/0.2f, /*fastMultiplier=*/10.0f
        );
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        // Touch overlay: floating joystick (move), drag (look), DIG button.
        // Speed matches the keyboard controller's base speed — no sprint on
        // touch, which also keeps the camera behind chunk streaming.
        _touchControls = static_cast<TouchControlsComponent*>(
            mainCamera->gameObject->AddComponent<TouchControlsComponent>(/*moveSpeed=*/20.0f, /*lookSpeed=*/2.6f)
        );
#endif

        auto* worldObj = CreateGameObject();
        _world = static_cast<VoxelWorldComponent*>(worldObj->AddComponent<VoxelWorldComponent>(/*seed=*/1337));
        _world->World().paletteIndex = 2;// "Earthy Green" (see gpaletteNames)

        // Water plane — large enough to cover the full view range plus fog
        // horizon. Parented to the world object so it stays in the same slot
        // in the entity tree; slid with the camera every frame so it always
        // covers the visible area.
        const float waterExt = ((2 * VoxelWorld::VIEW_X + 1) * VoxelChunkComponent::SIZE) * 2.0f;
        _waterGO = CreateGameObject(glm::vec3(0.0f, kWaterLine + 0.05f, 0.0f));
        _waterGO->SetName("VoxelWater");
        _waterGO->parent = worldObj;
        _waterGO->AddComponent<WaterComponent>(WaterProps{
            .width = waterExt,
            .depth = waterExt,
            .subdivisions = 64,
        });

        Renderer* renderer = GraphicsSubsystem::Get()->renderer.get();
        if (auto* bloom = renderer->GetPass<BloomPass>()) {
            bloom->enabled = true;
            bloom->threshold = 0.6f;
            bloom->bloomStrength = 0.06f;
        }
        // Default lighting for the demo: corner AO (crisp contact) + GTAO (broad,
        // cheap screen-space) + VoxelGI (world-space bounce). SSGI is left off —
        // it's noisier and heavier; toggle it with K to compare.
        if (auto* vp = renderer->GetPass<VoxelChunkPass>()) {
            vp->aoEnabled = true;
            vp->giMode = VoxelChunkPass::GIMode::VoxelGI;
        }
        if (auto* ssp = renderer->GetPass<ScreenSpaceGIPass>()) {
            ssp->gtaoEnabled = true;
        }
        // Post-process look: CRT + vignette on by default.
        if (auto* pp = renderer->GetPass<PostProcessPass>()) {
            pp->crtEnabled = true;
            pp->vignetteEnabled = true;
        }

        APP_INFO(
            "VoxelWorld loaded. WASD move, RF up/down, Arrow keys look, Z slow, X sprint, P palette, I wireframe, O "
            "corner AO, G VoxelGI, K SSGI, ESC quit."
        );
        APP_INFO("Hold E to dig — greedy-meshed 1m voxels, so carving re-meshes the affected chunks.");
    }

    static constexpr int gpaletteCount = 6;
    static constexpr const char* gpaletteNames[gpaletteCount] = { "1 - Warm Pink/Gold",      "2 - Cool Blue/Purple",
                                                                  "3 - Earthy Green",        "4 - Forest",
                                                                  "5 - Soft Cool (default)", "6 - Vivid Mint/Coral" };

    void OnUpdate(float /*dt*/, float /*time*/) override {
        if (_waterGO && mainCamera) {
            glm::vec3 pos = mainCamera->gameObject->GetPosition();
            _waterGO->SetPosition(glm::vec3(pos.x, kWaterLine + 0.05f, pos.z));
        }
        if (_world && InputSubsystem::Get()->IsKeyPressed(Key::P)) {
            int& idx = _world->World().paletteIndex;
            idx = (idx + 1) % gpaletteCount;
            APP_INFO("Voxel palette {}", gpaletteNames[idx]);
        }
        if (InputSubsystem::Get()->IsKeyPressed(Key::I)) {
            _wireframe = !_wireframe;
            GraphicsSubsystem::Get()->renderer->EnableWireframe(_wireframe);
        }
        // Toggle the greedy mesher's baked corner AO (contact-edge darkening).
        // The mesh already carries it per vertex; this just scales its influence.
        if (InputSubsystem::Get()->IsKeyPressed(Key::O)) {
            if (auto* vp = GraphicsSubsystem::Get()->renderer->GetPass<VoxelChunkPass>()) {
                vp->aoEnabled = !vp->aoEnabled;
                APP_INFO("{}", vp->aoEnabled ? "Corner AO: on" : "Corner AO: off");
            }
        }
        // Cycle global illumination: Off -> VoxelGI -> SSGI -> Off. Also
        // selectable in the GI panel (Engine Subsystems > Graphics).
        // G toggles VoxelGI, K toggles SSGI — mutually exclusive (one GI mode at
        // a time), matching the GI panel combo. Both also live in the panel.
        Renderer* gr = GraphicsSubsystem::Get()->renderer.get();
        if (InputSubsystem::Get()->IsKeyPressed(Key::G)) {
            using GIMode = VoxelChunkPass::GIMode;
            auto* vp = gr->GetPass<VoxelChunkPass>();
            auto* ssgi = gr->GetPass<ScreenSpaceGIPass>();
            if (vp) {
                vp->giMode = (vp->giMode == GIMode::VoxelGI) ? GIMode::Off : GIMode::VoxelGI;
                if (vp->giMode == GIMode::VoxelGI && ssgi) ssgi->ssgiEnabled = false;
                APP_INFO("{}", vp->giMode == GIMode::VoxelGI ? "GI: VoxelGI (cone tracing)" : "GI: off");
            }
        }
        if (InputSubsystem::Get()->IsKeyPressed(Key::K)) {
            auto* ssgi = gr->GetPass<ScreenSpaceGIPass>();
            auto* vp = gr->GetPass<VoxelChunkPass>();
            if (ssgi) {
                ssgi->ssgiEnabled = !ssgi->ssgiEnabled;
                if (ssgi->ssgiEnabled && vp) vp->giMode = VoxelChunkPass::GIMode::Off;
                APP_INFO("{}", ssgi->ssgiEnabled ? "GI: SSGI (screen-space)" : "GI: off");
            }
        }
        // Hold E (or the touch DIG button) to dig; aim is the camera's facing,
        // so keyboard and touch share one code path.
        bool digHeld = InputSubsystem::Get()->IsKeyDown(Key::E);
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        digHeld = digHeld || (_touchControls && _touchControls->IsActionHeld());
#endif
        if (digHeld) {
            const glm::vec3 ro = mainCamera->GetEyePosition();
            const glm::vec3 rd = mainCamera->GetEyeDirection();
            glm::vec3 hit;
            if (_world->World().RaycastVoxel(ro, rd, 200.0f, hit)) {
                _world->World().CarveSphere(hit, 3.0f);// 3 m crater (1 m voxels)
            }
        }
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
