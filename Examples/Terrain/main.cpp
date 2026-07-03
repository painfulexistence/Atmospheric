#include "Atmospheric.hpp"
#include "Atmospheric/rmlui_manager.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <array>
#include <functional>
#include <memory>

// Minimal RmlUi event listener that forwards to a std::function. Instances are
// owned by the app (must outlive the elements they're attached to).
class RmlEventCallback : public Rml::EventListener {
public:
    explicit RmlEventCallback(std::function<void()> cb) : _cb(std::move(cb)) {}
    void ProcessEvent(Rml::Event& /*event*/) override { if (_cb) _cb(); }
private:
    std::function<void()> _cb;
};

class TerrainDemo : public Application {
    using Application::Application;

    CameraComponent* _cam = nullptr;
    GameObject*      _camGO = nullptr;
    float _moveSpeed   = 20.0f;
    float _slowSpeed   =  4.0f;
    bool  _slow        = false;
    bool  _wireframe   = false;

    GameObject* _proceduralTerrain = nullptr;
    GameObject* _heightmapTerrain  = nullptr;
    bool        _showProcedural    = true;
    int         _paletteIndex      = 0;  // 0-5; 0 = default warm pink/gold

    static constexpr int PALETTE_COUNT = 6;
    static constexpr std::array<const char*, PALETTE_COUNT> PALETTE_NAMES = {
        "1 - Warm Pink/Gold", "2 - Cool Blue/Purple", "3 - Earthy Green",
        "4 - Forest",         "5 - Soft Cool",        "6 - Vivid Mint/Coral"
    };

    // HUD (RmlUi native <select> controls)
    Rml::ElementDocument*          _hud        = nullptr;
    Rml::ElementFormControlSelect* _selMode    = nullptr;
    Rml::ElementFormControlSelect* _selPalette = nullptr;
    // Guards SetSelection() so syncing the UI from code doesn't re-enter the
    // change handler (which would call Apply* again).
    bool _syncing = false;
    std::vector<std::unique_ptr<RmlEventCallback>> _listeners;

    void OnInit() override {
        GoScene("main", [this]{ OnLoad(); });
    }

    // ── State application (keeps terrain + HUD in sync) ──────────────────────
    void ApplyProcedural(bool procedural) {
        _showProcedural = procedural;
        if (_proceduralTerrain) _proceduralTerrain->SetActive(procedural);
        if (_heightmapTerrain)  _heightmapTerrain->SetActive(!procedural);
        if (_selMode) { _syncing = true; _selMode->SetSelection(procedural ? 0 : 1); _syncing = false; }
        console.Info(procedural ? "Switched to procedural noise terrain"
                                : "Switched to heightmap terrain");
    }

    void ApplyPalette(int index) {
        _paletteIndex = ((index % PALETTE_COUNT) + PALETTE_COUNT) % PALETTE_COUNT;
        for (auto* go : { _proceduralTerrain, _heightmapTerrain }) {
            if (!go) continue;
            if (auto* tm = go->GetComponent<TerrainMeshComponent>())
                if (auto* mat = tm->GetTerrainMaterial())
                    mat->paletteIndex = _paletteIndex;
        }
        if (_selPalette) { _syncing = true; _selPalette->SetSelection(_paletteIndex); _syncing = false; }
        console.Info(std::string("Terrain palette ") + PALETTE_NAMES[_paletteIndex]);
    }

    GameObject* CreateTerrain(const std::string& name, const std::shared_ptr<HeightField>& hf) {
        const float worldSize   = 1024.0f;
        const float heightScale = 64.0f;

        auto* terrain = CreateGameObject(glm::vec3(0.0f, -10.0f, 0.0f));
        terrain->SetName(name);
        terrain->AddComponent<TerrainMeshComponent>(
            GetGraphicsSubsystem(), hf,
            TerrainMeshProps{
                .worldSize          = worldSize,
                .resolution         = 256,
                .heightScale        = heightScale,
                .tessellationFactor = 16.0f,
            }
        );
        terrain->AddComponent<HeightFieldColliderComponent>(
            hf,
            HeightFieldColliderProps{
                .worldSize   = worldSize,
                .heightScale = heightScale,
                .minHeight   = -64.0f,
                .maxHeight   =  64.0f,
            }
        );
        return terrain;
    }

    // Attach an event listener to an element; the listener is owned by _listeners.
    void AddListener(Rml::Element* el, const char* event, std::function<void()> cb) {
        if (!el) return;
        auto listener = std::make_unique<RmlEventCallback>(std::move(cb));
        el->AddEventListener(event, listener.get());
        _listeners.push_back(std::move(listener));
    }

    void SetupHUD() {
        _hud = RmlUiManager::Get()->LoadDocument("assets/ui/terrain_hud.rml");
        if (!_hud) {
            console.Warn("Terrain HUD failed to load; keyboard controls still work.");
            return;
        }
        _hud->Show();

        _selMode    = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(_hud->GetElementById("mode_select"));
        _selPalette = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(_hud->GetElementById("palette_select"));

        // Native <select> fires "change" on user selection; apply it to the
        // terrain. The _syncing guard skips changes we triggered from code.
        AddListener(_selMode, "change", [this]{
            if (!_syncing && _selMode) ApplyProcedural(_selMode->GetSelection() == 0);
        });
        AddListener(_selPalette, "change", [this]{
            if (!_syncing && _selPalette) ApplyPalette(_selPalette->GetSelection());
        });
    }

    void OnLoad() override {
        _cam   = mainCamera;
        _camGO = _cam->gameObject;
        _camGO->SetPosition(glm::vec3(0.0f, 64.0f, 0.0f));

        // Procedural Noise HeightField
        _proceduralTerrain = CreateTerrain("ProceduralTerrain", std::make_shared<NoiseHeightField>(NoiseHeightFieldParams{
            .resolution = 256,
            .seed       = 42,
            .frequency  = 0.02f,
            .octaves    = 8,
            .lacunarity = 2.0f,
            .gain       = 0.5f,
        }));

        // Image-based HeightField. 16-bit sources (16-bit PNG, .r16/.raw,
        // .r32) keep full precision end-to-end; 8-bit images also work.
        _heightmapTerrain = CreateTerrain("HeightmapTerrain",
            std::make_shared<ImageHeightField>("assets/textures/test_heightmap.r16"));

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
        //     GetGraphicsSubsystem(), hf,
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

        console.Info("Terrain loaded. WASD move, Arrow keys look, Z slow, SPACE/LMB switch terrain, P palette, I wireframe, ESC quit.");
    }

    void OnUpdate(float dt, float /*time*/) override {
        _slow = input.IsKeyDown(Key::Z);

        if (input.IsKeyDown(Key::UP))    _cam->Pitch( CAMERA_ANGULAR_OFFSET);
        if (input.IsKeyDown(Key::DOWN))  _cam->Pitch(-CAMERA_ANGULAR_OFFSET);
        if (input.IsKeyDown(Key::RIGHT)) _cam->Yaw(   CAMERA_ANGULAR_OFFSET);
        if (input.IsKeyDown(Key::LEFT))  _cam->Yaw(  -CAMERA_ANGULAR_OFFSET);

        const float speed  = (_slow ? _slowSpeed : _moveSpeed) * dt;
        glm::vec3   pos    = _camGO->GetPosition();
        glm::vec3   fwd    = _cam->GetEyeDirection();

        if (input.IsKeyDown(Key::W)) pos += fwd * speed;
        if (input.IsKeyDown(Key::S)) pos -= fwd * speed;
        if (input.IsKeyDown(Key::A)) pos -= glm::normalize(glm::cross(fwd, glm::vec3(0,1,0))) * speed;
        if (input.IsKeyDown(Key::D)) pos += glm::normalize(glm::cross(fwd, glm::vec3(0,1,0))) * speed;
        if (input.IsKeyDown(Key::R)) pos.y += speed;
        if (input.IsKeyDown(Key::F)) pos.y -= speed;
        _camGO->SetPosition(pos);

        // LMB switches terrain only when not clicking the HUD (the HUD handles
        // its own clicks via RmlUi event listeners).
        bool worldClick = input.IsMouseButtonPressed() && !input.IsMouseOverUI();
        if (input.IsKeyPressed(Key::SPACE) || worldClick) {
            ApplyProcedural(!_showProcedural);
        }
        if (input.IsKeyPressed(Key::P)) {
            ApplyPalette(_paletteIndex + 1);
        }
        if (input.IsKeyPressed(Key::I)) {
            _wireframe = !_wireframe;
            GetGraphicsSubsystem()->renderer->EnableWireframe(_wireframe);
        }
        if (input.IsKeyPressed(Key::ESCAPE)) Quit();
    }
};

int main(int argc, char* argv[]) {
    TerrainDemo game({
        .useDefaultTextures = true,
        .useDefaultShaders  = true,
    });
    game.Run();
    return 0;
}
