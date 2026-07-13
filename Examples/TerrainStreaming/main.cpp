#include "Atmospheric.hpp"
#include "Atmospheric/camera_controller_3d.hpp"
#include "Atmospheric/material.hpp"
#include "Atmospheric/mesh.hpp"
#include "Atmospheric/mesh_builder.hpp"
#include "Atmospheric/rmlui_manager.hpp"
#include "Atmospheric/terrain_texture_gen.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>

// Minimal RmlUi event listener that forwards to a std::function; owned by the
// app so it outlives the element it's attached to (see Terrain example).
class RmlEventCallback : public Rml::EventListener {
public:
    explicit RmlEventCallback(std::function<void()> cb) : _cb(std::move(cb)) {
    }
    void ProcessEvent(Rml::Event& /*event*/) override {
        if (_cb) _cb();
    }

private:
    std::function<void()> _cb;
};

// 10km x 10km streamed open-world terrain.
//
// The whole world is prewarmed at the coarsest LOD during load (full horizon
// on frame one, Ghost-of-Tsushima-style fast boot), then TerrainStreamer
// refines concentric detail rings around the camera on worker threads while
// you fly — no hitches, no pop-in holes, Bullet colliders following the
// camera tile.
//
// Controls: WASD move, arrows look, X sprint (x50), Z slow, R/F up/down,
//           G toggle ground-clamp, T teleport +2km (streaming stress test),
//           SPACE cycle surface mode (textured / palette / LOD debug),
//           P select palette (Palette mode only),
//           I jump to LOD debug (LOD colours + wireframe), ESC quit.
// A HUD panel exposes the same surface-mode and palette selectors.

// Keeps the fly camera above the streamed terrain. Ticks after the
// CameraController3D on the same GameObject (components tick in add order),
// so the clamp applies to this frame's movement.
class GroundClampComponent : public Component {
public:
    GroundClampComponent(GameObject* owner, StreamingTerrainComponent* terrain) : _terrain(terrain) {
        gameObject = owner;
    }
    std::string GetName() const override {
        return "GroundClamp";
    }
    void OnAttach() override {
    }
    void OnDetach() override {
    }
    void OnTick(float /*dt*/) override {
        if (!enabled) return;
        glm::vec3 pos = gameObject->GetPosition();
        pos.y = std::max(pos.y, _terrain->GetHeight(pos.x, pos.z) + 2.0f);
        gameObject->SetPosition(pos);
    }

    bool enabled = true;

private:
    StreamingTerrainComponent* _terrain;
};

class TerrainStreamingDemo : public Application {
    using Application::Application;

    // The terrain is a reusable engine component now: it owns the streamer and
    // drives it from its own OnTick, so the app just holds a handle for height
    // queries / debug toggles.
    StreamingTerrainComponent* _terrain = nullptr;
    CameraComponent* _cam = nullptr;
    GameObject* _camGO = nullptr;
    GroundClampComponent* _groundClamp = nullptr;
    MeshHandle _treeMesh;
    MeshHandle _rockMesh;

    // HUD (RmlUi native <select> controls), mirroring the Terrain example.
    Rml::ElementDocument* _hud = nullptr;
    Rml::ElementFormControlSelect* _selMode = nullptr;
    Rml::ElementFormControlSelect* _selPalette = nullptr;
    // Guards SetSelection() so syncing the UI from code doesn't re-enter the
    // change handler (which would call Apply* again).
    bool _syncing = false;
    std::vector<std::unique_ptr<RmlEventCallback>> _listeners;
    static constexpr int gpaletteCount = 6;

    float _statsTimer = 0.0f;
    std::chrono::steady_clock::time_point _bootTime;
    bool _reportedStreamed = false;

    void OnInit() override {
        _bootTime = std::chrono::steady_clock::now();
        GoScene("main", [this] { OnLoad(); });
    }

    // ── State application (keeps streamer + HUD in sync) ─────────────────────
    // The single source of truth is the streamer's TerrainColorMode; these
    // push a change onto it and mirror it into the <select>s (guarded so the
    // mirror doesn't re-fire the change handler). The palette select is only
    // meaningful in Palette mode, so it's disabled otherwise.
    void ApplyColorMode(TerrainColorMode mode) {
        _terrain->SetColorMode(mode);
        if (_selMode) {
            _syncing = true;
            _selMode->SetSelection(static_cast<int>(mode));
            _syncing = false;
        }
        // The palette selector only bites in Palette mode — disable it (dims via
        // the :disabled rcss and blocks interaction) otherwise.
        if (_selPalette) _selPalette->SetDisabled(mode != TerrainColorMode::Palette);
        // LOD tint is the debug view, so wireframe rides with it: entering LOD
        // tint (via I, SPACE, or the dropdown) turns wireframe on, leaving it
        // turns it off. No-op on WebGL where glPolygonMode is unavailable.
        GraphicsSubsystem::Get()->renderer->EnableWireframe(mode == TerrainColorMode::LodTint);
        const char* names[] = { "textured (detail layers)", "palette", "LOD debug" };
        ConsoleSubsystem::Get()->Info(std::string("Surface: ") + names[static_cast<int>(mode)]);
    }

    void ApplyPalette(int index) {
        const int p = ((index % gpaletteCount) + gpaletteCount) % gpaletteCount;
        _terrain->SetPalette(p);
        if (_selPalette) {
            _syncing = true;
            _selPalette->SetSelection(p);
            _syncing = false;
        }
        ConsoleSubsystem::Get()->Info("Palette " + std::to_string(p));
    }

    void AddListener(Rml::Element* el, const char* event, std::function<void()> cb) {
        if (!el) return;
        auto listener = std::make_unique<RmlEventCallback>(std::move(cb));
        el->AddEventListener(event, listener.get());
        _listeners.push_back(std::move(listener));
    }

    void SetupHUD() {
        _hud = RmlUiManager::Get()->LoadDocument("assets/ui/streaming_hud.rml");
        if (!_hud) {
            ConsoleSubsystem::Get()->Warn("Streaming HUD failed to load; keyboard controls still work.");
            return;
        }
        _hud->Show();
        _selMode = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(_hud->GetElementById("mode_select"));
        _selPalette = rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(_hud->GetElementById("palette_select"));

        // Native <select> fires "change" on user selection; the _syncing guard
        // skips selections we set from code.
        AddListener(_selMode, "change", [this] {
            if (!_syncing && _selMode) ApplyColorMode(static_cast<TerrainColorMode>(_selMode->GetSelection()));
        });
        AddListener(_selPalette, "change", [this] {
            if (!_syncing && _selPalette) ApplyPalette(_selPalette->GetSelection());
        });
    }

    void OnLoad() override {
        _cam = mainCamera;
        _camGO = _cam->gameObject;
        // Wide fov + far plane past the world diagonal (14.5km) so the whole
        // streamed horizon is in view. fov is in degrees.
        _cam->SetPerspective(
            55.0f, static_cast<float>(INIT_SCREEN_WIDTH) / static_cast<float>(INIT_SCREEN_HEIGHT), 0.5f, 30000.0f
        );

        // Sun
        auto* sunGO = CreateGameObject(glm::vec3(0.0f));
        sunGO->SetName("Sun");
        sunGO->AddComponent(new LightComponent(
            sunGO,
            LightProps{
                .type = LightType::Directional,
                .ambient = glm::vec3(1.0f),
                .diffuse = glm::vec3(67.0f, 250.0f, 202.0f) / 255.0f,// mint-cyan sunlight
                .specular = glm::vec3(1.0f),
                .direction = glm::normalize(glm::vec3(-0.4f, -1.0f, 0.35f)),
                .intensity = 1.0f,
                .castShadow = false,
            }
        ));

        // Entity prototype meshes: one mesh + material per type, drawn via
        // per-tile MeshInstancer clouds (see .entityMeshes below), so all
        // trees across the whole ring collapse into one instanced draw.
        auto& am = AssetManager::Get();
        Mesh* tree = MeshBuilder::CreateCube(1.0f);
        tree->SetMaterial(am.GetMaterialHandle(
            am.CreateMaterial("ent_tree_mat", MaterialProps{ .diffuse = glm::vec3(0.16f, 0.42f, 0.18f) })
        ));
        _treeMesh = am.CreateMesh("ent_tree", tree);
        Mesh* rock = MeshBuilder::CreateSphere(0.5f, 10);
        rock->SetMaterial(am.GetMaterialHandle(
            am.CreateMaterial("ent_rock_mat", MaterialProps{ .diffuse = glm::vec3(0.45f, 0.44f, 0.42f) })
        ));
        _rockMesh = am.CreateMesh("ent_rock", rock);

        // Procedural Gaea stand-in: four seamlessly tiling detail layers
        // (albedo + normal, generated in a few ms at init) and a splat
        // generator keyed on the exact streamed height source. Swapping in a
        // real Gaea export later = replace these with file loads; the
        // streamer/shader path is identical.
        const auto grassLayer = TerrainTextureGen::GenerateGrass();
        const auto rockLayer = TerrainTextureGen::GenerateRock();
        const auto dirtLayer = TerrainTextureGen::GenerateDirt();
        const auto snowLayer = TerrainTextureGen::GenerateSnow();

        // The terrain GameObject roots every tile/collider/entity; the
        // component's OnAttach prewarms the world (synchronous, ~1 frame) and
        // its OnTick streams thereafter — no per-frame work left in the app.
        auto* terrainGO = CreateGameObject(glm::vec3(0.0f));
        terrainGO->SetName("Terrain");
        _terrain = static_cast<StreamingTerrainComponent*>(
            terrainGO->AddComponent<StreamingTerrainComponent>(StreamingTerrainProps{
                .worldSize = 10240.0f,
                .tileSize = 512.0f,
                // Balanced against the noise frequency below: perceived
                // steepness is heightScale * frequency, so halving the
                // wavelength (0.00035 -> 0.0007) must roughly halve the
                // amplitude or every slope doubles (and the tree-scatter
                // slope rules stop matching).
                .heightScale = 500.0f,
                .tileHeightRes = 512,// 1 m/texel in the LOD0 ring
                .tileMeshRes = 64,
                .lodCount = 4,
                .lod0RadiusTiles = 2,
                .paletteIndex = 2,// earthy green
                // ~1.4km feature wavelength: enough distinct ridges/valleys
                // across 10km that traversal reads as covering ground.
                .noise = { .resolution = 0, .seed = 20260705, .frequency = 0.0007f, .octaves = 9 },
        // Baked-tile cache: first run generates + stores, every run
        // after boots from pure IO (watch "cache" in the stats line).
        // Resolved against the engine base path (SDL_GetBasePath — next
        // to the executable) so it doesn't depend on the working dir.
        // Emscripten's default FS is RAM-backed, so skip it there and
        // keep generating (ship a preloaded pyramid instead).
#if !defined(__EMSCRIPTEN__)
                .cacheDir = FileSystem::Get().BasePath() + "cache/terrain",
#endif
                // Detail layers + splat = the full Gaea texturing path, fed by
                // the procedural generators above. Tiling is repeats per tile
                // edge (world period = 512m / tiling): grass repeats every 4m,
                // rock every ~21m, dirt every 8m, snow every ~13m so no layer
                // shows an obvious repeat at its typical viewing distance.
                .layers = { { .albedo = grassLayer.albedo, .normal = grassLayer.normal, .tiling = 128.0f },
                            { .albedo = rockLayer.albedo, .normal = rockLayer.normal, .tiling = 24.0f },
                            { .albedo = dirtLayer.albedo, .normal = dirtLayer.normal, .tiling = 64.0f },
                            { .albedo = snowLayer.albedo, .normal = snowLayer.normal, .tiling = 40.0f } },
                .splatFn =
                    [](glm::vec2 mn, glm::vec2 mx, int res, const std::function<float(float, float)>& h01) {
                        TerrainTextureGen::SplatParams sp;
                        sp.heightScale = 500.0f;// must match .heightScale above
                        return TerrainTextureGen::DefaultSplat(mn, mx, res, h01, sp);
                    },
                .colliderRadiusTiles = 1,
                // Deterministic scatter: slope/height rules decide trees vs
                // rocks; the streamer builds/pools one instance cloud per
                // (tile, type) as the ring moves.
                .placeEntitiesFn =
                    [](const TerrainTileContext& ctx) {
                        std::vector<TerrainEntityPlacement> out;
                        uint32_t rng =
                            static_cast<uint32_t>(ctx.coord.x * 73856093 ^ ctx.coord.y * 19349663 ^ ctx.seed);
                        auto rand01 = [&rng] {
                            rng = rng * 1664525u + 1013904223u;
                            return static_cast<float>(rng >> 8) * (1.0f / 16777216.0f);
                        };
                        for (int i = 0; i < 90; ++i) {
                            const float wx = ctx.worldMin.x + rand01() * (ctx.worldMax.x - ctx.worldMin.x);
                            const float wz = ctx.worldMin.y + rand01() * (ctx.worldMax.y - ctx.worldMin.y);
                            const float y = ctx.HeightAt(wx, wz);
                            const float dhdx = (ctx.HeightAt(wx + 4.0f, wz) - ctx.HeightAt(wx - 4.0f, wz)) / 8.0f;
                            const float dhdz = (ctx.HeightAt(wx, wz + 4.0f) - ctx.HeightAt(wx, wz - 4.0f)) / 8.0f;
                            const float slope = std::sqrt(dhdx * dhdx + dhdz * dhdz);
                            const float hn = y / ctx.heightScale;
                            if (slope < 0.35f && hn > 0.25f && hn < 0.62f) {
                                const float s = 3.0f + rand01() * 5.0f;// tree
                                out.push_back({ { wx, y + 0.5f * s, wz }, rand01() * 6.2832f, s, 0 });
                            } else if (slope >= 0.35f && slope < 1.1f && rand01() < 0.35f) {
                                const float s = 1.0f + rand01() * 3.0f;// rock
                                out.push_back({ { wx, y + 0.2f * s, wz }, rand01() * 6.2832f, s, 1 });
                            }
                        }
                        return out;
                    },
                // Instanced clouds: one MeshInstancer per (tile, type) instead
                // of a GameObject per tree/rock — the whole ring is a handful
                // of commands, and BuildBatches folds every tile's cloud of the
                // same mesh into a single instanced draw.
                .entityMeshes = { _treeMesh, _rockMesh },
                .entityRadiusTiles = 3,
                // Streamed grass ring — cells build in as you sprint (GoT-style
                // wind sway). Dark green root grounds the blade in the grass
                // detail layer; golden-yellow tip gives the sunlit-meadow
                // highlight (tuned live in the ImGui colour pickers).
                .grassDensity = 40.0f,// instanced now — 40 blades/m^2 is cheap
                .grassRadius = 90.0f,
                .grassBladeHeight = 1.6f,
                .grassMaxSlope = 1000.0f,// grow grass everywhere, ignore slope
                // Grass blades stop where the splat snowline starts (0.62) —
                // blades poking through snow read wrong; the rock and snow
                // LAYERS carry the peaks now, not bald palette terrain.
                .grassHeightBand = { 0.02f, 0.64f },
                .grassCoverage = 0.7f,
                .grassRootColor = { 0.10f, 0.15f, 0.06f },// shadowed base, matches layer soil/dark green
                .grassTipColor = { 0.965f, 0.949f, 0.388f },// golden-yellow tip (R246 G242 B99), tuned live
                .grassWindStrength = 0.45f,
                .grassWindSpeed = 1.8f,
            })
        );

        // Rivers: one global flow-accumulation pass derives a drainage network
        // from the same height source the tiles use, meshed into draped flowing
        // ribbons. The owner sits at the origin — river vertices are world-space.
        auto* riverGO = CreateGameObject(glm::vec3(0.0f));
        riverGO->SetName("Rivers");
        auto* river = static_cast<RiverComponent*>(riverGO->AddComponent<RiverComponent>(_terrain, RiverProps{}));

        const auto bootMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _bootTime).count();
        ConsoleSubsystem::Get()->Info(
            "TerrainStreaming: full 10.24km x 10.24km horizon ready in " + std::to_string(bootMs) + "ms ("
            + std::to_string(river->RiverCount()) + " rivers, " + std::to_string(river->TriangleCount()) + " tris)"
        );

        // Spawn high enough for an establishing vista over the valley — at
        // ground+40 you only see the local mountainside.
        const float groundY = _terrain->GetHeight(0.0f, 0.0f);
        _camGO->SetPosition(glm::vec3(0.0f, groundY + 200.0f, 0.0f));

        // Unified fly camera. 12 m/s base ~ a sprinting character (crossing
        // the world takes ~14 min, BotW-like), X sprint = 600 m/s for
        // streaming stress tests. Added before the ground clamp so the clamp
        // ticks after the movement.
        _camGO->AddComponent<CameraController3D>(
            /*moveSpeed=*/12.0f, /*lookSpeed=*/1.5f, /*slowMultiplier=*/0.2f, /*fastMultiplier=*/50.0f
        );
        _groundClamp = static_cast<GroundClampComponent*>(_camGO->AddComponent<GroundClampComponent>(_terrain));

        // HUD: the Surface / Palette selectors. Initialize the controls from the
        // streamer's current state so the panel matches the terrain on load.
        SetupHUD();
        ApplyColorMode(_terrain->GetColorMode());
        ApplyPalette(_terrain->GetPalette());

        // Chromatic aberration on by default (toggle in the Graphics panel).
        if (auto* pp = GraphicsSubsystem::Get()->renderer->GetPass<PostProcessPass>()) pp->caEnabled = true;

        ConsoleSubsystem::Get()->Info(
            "WASD move, arrows look, X sprint, Z slow, R/F up/down, G ground-clamp, T teleport, SPACE surface mode, "
            "P palette, I LOD debug, ESC quit."
        );
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (!_cam || !_terrain) return;
        auto* input = InputSubsystem::Get();

        // Movement/look/sprint live in CameraController3D; the ground clamp in
        // GroundClampComponent. Only demo-specific keys remain here.
        TerrainStreamer& terrain = _terrain->Streamer();
        if (input->IsKeyPressed(Key::T)) {
            // Teleport 2km along the view direction, kept inside the world —
            // stepping past the edge leaves nothing but void in front of you.
            const float bound = 0.5f * terrain.Props().worldSize - terrain.Props().tileSize;
            glm::vec3 fwd = _cam->GetEyeDirection();
            fwd.y = 0.0f;
            fwd = glm::length(fwd) > 1e-3f ? glm::normalize(fwd) : glm::vec3(1, 0, 0);
            glm::vec3 pos = _camGO->GetPosition() + fwd * 2000.0f;
            pos.x = std::clamp(pos.x, -bound, bound);
            pos.z = std::clamp(pos.z, -bound, bound);
            pos.y = terrain.GetHeight(pos.x, pos.z) + 200.0f;
            _camGO->SetPosition(pos);
            ConsoleSubsystem::Get()->Info(
                "Teleported to (" + std::to_string(static_cast<int>(pos.x)) + ", "
                + std::to_string(static_cast<int>(pos.z)) + ")"
            );
        }
        if (input->IsKeyPressed(Key::G) && _groundClamp) _groundClamp->enabled = !_groundClamp->enabled;
        // SPACE cycles the surface mode through the same three options as the
        // HUD's Surface dropdown (Textured -> Palette -> LOD tint -> ...); the
        // HUD select is kept in sync by ApplyColorMode.
        if (input->IsKeyPressed(Key::SPACE)) {
            const int next = (static_cast<int>(terrain.GetColorMode()) + 1) % 3;
            ApplyColorMode(static_cast<TerrainColorMode>(next));
        }
        // P selects the palette (0..5, wrapping) — it does NOT touch the mode,
        // so it only shows when the surface mode is Palette. Cycle to it with
        // SPACE (or the dropdown) first.
        if (input->IsKeyPressed(Key::P)) ApplyPalette(terrain.GetPalette() + 1);
        // I jumps straight to the LOD-tint debug view (LOD colours + wireframe,
        // coupled in ApplyColorMode) and back to Textured — a shortcut past the
        // SPACE cycle.
        if (input->IsKeyPressed(Key::I)) {
            ApplyColorMode(
                terrain.GetColorMode() != TerrainColorMode::LodTint ? TerrainColorMode::LodTint
                                                                    : TerrainColorMode::Textured
            );
        }
        if (input->IsKeyPressed(Key::ESCAPE)) Quit();

        // Streaming is driven by StreamingTerrainComponent::OnTick now; the app
        // only reads stats.
        const auto& stats = _terrain->GetStats();
        if (!_reportedStreamed && stats.initialLoadDone) {
            _reportedStreamed = true;
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _bootTime)
                    .count();
            ConsoleSubsystem::Get()->Info(
                "TerrainStreaming: every tile at its target LOD after " + std::to_string(ms) + "ms"
            );
        }
        _statsTimer += dt;
        if (_statsTimer >= 5.0f) {
            _statsTimer = 0.0f;
            const glm::vec3 pos = _camGO->GetPosition();
            const float bound = 0.5f * terrain.Props().worldSize;
            const bool inside = std::abs(pos.x) <= bound && std::abs(pos.z) <= bound;
            ConsoleSubsystem::Get()->Info(
                "cam (" + std::to_string(static_cast<int>(pos.x)) + ", " + std::to_string(static_cast<int>(pos.z))
                + (inside ? ") " : ") OUTSIDE WORLD ") + "tiles " + std::to_string(stats.loadedTiles) + " visible "
                + std::to_string(stats.visibleTiles) + " pending " + std::to_string(stats.pendingJobs) + " entities "
                + std::to_string(stats.activeEntities) + " grass " + std::to_string(stats.grassCells) + "c/"
                + std::to_string(stats.grassBlades / 1000) + "k cache " + std::to_string(stats.cacheHits) + "/"
                + std::to_string(stats.cacheHits + stats.cacheMisses) + " heightmapMB "
                + std::to_string(stats.gpuHeightmapBytes / (1024 * 1024))
            );
        }
    }
};

int main(int argc, char* argv[]) {
    TerrainStreamingDemo game(
        {
            .windowTitle = "Atmospheric — Terrain Streaming (10km x 10km)",
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
