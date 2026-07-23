#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"
#include "Atmospheric/light_component.hpp"
#include "Atmospheric/voxel_volume_component.hpp"
#include "Atmospheric/window.hpp"
#include <fmt/format.h>
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
// SDL_main.h renames main() to SDL_main so SDLActivity/UIKit can invoke it.
#include "Atmospheric/touch_controls_component.hpp"
#include <SDL3/SDL_main.h>
#endif

// Micro voxel rendering demo: a raymarched 12.8m terrain of 5cm voxels
// (procedural terrain + caves + ore + floating crystals) plus ~20 small
// per-object volumes (crates / boulders / crystal clusters, several rotated),
// depth-composited with the rasterized scene by MicroVoxelPass. Contrast with
// the VoxelWorld example, which greedy-meshes 1m macro voxels into triangles —
// here no triangles are generated at all; every pixel raymarches a volume with
// a two-level DDA (brick skip + per-voxel walk) in that volume's local space.
class MicroVoxelApp : public Application {
    using Application::Application;

    std::vector<VoxelVolumeComponent*> _carveTargets;// volumes the E key / DIG button can dig into
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    TouchControlsComponent* _touchControls = nullptr;
#endif

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        // One big terrain volume plus a scatter of small per-object volumes
        // (crates / boulders / crystal clusters, 16-48 voxels per edge — a few
        // dozen KB each instead of terrain's megabytes). This is the
        // right-sizing layout the physics stage builds on: every object has its
        // own grid, its own transform (several are rotated to exercise the OBB
        // raymarch), and MicroVoxelPass frustum-culls and draws each tight box
        // depth-composited.
        auto* terrainObj = CreateGameObject();
        terrainObj->SetName("Volume.Terrain");
        auto* terrain = static_cast<VoxelVolumeComponent*>(terrainObj->AddComponent<VoxelVolumeComponent>(1337u));
        terrainObj->SetPosition(glm::vec3(0.0f));
        _carveTargets.push_back(terrain);

        // Drop a world-space point onto the terrain surface (the terrain is
        // generated in AddComponent above, so its raycast is ready).
        auto surfaceY = [terrain](float x, float z) -> float {
            glm::vec3 hit;
            if (terrain->RaycastVoxel(glm::vec3(x, 40.0f, z), glm::vec3(0.0f, -1.0f, 0.0f), 80.0f, hit)) {
                return hit.y;
            }
            return 0.0f;
        };

        // kind, gridDim, world x/z, yaw (deg), tilt (deg, exercises full OBB)
        struct ObjDef {
            VoxelVolumeKind kind;
            int gridDim;
            float x, z;
            float yawDeg;
            float tiltDeg;
        };
        const float s = terrain->WorldExtent() / 12.8f;// keep layout sane on the 9.6m web grid
        const ObjDef kObjects[] = {
            { VoxelVolumeKind::Crate, 32, -4.2f * s, -3.6f * s, 15.0f, 0.0f },
            { VoxelVolumeKind::Crate, 32, -3.4f * s, -4.1f * s, 40.0f, 0.0f },
            { VoxelVolumeKind::Crate, 24, -3.8f * s, -3.0f * s, 70.0f, 8.0f },
            { VoxelVolumeKind::Crate, 24, 2.6f * s, -4.0f * s, 25.0f, 0.0f },
            { VoxelVolumeKind::Crate, 32, 3.3f * s, -3.3f * s, 55.0f, 0.0f },
            { VoxelVolumeKind::Crate, 24, 4.1f * s, 2.8f * s, 10.0f, -6.0f },
            { VoxelVolumeKind::Crate, 32, -0.6f * s, 3.9f * s, 80.0f, 0.0f },
            { VoxelVolumeKind::Crate, 24, 0.4f * s, 4.4f * s, 35.0f, 0.0f },
            { VoxelVolumeKind::Boulder, 48, -1.8f * s, -2.2f * s, 0.0f, 0.0f },
            { VoxelVolumeKind::Boulder, 32, 1.5f * s, -1.9f * s, 30.0f, 0.0f },
            { VoxelVolumeKind::Boulder, 32, 2.2f * s, 1.4f * s, 60.0f, 12.0f },
            { VoxelVolumeKind::Boulder, 24, -2.6f * s, 1.8f * s, 90.0f, 0.0f },
            { VoxelVolumeKind::Boulder, 48, 4.4f * s, -0.8f * s, 45.0f, 0.0f },
            { VoxelVolumeKind::Boulder, 24, -4.5f * s, 0.6f * s, 20.0f, 0.0f },
            { VoxelVolumeKind::CrystalCluster, 24, -1.0f * s, -4.4f * s, 0.0f, 0.0f },
            { VoxelVolumeKind::CrystalCluster, 16, 0.8f * s, -3.1f * s, 45.0f, 0.0f },
            { VoxelVolumeKind::CrystalCluster, 24, 3.9f * s, 4.1f * s, 15.0f, 0.0f },
            { VoxelVolumeKind::CrystalCluster, 16, -3.2f * s, 4.3f * s, 75.0f, 0.0f },
            { VoxelVolumeKind::CrystalCluster, 24, 1.9f * s, 3.2f * s, 30.0f, -9.0f },
            { VoxelVolumeKind::CrystalCluster, 16, -2.1f * s, -0.9f * s, 60.0f, 0.0f },
        };
        int objIndex = 0;
        for (const auto& od : kObjects) {
            auto* obj = CreateGameObject();
            obj->SetName(fmt::format("Volume.Obj{}", objIndex));
            // Position first (sunk 5cm so objects seat into the terrain), then
            // attach: Generate runs on attach and the volume follows the
            // object's transform every frame after.
            obj->SetPosition(glm::vec3(od.x, surfaceY(od.x, od.z) - 0.05f, od.z));
            obj->SetRotation(glm::vec3(glm::radians(od.tiltDeg), glm::radians(od.yawDeg), 0.0f));
            auto* vc = static_cast<VoxelVolumeComponent*>(
                obj->AddComponent<VoxelVolumeComponent>(1000u + static_cast<uint32_t>(objIndex) * 17u, od.gridDim, od.kind)
            );
            _carveTargets.push_back(vc);
            objIndex++;
        }

        // An angled warm sun. Without one the engine falls back to its default
        // directional light, which points straight down (0,-1,0) — that lights
        // the terrain flat from overhead with no raking shadows. A low, angled
        // sun gives long shadows and side-lit relief, and shows up as "Sun" in
        // the scene tree. MicroVoxelPass reads its direction, diffuse, and
        // intensity, and casts a raymarched shadow against it.
        auto* sunGO = CreateGameObject(glm::vec3(0.0f));
        sunGO->SetName("Sun");
        sunGO->AddComponent(new LightComponent(
            sunGO,
            LightProps{
                .type = LightType::Directional,
                .ambient = glm::vec3(1.0f),
                .diffuse = glm::vec3(1.0f, 0.95f, 0.85f),// warm daylight
                .specular = glm::vec3(1.0f),
                // Low, near-horizon sun (tuned in the editor) for long raking
                // shadows and grazing light across the terrain.
                .direction = glm::normalize(glm::vec3(-0.451f, 10.179f, -236.350f)),
                .intensity = 20.0f,
                .castShadow = false,
            }
        ));

        // Initial camera at (1, 10, 15), pitched down 16°. From the default -z
        // forward, yaw left 90° (toward -x) so it looks across the diorama.
        mainCamera->gameObject->SetPosition(glm::vec3(1.0f, 10.0f, 15.0f));
        mainCamera->Yaw(glm::radians(-90.0f));
        mainCamera->Pitch(glm::radians(-16.0f));
        mainCamera->gameObject->AddComponent<CameraController3D>(/*moveSpeed=*/6.0f, /*lookSpeed=*/1.5f);
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        // Touch overlay: floating joystick (move), drag (look), DIG button.
        _touchControls = static_cast<TouchControlsComponent*>(
            mainCamera->gameObject->AddComponent<TouchControlsComponent>(/*moveSpeed=*/6.0f, /*lookSpeed=*/2.6f)
        );
#endif

        // A warm local point light pooling over the terrain, to show off local
        // illumination alongside the sun. Toggle with P. (The scene also has
        // emissive glowstone orbs whose glow the GI bounce spreads onto nearby
        // stone — that effect is on by default with GI.)
        if (Renderer* renderer = GraphicsSubsystem::Get()->renderer.get()) {
            if (auto* mv = renderer->GetPass<MicroVoxelPass>()) {
                mv->pointLightCount = 1;
                mv->pointLightPos[0] = glm::vec3(2.5f, 3.4f, 2.5f);
                mv->pointLightColor[0] = glm::vec3(1.0f, 0.55f, 0.22f);// warm orange
                mv->pointLightIntensity[0] = 7.0f;
                mv->pointLightRadius[0] = 7.5f;
            }
            // Bloom, so the emissive glowstones, crystal reflections, and
            // sun-lit highlights bleed a soft glow. Threshold/strength are
            // tunable live in the ImGui panel; raise threshold if the bright
            // sun (intensity 20) blooms too much of the terrain.
            if (auto* bloom = renderer->GetPass<BloomPass>()) {
                bloom->enabled = true;
                bloom->threshold = 1.0f;
                bloom->bloomStrength = 0.08f;
            }
        }

        ConsoleSubsystem::Get()->Info("MicroVoxel loaded. WASD move, RF up/down, Arrow keys look, Z slow, ESC quit.");
        ConsoleSubsystem::Get()->Info(
            fmt::format(
                "{} raymarched 5cm-voxel volumes (1 terrain + {} objects, several rotated) — no triangles. "
                "Hold E to dig into any of them.",
                _carveTargets.size(),
                _carveTargets.size() - 1
            )
        );
        ConsoleSubsystem::Get()->Info(
            "Debug: 0=final 1=albedo 2=normals 3=AO 4=shadow 5=GI 6=material | G/O/H/P/X/N/V toggle "
            "GI/AO/shadow/point light/reflections/denoiser/cross-volume | B = split raw|denoised."
        );
    }

    void OnUpdate(float /*dt*/, float /*time*/) override {
        auto* input = InputSubsystem::Get();
        if (input->IsKeyPressed(Key::ESCAPE)) Quit();

        // Hold E (or the touch DIG button) to dig: raycast the camera's aim into
        // the volumes and carve a sphere of air at the first solid voxel. No
        // remeshing — the pass just re-uploads the edited volume, which is the
        // whole point of the raymarch model (a greedy-meshed world would have to
        // rebuild its mesh here). Aim is the screen-center crosshair, so the
        // same code serves keyboard and touch.
        bool digHeld = input->IsKeyDown(Key::E);
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
        digHeld = digHeld || (_touchControls && _touchControls->IsActionHeld());
#endif
        if (digHeld && !_carveTargets.empty()) {
            const glm::vec3 ro = mainCamera->GetEyePosition();
            const glm::vec3 rd = mainCamera->GetEyeDirection();
            float best = 1e9f;
            VoxelVolumeComponent* target = nullptr;
            glm::vec3 hitPos(0.0f);
            for (auto* vc : _carveTargets) {
                glm::vec3 hit;
                if (vc->RaycastVoxel(ro, rd, 60.0f, hit)) {
                    const float d = glm::length(hit - ro);
                    if (d < best) {
                        best = d;
                        target = vc;
                        hitPos = hit;
                    }
                }
            }
            if (target) target->CarveSphere(hitPos, 0.45f);
        }

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
        if (input->IsKeyPressed(Key::P)) {
            mv->pointLightCount = (mv->pointLightCount > 0) ? 0 : 1;
            console->Info(mv->pointLightCount > 0 ? "MicroVoxel point light: on" : "MicroVoxel point light: off");
        }
        if (input->IsKeyPressed(Key::X)) {
            mv->reflectionsEnabled = !mv->reflectionsEnabled;
            console->Info(mv->reflectionsEnabled ? "MicroVoxel reflections: on" : "MicroVoxel reflections: off");
        }
        if (input->IsKeyPressed(Key::N)) {
            mv->giAtrousIterations = (mv->giAtrousIterations > 0) ? 0 : 3;
            console->Info(
                mv->giAtrousIterations > 0 ? "MicroVoxel GI denoiser: on (a-trous)"
                                           : "MicroVoxel GI denoiser: off (raw temporal)"
            );
        }
        if (input->IsKeyPressed(Key::B)) {
            mv->giSplitCompare = (mv->giSplitCompare >= 0.0f) ? -1.0f : 0.5f;
            console->Info(
                mv->giSplitCompare >= 0.0f ? "MicroVoxel GI split: on (left raw | right denoised), drag to move"
                                           : "MicroVoxel GI split: off"
            );
        }
        if (input->IsKeyPressed(Key::V)) {
            mv->giCrossVolume = !mv->giCrossVolume;
            console->Info(
                mv->giCrossVolume ? "MicroVoxel GI: cross-volume (brute-force all volumes, light bleeds between them)"
                                  : "MicroVoxel GI: primary volume only"
            );
        }
        // Drag the split divider with the mouse while the compare is on.
        // GetMousePosition is in physical/framebuffer pixels (see InputSubsystem),
        // matching the shader's gl_FragCoord-based uv, so divide by the physical
        // width — dividing by the logical width lands 2x off on Retina.
        if (mv->giSplitCompare >= 0.0f && input->IsMouseButtonDown() && !input->IsMouseOverUI()) {
            const glm::vec2 m = input->GetMousePosition();
            const auto [pw, ph] = Window::Get()->GetPhysicalSize();
            (void)ph;
            if (pw > 0) mv->giSplitCompare = glm::clamp(m.x / static_cast<float>(pw), 0.0f, 1.0f);
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
