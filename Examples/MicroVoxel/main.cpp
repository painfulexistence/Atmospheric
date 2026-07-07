#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"
#include "Atmospheric/light_component.hpp"
#include "Atmospheric/voxel_volume_component.hpp"

// Micro voxel rendering demo: a raymarched 12.8m diorama of 5cm voxels
// (procedural terrain + caves + ore + floating crystals), depth-composited
// with the rasterized scene by MicroVoxelPass. Contrast with the VoxelWorld
// example, which greedy-meshes 1m macro voxels into triangles — here no
// triangles are generated at all; every pixel raymarches the volume with a
// two-level DDA (brick skip + per-voxel walk).
class MicroVoxelApp : public Application {
    using Application::Application;

    std::vector<VoxelVolumeComponent*> _carveTargets;// volumes the E key can dig into

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        // Several independent voxel volumes, each attached to its own
        // GameObject. Each grid is centred over its object in x/z and rises from
        // y=0; MicroVoxelPass draws one bounding box per volume, so they
        // depth-composite with each other. Different seeds give different
        // terrain — proof the renderer handles many independent volumes.
        struct { glm::vec3 pos; uint32_t seed; const char* name; } kVolumes[] = {
            { glm::vec3(0.0f, 0.0f, 0.0f), 1337u, "Volume.Center" },
            { glm::vec3(13.5f, 0.0f, -1.0f), 7u, "Volume.Right" },
            { glm::vec3(-13.5f, 0.0f, 2.0f), 99u, "Volume.Left" },
        };
        for (const auto& vd : kVolumes) {
            auto* volumeObj = CreateGameObject();
            volumeObj->SetName(vd.name);
            volumeObj->SetPosition(vd.pos);
            auto* vc = static_cast<VoxelVolumeComponent*>(volumeObj->AddComponent<VoxelVolumeComponent>(vd.seed));
            _carveTargets.push_back(vc);
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

        mainCamera->gameObject->SetPosition(glm::vec3(0.0f, 11.0f, 26.0f));
        mainCamera->Pitch(glm::radians(-16.0f));
        mainCamera->gameObject->AddComponent<CameraController3D>(/*moveSpeed=*/6.0f, /*lookSpeed=*/1.5f);

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
        ConsoleSubsystem::Get()->Info("Three raymarched 5cm-voxel volumes — no triangles. Hold E to dig into them.");
        ConsoleSubsystem::Get()->Info(
            "Debug: 0=final 1=albedo 2=normals 3=AO 4=shadow 5=GI 6=material | G/O/H/P/X toggle "
            "GI/AO/shadow/point light/reflections."
        );
    }

    void OnUpdate(float /*dt*/, float /*time*/) override {
        auto* input = InputSubsystem::Get();
        if (input->IsKeyPressed(Key::ESCAPE)) Quit();

        // Hold E to dig: raycast the camera's aim into the volumes and carve a
        // sphere of air at the first solid voxel. No remeshing — the pass just
        // re-uploads the edited volume, which is the whole point of the raymarch
        // model (a greedy-meshed world would have to rebuild its mesh here).
        if (input->IsKeyDown(Key::E) && !_carveTargets.empty()) {
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
