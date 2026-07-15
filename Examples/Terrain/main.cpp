#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"
#include "Atmospheric/portal_component.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <array>
#include <functional>
#include <memory>

class TerrainDemo : public Application {
    using Application::Application;

    CameraComponent* _cam = nullptr;
    GameObject* _camGO = nullptr;
    bool _wireframe = false;

    GameObject* _proceduralTerrain = nullptr;
    GameObject* _heightmapTerrain = nullptr;
    bool _showProcedural = true;
    int _paletteIndex = 0;// 0-5; 0 = default warm pink/gold

    static constexpr int gpaletteCount = 6;
    static constexpr std::array<const char*, gpaletteCount> gpaletteNames = {
        "1 - Warm Pink/Gold", "2 - Cool Blue/Purple", "3 - Earthy Green",
        "4 - Forest",         "5 - Soft Cool",        "6 - Vivid Mint/Coral"
    };

    // HUD (RmlUi native <select> controls), driven by an engine UIPageComponent.
    UIPageComponent* _hudPage = nullptr;
    Rml::ElementFormControlSelect* _selMode = nullptr;
    Rml::ElementFormControlSelect* _selPalette = nullptr;
    // Guards SetSelection() so syncing the UI from code doesn't re-enter the
    // change handler (which would call Apply* again).
    bool _syncing = false;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    // ── State application (keeps terrain + HUD in sync) ──────────────────────
    void ApplyProcedural(bool procedural) {
        _showProcedural = procedural;
        if (_proceduralTerrain) _proceduralTerrain->SetActive(procedural);
        if (_heightmapTerrain) _heightmapTerrain->SetActive(!procedural);
        if (_selMode) {
            _syncing = true;
            _selMode->SetSelection(procedural ? 0 : 1);
            _syncing = false;
        }
        ConsoleSubsystem::Get()->Info(
            procedural ? "Switched to procedural noise terrain" : "Switched to heightmap terrain"
        );
    }

    void ApplyPalette(int index) {
        _paletteIndex = ((index % gpaletteCount) + gpaletteCount) % gpaletteCount;
        for (auto* go : { _proceduralTerrain, _heightmapTerrain }) {
            if (!go) continue;
            if (auto* tm = go->GetComponent<TerrainMeshComponent>())
                if (auto* mat = tm->GetTerrainMaterial()) mat->paletteIndex = _paletteIndex;
        }
        if (_selPalette) {
            _syncing = true;
            _selPalette->SetSelection(_paletteIndex);
            _syncing = false;
        }
        ConsoleSubsystem::Get()->Info(std::string("Terrain palette ") + gpaletteNames[_paletteIndex]);
    }

    GameObject* CreateTerrain(const std::string& name, const std::shared_ptr<HeightField>& hf) {
        const float worldSize = 1024.0f;
        const float heightScale = 64.0f;

        auto* terrain = CreateGameObject(glm::vec3(0.0f, -10.0f, 0.0f));
        terrain->SetName(name);
        terrain->AddComponent<TerrainMeshComponent>(
            GraphicsSubsystem::Get(),
            hf,
            TerrainMeshProps{
                .worldSize = worldSize,
                .resolution = 256,
                .heightScale = heightScale,
                .tessellationFactor = 16.0f,
            }
        );
        terrain->AddComponent<HeightFieldColliderComponent>(
            hf,
            HeightFieldColliderProps{
                .worldSize = worldSize,
                .heightScale = heightScale,
                .minHeight = -64.0f,
                .maxHeight = 64.0f,
            }
        );
        return terrain;
    }

    // Two large linked portals flanking the initial camera (at (0, 64, 0)),
    // facing each other for the classic infinite-corridor recursion. Spaced a
    // little wider than eye-to-eye so the corridor reads clearly. Created
    // inactive so the demo carries no portal render budget until asked for: an
    // inactive GameObject is neither ticked nor submitted, so PortalPass finds
    // no PortalMaterial and returns before allocating any recursion RT. Toggle
    // both ends "Active" in the entity inspector to switch the corridor on
    // (both must be active — each end needs its partner's draw).
    void SetupPortals() {
        auto* blueGO = CreateGameObject(glm::vec3(-24.0f, 60.0f, 0.0f));
        blueGO->SetName("PortalBlue");
        blueGO->SetRotation(glm::vec3(0.0f, glm::radians(90.0f), 0.0f));// +Z -> +X, faces the camera
        auto* blue = static_cast<PortalComponent*>(blueGO->AddComponent<PortalComponent>(PortalProps{
            .radius = 8.0f,
            .rimColor = { 0.25f, 0.55f, 1.0f },
        }));

        auto* orangeGO = CreateGameObject(glm::vec3(24.0f, 60.0f, 0.0f));
        orangeGO->SetName("PortalOrange");
        orangeGO->SetRotation(glm::vec3(0.0f, glm::radians(-90.0f), 0.0f));// +Z -> -X, faces the camera
        auto* orange = static_cast<PortalComponent*>(orangeGO->AddComponent<PortalComponent>(PortalProps{
            .radius = 8.0f,
            .rimColor = { 1.0f, 0.55f, 0.15f },
        }));

        PortalComponent::Link(blue, orange);
        blueGO->SetActive(false);
        orangeGO->SetActive(false);
    }

    void SetupHUD() {
        // The engine's UIPageComponent loads the document, shows it, and owns
        // the event-listener adapters — no hand-rolled RmlUi glue here.
        _hudPage = static_cast<UIPageComponent*>(
            CreateGameObject()->AddComponent<UIPageComponent>("assets/ui/terrain_hud.rml")
        );
        if (!_hudPage->GetDocument()) {
            ConsoleSubsystem::Get()->Warn("Terrain HUD failed to load; keyboard controls still work.");
            return;
        }

#if defined(__EMSCRIPTEN__)
        // Wireframe (glPolygonMode) is unavailable on WebGL — hide its hint.
        // The I-key handler stays active but is a harmless no-op on the web.
        if (auto* hint = _hudPage->GetElement("hint_wireframe")) hint->SetProperty("display", "none");
#endif

        _selMode = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(_hudPage->GetElement("mode_select"));
        _selPalette = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(_hudPage->GetElement("palette_select"));

        // Native <select> fires "change" on user selection; apply it to the
        // terrain. The _syncing guard skips changes we triggered from code.
        _hudPage->AddListener(_selMode, "change", [this] {
            if (!_syncing && _selMode) ApplyProcedural(_selMode->GetSelection() == 0);
        });
        _hudPage->AddListener(_selPalette, "change", [this] {
            if (!_syncing && _selPalette) ApplyPalette(_selPalette->GetSelection());
        });
    }

    void OnLoad() override {
        _cam = mainCamera;
        _camGO = _cam->gameObject;
        _camGO->SetPosition(glm::vec3(0.0f, 64.0f, 0.0f));
        _camGO->AddComponent<CameraController3D>(/*moveSpeed=*/20.0f, /*lookSpeed=*/1.5f);

        SetupPortals();

        // Procedural Noise HeightField
        _proceduralTerrain = CreateTerrain(
            "ProceduralTerrain",
            std::make_shared<NoiseHeightField>(NoiseHeightFieldParams{
                .resolution = 256,
                .seed = 42,
                .frequency = 0.02f,
                .octaves = 8,
                .lacunarity = 2.0f,
                .gain = 0.5f,
            })
        );

        // Image-based HeightField. 16-bit sources (16-bit PNG, .r16/.raw,
        // .r32) keep full precision end-to-end; 8-bit images also work.
        _heightmapTerrain =
            CreateTerrain("HeightmapTerrain", std::make_shared<ImageHeightField>("assets/textures/test_heightmap.r16"));

        // ── Using WorldCreator / Gaea exports ────────────────────────────────
        // 1. Export the heightmap as 16-bit PNG or RAW (.r16/.r32), square.
        // 2. Set heightScale to the export's height range (in meters) and
        //    worldSize to its world extent.
        // 3. Optionally export color/normal/AO/splat maps and pass them via
        //    TerrainMeshProps:
        //
        // auto hf = std::make_shared<ImageHeightField>("assets/textures/gaea_height.r16");
        // auto* terrain = CreateGameObject(glm::vec3(0.0f, -10.0f, 0.0f));
        // terrain->AddComponent<TerrainMeshComponent>(
        //     GraphicsSubsystem::Get(), hf,
        //     TerrainMeshProps{
        //         .worldSize     = 2048.0f,
        //         .resolution    = 256,
        //         .heightScale   = 200.0f,
        //         .colorMapPath  = "assets/textures/gaea_texture.png",   // Gaea "Texture" node
        //         .normalMapPath = "assets/textures/gaea_normals.png",   // Gaea "Normals" node
        //         .aoMapPath     = "assets/textures/gaea_ao.png",
        //         .splatMapPath  = "assets/textures/gaea_splat.png",     // masks packed into RGBA
        //         .layers = {
        //             { .albedoPath = "assets/textures/grass.png", .tiling = 64.0f },
        //             { .albedoPath = "assets/textures/rock.png",
        //               .normalPath = "assets/textures/rock_n.png", .tiling = 48.0f },
        //             { .albedoPath = "assets/textures/snow.png",  .tiling = 64.0f },
        //         },
        //     }
        // );
        // terrain->AddComponent<HeightFieldColliderComponent>(hf,
        //     HeightFieldColliderProps{ .worldSize = 2048.0f, .heightScale = 200.0f,
        //                               .minHeight = -200.0f, .maxHeight = 200.0f,
        //                               .resolution = 256 /* decimated physics grid */ });

        SetupHUD();
        ApplyProcedural(_showProcedural);
        ApplyPalette(_paletteIndex);

        ConsoleSubsystem::Get()->Info(
            "Terrain loaded. WASD move, Arrow keys look, Z slow, SPACE/LMB switch terrain, P palette, I wireframe, "
            "ESC quit."
        );
    }

    void OnUpdate(float /*dt*/, float /*time*/) override {
        // Camera movement/look is handled by CameraController3D on _camGO.
        // LMB switches terrain only when not clicking the HUD (the HUD handles
        // its own clicks via RmlUi event listeners).
        bool worldClick = InputSubsystem::Get()->IsMouseButtonPressed() && !InputSubsystem::Get()->IsMouseOverUI();
        if (InputSubsystem::Get()->IsKeyPressed(Key::SPACE) || worldClick) {
            ApplyProcedural(!_showProcedural);
        }
        if (InputSubsystem::Get()->IsKeyPressed(Key::P)) {
            ApplyPalette(_paletteIndex + 1);
        }
        if (InputSubsystem::Get()->IsKeyPressed(Key::I)) {
            _wireframe = !_wireframe;
            GraphicsSubsystem::Get()->renderer->EnableWireframe(_wireframe);
        }
        if (InputSubsystem::Get()->IsKeyPressed(Key::ESCAPE)) Quit();
    }
};

int main(int argc, char* argv[]) {
    TerrainDemo game(
        {
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
