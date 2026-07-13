// DeathmatchClient — 2-player, server-authoritative, Quake-style 3D arena
// shooter (first-person) with a small Control-style ability kit. Demonstrates
// the FPS-lineage client stack: predicts own movement (incl. jump/levitate/
// dash) and reconciles it with exact rewind-replay, replicates and
// interpolates the enemy's position AND view angles, shows cosmetic rockets,
// and replicates the shield buff. The server does the lag compensation.
//
//   ./DeathmatchClient --connect <ip> [port]     (default 127.0.0.1:9200)
//   ./DeathmatchClient --local                    (embed the server, play solo)
//
// --local runs a single-process test mode: a DeathmatchAuthority is embedded
// and pumped in-process and this client talks to it over loopback, so one
// binary is a playable test arena without launching a separate server.
//
// Controls: WASD move · mouse look · LMB railgun · F rocket · SPACE jump
//           (hold in air to levitate) · Q dash · C shield (hold) · ESC toggle
//           mouse capture (close the window / Cmd-Q to quit).
//
// The local player is a DeathmatchController Component (mirroring the engine's
// CameraController3D): it owns input sampling, the first-person camera, and
// the per-tick SubmitInput. The Application builds the greybox world and syncs
// the enemy/rocket render objects from ClientNet.
//
// NOTE: This render/input layer needs the full engine and is NOT built in the
// netcode CI lane; the netcode it drives is the headless-verified ClientNet.
// Mouse-look uses the engine's relative (pointer-lock) mouse mode; ESC toggles
// the capture. Low-poly "Control"-style materials + particle juice are a
// planned polish pass.
#include "Atmospheric.hpp"
#include "authority.hpp"
#include "client_net.hpp"
#include "sim_common.hpp"
#include "vat_enemy.hpp"
#include <Atmospheric/net_debug_controls.hpp>
#include <Atmospheric/net_hud.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
    struct CliOptions {
        std::string serverIp = "127.0.0.1";
        uint16_t serverPort = 9200;
        bool local = false;
    } gcli;

    uint32_t NowMs() {
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
    }

    constexpr int kRocketPoolSize = 16;
    const glm::vec3 kParked = glm::vec3(0.0f, -100.0f, 0.0f);
}// namespace

// ── Local-player controller ──────────────────────────────────────────────────
// A Component (like CameraController3D): samples input, drives the FP camera,
// and feeds ClientNet one input per fixed tick.
class DeathmatchController : public Component {
public:
    // authority is non-null only in --local mode, where it's pumped in-process
    // right before the client's own Pump so this one binary is a full game.
    DeathmatchController(GameObject* cameraGO, ClientNet* net, DeathmatchAuthority* authority)
      : _net(net), _authority(authority) {
        gameObject = cameraGO;
    }

    std::string GetName() const override {
        return "DeathmatchController";
    }

    void OnAttach() override {
        _cam = gameObject->GetComponent<CameraComponent>();
    }

    void OnTick(float dt) override {
        auto* inp = InputSubsystem::Get();
        if (!inp || !_cam) return;
        const uint32_t nowMs = NowMs();

        if (_authority) _authority->Pump();// --local: drive the embedded server
        _net->Pump(nowMs);
        _net->UpdateCosmetic(dt);

        // Mouse-look via relative (pointer-lock) motion. Only while captured, so
        // an unlocked cursor (ESC released) doesn't keep spinning the view.
        if (Window::Get()->IsRelativeMouseMode()) {
            const glm::vec2 d = Window::Get()->GetMouseDelta();
            _cam->Yaw(d.x * 0.003f);
            _cam->Pitch(-d.y * 0.003f);
        }

        // Edge-triggered actions latched between fixed ticks.
        if (inp->IsMouseButtonPressed()) _pendingRail = true;
        if (inp->IsKeyPressed(Key::F)) _pendingRocket = true;
        if (inp->IsKeyPressed(Key::Q)) _pendingDash = true;

        // Dial the inbound link emulator live (1/2 latency, 3/4 jitter, 5/6
        // loss, 0 reset) so the netgraph's prediction/reconciliation/RTT lines
        // respond on screen — works even in --local loopback play.
        DialConditioner(inp, _net->Conditioner());

        const glm::vec3 vd = _cam->GetEyeDirection();
        const float yaw = std::atan2(vd.x, -vd.z);
        const float pitch = std::asin(sim::Clamp(vd.y, -1.0f, 1.0f));

        _accum += std::min(dt, 0.25f);
        while (_accum >= sim::kTickDt) {
            _accum -= sim::kTickDt;
            float forward = 0.0f, strafe = 0.0f;
            if (inp->IsKeyDown(Key::W)) forward += 1.0f;
            if (inp->IsKeyDown(Key::S)) forward -= 1.0f;
            if (inp->IsKeyDown(Key::D)) strafe += 1.0f;
            if (inp->IsKeyDown(Key::A)) strafe -= 1.0f;
            const bool jump = inp->IsKeyDown(Key::SPACE);// tap = jump, hold in air = levitate
            const bool shield = inp->IsKeyDown(Key::C);
            const bool dash = _pendingDash;
            const bool fr = _pendingRail, fk = _pendingRocket;
            _pendingRail = _pendingRocket = _pendingDash = false;
            _net->SubmitInput(nowMs, _tick++, forward, strafe, yaw, pitch, jump, dash, shield, fr, fk);

            // Railgun is hitscan (instant) — give it a visible beam so it reads
            // differently from the rocket's slow projectile.
            if (fr) {
                const glm::vec3 eye = _cam->GetEyePosition();
                const glm::vec3 dir = _cam->GetEyeDirection();
                _beamFrom = eye + dir * 0.5f;// nudge past the near plane
                _beamTo = eye + dir * 100.0f;
                _beamTimer = 0.08f;
            }
        }

        // First-person camera at the eye of the predicted foot position.
        const sim::Vec3 foot = _net->GetOwnFoot();
        gameObject->SetPosition(glm::vec3(foot.x, foot.y + sim::kEyeHeight, foot.z));

        // Draw the railgun beam (world-space debug line) while its flash lasts.
        // Debug lines are cleared each frame, so re-push it every frame.
        if (_beamTimer > 0.0f) {
            _beamTimer -= dt;
            GraphicsSubsystem::Get()->PushDebugLine(
                { _beamFrom, glm::vec3(0.6f, 0.9f, 1.0f) }, { _beamTo, glm::vec3(0.6f, 0.9f, 1.0f) }
            );
        }
    }

private:
    ClientNet* _net;
    DeathmatchAuthority* _authority = nullptr;
    CameraComponent* _cam = nullptr;
    float _accum = 0.0f;
    uint32_t _tick = 0;
    bool _pendingRail = false, _pendingRocket = false, _pendingDash = false;
    float _beamTimer = 0.0f;
    glm::vec3 _beamFrom{ 0.0f }, _beamTo{ 0.0f };
};

// ── Application: world + render sync ─────────────────────────────────────────
class DeathmatchGame : public Application {
    using Application::Application;

    ClientNet _net;
    DeathmatchAuthority _localAuthority;// only bound/pumped in --local mode
    FontHandle _fontID = 0;
    GameObject* _enemyGO = nullptr;
    std::vector<GameObject*> _rocketPool;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    Material* MakeMaterial(const std::string& name, glm::vec3 diffuse) {
        MaterialProps props;
        props.diffuse = diffuse;
        return AssetManager::Get().CreateMaterial(name, props);
    }

    // Solid 1x1 texture — used to pin a floor/prop to a specific roughness or
    // metalness (pbr.frag reads those from maps, not scalars).
    TextureHandle MakeSolidTexture(glm::vec3 rgb) {
        auto b = [](float x) { return static_cast<unsigned char>(std::round(glm::clamp(x, 0.0f, 1.0f) * 255.0f)); };
        unsigned char px[3] = { b(rgb.r), b(rgb.g), b(rgb.b) };
        return AssetManager::Get().CreateTextureFromImage(std::make_shared<Image>(1, 1, 3, px));
    }

    // Blueprint-style grid baked into a single 0..1-UV texture: gives the floor
    // a strong motion reference (a flat matte plane reads as no self-motion).
    TextureHandle MakeGridTexture(int cells, glm::vec3 fill, glm::vec3 line) {
        const int S = 512;
        const int cell = S / cells;
        const int lw = 2;
        auto b = [](float x) { return static_cast<unsigned char>(std::round(glm::clamp(x, 0.0f, 1.0f) * 255.0f)); };
        std::vector<unsigned char> px(static_cast<size_t>(S) * S * 3);
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                const bool onLine = (x % cell) < lw || (y % cell) < lw;
                const glm::vec3 c = onLine ? line : fill;
                const size_t i = (static_cast<size_t>(y) * S + x) * 3;
                px[i] = b(c.r);
                px[i + 1] = b(c.g);
                px[i + 2] = b(c.b);
            }
        }
        return AssetManager::Get().CreateTextureFromImage(std::make_shared<Image>(S, S, 3, px.data()));
    }

    GameObject* MakeBox(const sim::Box& b, MeshHandle cube) {
        auto* go =
            CreateGameObject(glm::vec3((b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f, (b.minZ + b.maxZ) * 0.5f));
        go->SetScale(glm::vec3(b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ));
        go->AddComponent<MeshRenderer>(cube);
        return go;
    }

    void OnLoad() override {
        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        // Optional HDRI sky: drop an equirectangular .hdr/.exr at
        // assets/textures/ to replace the gradient sky (and, once the IBL
        // phases land, to light the metal blob). Absent → gradient fallback.
        if (FileSystem::Get().Exists("assets/textures/ushaka_sea_world_aquarium_2k.exr")) {
            TextureHandle env = AssetManager::Get().LoadHDR("assets/textures/ushaka_sea_world_aquarium_2k.exr");
            if (env.IsValid()) GraphicsSubsystem::Get()->renderer->environmentMap = env;
        }

        // Default post-process look for this example: bloom + chromatic
        // aberration on (both toggleable live in the Graphics ImGui panel).
        auto* renderer = GraphicsSubsystem::Get()->renderer.get();
        if (auto* bloom = renderer->GetPass<BloomPass>()) bloom->enabled = true;
        if (auto* pp = renderer->GetPass<PostProcessPass>()) pp->caEnabled = true;

        // Low-poly greybox palette (concrete grey + red accents).
        MakeMaterial("dm_box_mat", glm::vec3(0.48f, 0.48f, 0.52f));
        MakeMaterial("dm_enemy_mat", glm::vec3(0.80f, 0.18f, 0.16f));
        MakeMaterial("dm_rocket_mat", glm::vec3(1.0f, 0.55f, 0.12f));

        auto& am = AssetManager::Get();

        // ── Ground ──────────────────────────────────────────────────────────
        // Play floor gets a blueprint grid so self-motion is legible (a flat
        // matte plane reads as standing still). Kept matte — solid high
        // roughness / zero metalness — so IBL tints it instead of mirroring the
        // sky; diffuse=white lets the grid texture show through unmodulated.
        Material* floorMat = MakeMaterial("dm_floor_mat", glm::vec3(1.0f));
        floorMat->baseMap = MakeGridTexture(24, glm::vec3(0.26f, 0.28f, 0.32f), glm::vec3(0.50f, 0.54f, 0.62f));
        floorMat->roughnessMap = MakeSolidTexture(glm::vec3(0.85f));
        floorMat->metallicMap = MakeSolidTexture(glm::vec3(0.0f));
        auto floorMesh = am.CreatePlaneMesh("dm_floor", 2.0f * sim::kArenaHalf, 2.0f * sim::kArenaHalf);
        am.GetMeshPtr(floorMesh)->SetMaterial(am.GetMaterialHandle("dm_floor_mat"));
        CreateGameObject(glm::vec3(0.0f))->AddComponent<MeshRenderer>(floorMesh);

        // The whole static environment shell — a walled "training yard" with a
        // curb ring at the movement clamp, perimeter walls with pilasters and
        // signal stripes, corner watchtowers, a gated south wall, a control-room
        // deck, a quarter-pipe (Bezier patch), road barriers, crates, a shooting
        // range, and distant skyline towers — is authored in
        // assets/maps/arena.map and imported below. Its per-face texture names
        // resolve to the palette materials created here (Instantiate looks each
        // material up by name); its light entities become the arena's point
        // lights. The ground reaches the HDRI horizon so objects sit on ground.
        // PBR palette. roughness/metalness are what give the greybox a "finished"
        // read: pbr.frag samples both from these solid maps, and with the HDRI
        // environment loaded above, metallic surfaces pick up real reflections.
        // So the surfaces are deliberately contrasted — matte concrete vs. brushed
        // metal trim vs. glossy dark glass — rather than flat same-roughness fills.
        auto pbr = [&](const std::string& name, glm::vec3 rgb, float rough, float metal) {
            Material* m = MakeMaterial(name, rgb);
            m->roughnessMap = MakeSolidTexture(glm::vec3(rough));
            m->metallicMap = MakeSolidTexture(glm::vec3(metal));
            return m;
        };
        pbr("dm_ground_mat", glm::vec3(0.13f, 0.14f, 0.16f), 0.92f, 0.0f);// matte asphalt apron
        pbr("dm_wall_mat", glm::vec3(0.44f, 0.42f, 0.38f), 0.82f, 0.0f);// matte warm concrete
        pbr("dm_pillar_mat", glm::vec3(0.42f, 0.44f, 0.49f), 0.75f, 0.08f);// props / quarter-pipe, faint sheen
        pbr("dm_far_mat", glm::vec3(0.20f, 0.21f, 0.24f), 0.95f, 0.0f);// matte skyline silhouettes
        pbr("dm_trim_mat", glm::vec3(0.26f, 0.27f, 0.30f), 0.40f, 0.75f);// brushed-metal caps/curbs/barriers (IBL)
        pbr("dm_accent_mat", glm::vec3(0.72f, 0.15f, 0.12f), 0.50f, 0.10f);// painted signal-red
        pbr("dm_window_mat", glm::vec3(0.04f, 0.06f, 0.09f), 0.05f, 0.60f);// dark glossy glass, sharp reflections

        auto cubeMesh = am.CreateCubeMesh("dm_cube", 1.0f);
        am.GetMeshPtr(cubeMesh)->SetMaterial(am.GetMaterialHandle("dm_box_mat"));
        const sim::Box* boxes = sim::Boxes();
        for (int i = 0; i < sim::kNumBoxes; i++)
            MakeBox(boxes[i], cubeMesh);

        // ── Static environment shell (imported from a TrenchBroom .map) ──────
        // The whole training-yard set dressing (everything outside the
        // ±kArenaHalf movement clamp) lives in arena.map. It is authored in
        // engine units (loaded at scale 1.0) and instantiated as a node subtree;
        // per-texture batches resolve to the materials created above, each brush
        // contributes a static convex collider, and the map's light entities
        // spawn LightComponents (the warm tower floods + cool deck wash). The
        // grid play floor and gameplay boxes stay procedural — the authoritative
        // sim owns box collision (see sim::Boxes()), and the floor needs its
        // blueprint-grid UVs that a brush texture projection can't reproduce.
        Prefab arena = ImportMapPrefab("assets/maps/arena.map", 1.0f);
        if (arena.ok) {
            Instantiate(arena, nullptr, "arena");
            // Entity data survives import: the map's info_player_start entities
            // are queryable for gameplay (the netcode sim dictates actual spawns
            // here, so this just demonstrates the API).
            const auto spawns = arena.FindEntities("info_player_start");
            ConsoleSubsystem::Get()->Info(
                "arena.map: " + std::to_string(spawns.size()) + " spawn point(s), "
                + std::to_string(arena.colliders.size()) + " brush collider(s)"
            );
        }

        // Enemy avatar. In --local solo mode the "enemy" is the embedded
        // server's training bot (a practice dummy), so render it as an animated
        // VAT "blob" with a preset metallic material — a distinct target. A real
        // networked opponent is another player, so it stays a plain capsule.
        // Either way only the render mesh differs; the sim collides and
        // lag-compensates against the gameplay capsule (kCapsuleRadius/Height).
        _enemyGO = CreateGameObject(kParked);
        VATDemoAsset enemyAsset;
        if (gcli.local) {
            enemyAsset = BuildBlobVATAsset(
                "dm_enemy_vat", /*radius*/ sim::kCapsuleRadius * 1.5f, /*division*/ 28, /*frames*/ 60, /*fps*/ 24.0f
            );
        }
        if (enemyAsset.clip) {
            auto* vat = static_cast<VATComponent*>(_enemyGO->AddComponent<VATComponent>(
                enemyAsset.mesh, std::move(enemyAsset.clip), VATProps{ .speed = 1.0f }
            ));
            // Preset metallic look: gunmetal-red base, full metalness. Back-face
            // culling is the material default and the blob is convex, so there's
            // nothing to override. Without image-based lighting a pure metal is
            // mostly dark except the directional highlight, so a mid roughness
            // spreads that highlight enough to read as metal — tune to taste.
            if (auto* mat = vat->GetMaterial()) {
                MetalMaps m = MakePresetMetalMaps(glm::vec3(0.59f, 0.16f, 0.15f), /*roughness*/ 0.4f);
                mat->baseMap = m.base;
                mat->metallicMap = m.metallic;
                mat->roughnessMap = m.roughness;
                mat->diffuse = glm::vec3(1.0f);// tint comes from baseMap now
            }
        } else {
            // Networked opponent (or a bake failure): plain capsule.
            auto capsuleMesh = am.CreateCapsuleMesh("dm_capsule", sim::kCapsuleRadius, sim::kCapsuleHeight);
            am.GetMeshPtr(capsuleMesh)->SetMaterial(am.GetMaterialHandle("dm_enemy_mat"));
            _enemyGO->AddComponent<MeshRenderer>(capsuleMesh);
        }

        auto rocketMesh = am.CreateSphereMesh("dm_rocket", sim::kRocketRadius, 10);
        am.GetMeshPtr(rocketMesh)->SetMaterial(am.GetMaterialHandle("dm_rocket_mat"));
        for (int i = 0; i < kRocketPoolSize; i++) {
            auto* go = CreateGameObject(kParked);
            go->AddComponent<MeshRenderer>(rocketMesh);
            _rocketPool.push_back(go);
        }

        // --local: embed the authority in this process and talk to it over
        // loopback, so one binary is a playable test arena (alone until a
        // second player would join).
        DeathmatchAuthority* localAuth = nullptr;
        if (gcli.local) {
            if (_localAuthority.Bind(0)) {
                _localAuthority.SpawnTrainingBot();// a target to see/shoot when solo
                gcli.serverIp = "127.0.0.1";
                gcli.serverPort = _localAuthority.BoundPort();
                localAuth = &_localAuthority;
            } else {
                ConsoleSubsystem::Get()->Error("Failed to bind embedded authority");
            }
        }

        if (auto* cam = GraphicsSubsystem::Get()->GetMainCamera()) {
            auto ws = Window::Get()->GetLogicalSize();
            // SetPerspective takes vertical FOV in *degrees* (it converts to
            // radians internally). 90 deg for an FPS-feeling wide view.
            cam->SetPerspective(90.0f, static_cast<float>(ws.width) / ws.height, 0.05f, 200.0f);
            cam->gameObject->AddComponent<DeathmatchController>(&_net, localAuth);// the local player
        }

        // Capture the mouse for FPS look (cursor hidden + locked). ESC toggles
        // it; the window close button / Cmd-Q quits.
        Window::Get()->SetRelativeMouseMode(true);

        if (!_net.Connect(gcli.serverIp, gcli.serverPort)) {
            ConsoleSubsystem::Get()->Error(fmt::format("Failed to connect to {}:{}", gcli.serverIp, gcli.serverPort));
        }
    }

    void OnUpdate(float /*dt*/, float /*time*/) override {
        const uint32_t nowMs = NowMs();

        // Enemy capsule (interpolated position + yaw); a red-shift could denote
        // the shield in the polish pass — EnemyShielded() is available.
        if (_net.HasEnemy()) {
            const sim::Vec3 ef = _net.GetEnemyFoot(nowMs);
            _enemyGO->SetPosition(glm::vec3(ef.x, ef.y + sim::kCapsuleHeight * 0.5f, ef.z));
            _enemyGO->SetRotation(glm::vec3(0.0f, _net.GetEnemyYaw(nowMs), 0.0f));
        } else {
            _enemyGO->SetPosition(kParked);
        }

        int slot = 0;
        for (const auto& r : _net.AuthoritativeRockets()) {
            if (slot >= kRocketPoolSize) break;
            _rocketPool[slot++]->SetPosition(glm::vec3(r.pos.x, r.pos.y, r.pos.z));
        }
        for (const auto& c : _net.CosmeticRockets()) {
            if (slot >= kRocketPoolSize) break;
            _rocketPool[slot++]->SetPosition(glm::vec3(c.pos.x, c.pos.y, c.pos.z));
        }
        for (; slot < kRocketPoolSize; slot++)
            _rocketPool[slot]->SetPosition(kParked);

        DrawHud();

        // ESC toggles pointer lock (release / recapture the cursor) instead of
        // quitting; close the window (or Cmd-Q) to quit.
        if (InputSubsystem::Get()->IsKeyPressed(Key::ESCAPE)) {
            auto* win = Window::Get();
            win->SetRelativeMouseMode(!win->IsRelativeMouseMode());
        }
    }

    void DrawHud() {
        auto* gfx = GraphicsSubsystem::Get();
        auto ws = Window::Get()->GetLogicalSize();
        gfx->DrawText(
            _fontID,
            "HP " + std::to_string(_net.GetHealth()),
            20.0f,
            20.0f,
            0.9f,
            _net.IsAlive() ? glm::vec4(1.0f) : glm::vec4(1.0f, 0.4f, 0.4f, 1.0f)
        );
        gfx->DrawText(
            _fontID,
            "You " + std::to_string(_net.GetScore()) + "  -  " + std::to_string(_net.GetEnemyScore()) + " Them",
            20.0f,
            50.0f,
            0.8f,
            glm::vec4(0.85f, 0.85f, 0.85f, 1.0f)
        );
        if (_net.IsShielded()) gfx->DrawText(_fontID, "SHIELD", 20.0f, 84.0f, 0.8f, glm::vec4(0.4f, 0.7f, 1.0f, 1.0f));
        if (_net.DashCooldownFrac() <= 0.0f)
            gfx->DrawText(_fontID, "DASH READY", 20.0f, 116.0f, 0.7f, glm::vec4(0.7f, 1.0f, 0.7f, 1.0f));
        if (!_net.IsAlive())
            gfx->DrawText(_fontID, "DOWNED — respawning...", 20.0f, 148.0f, 0.9f, glm::vec4(1.0f, 0.5f, 0.5f, 1.0f));
        // Crosshair.
        gfx->DrawLine(
            ws.width * 0.5f - 8,
            ws.height * 0.5f,
            ws.width * 0.5f + 8,
            ws.height * 0.5f,
            glm::vec4(0.9f, 0.9f, 0.9f, 0.8f)
        );
        gfx->DrawLine(
            ws.width * 0.5f,
            ws.height * 0.5f - 8,
            ws.width * 0.5f,
            ws.height * 0.5f + 8,
            glm::vec4(0.9f, 0.9f, 0.9f, 0.8f)
        );
        // Netgraph (top-right): RTT / loss / bandwidth / prediction error /
        // pending inputs + the live conditioner knobs (keys 1-6, 0 to reset).
        // Debug-only — it's a netcode diagnostic, not part of the shipped HUD.
#ifndef NDEBUG
        DrawNetHud(gfx, _fontID, _net.Metrics(), _net.Conditioner(), ws.width - 258.0f, 20.0f);
#endif
    }
};

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            gcli.serverIp = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') gcli.serverPort = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--local") == 0) {
            gcli.local = true;
        }
    }

#ifdef __EMSCRIPTEN__
    // The shell.html lobby launches Solo via callMain(["--local"]); its PvP mode
    // is WIP (needs a WebTransport transport + server). Until that lands the web
    // build is always single-player: an embedded authority reached over an
    // in-process LoopbackDatagramSocket (see datagram_socket.hpp). Forced here
    // too so a bare launch can never end up in a broken connect state.
    gcli.local = true;
#endif

    DeathmatchGame game(
        { .windowTitle = "Deathmatch",
          .windowWidth = 1280,
          .windowHeight = 720,
          .enableAudio = false,
          .useDefaultTextures = true,
          .useDefaultShaders = true }
    );
    game.Run();
    return 0;
}
