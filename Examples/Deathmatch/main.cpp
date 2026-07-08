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
//           (hold in air to levitate) · Q dash · C shield (hold) · ESC quit.
//
// The local player is a DeathmatchController Component (mirroring the engine's
// CameraController3D): it owns input sampling, the first-person camera, and
// the per-tick SubmitInput. The Application builds the greybox world and syncs
// the enemy/rocket render objects from ClientNet.
//
// NOTE: This render/input layer needs the full engine and is NOT built in the
// netcode CI lane; the netcode it drives is the headless-verified ClientNet.
// The engine has no relative-mouse/cursor-lock, so mouse-look reads the
// absolute cursor delta (the cursor can escape at window edges). Low-poly
// "Control"-style materials + particle juice are a planned polish pass.
#include "Atmospheric.hpp"
#include "authority.hpp"
#include "client_net.hpp"
#include "sim_common.hpp"
#include "vat_enemy.hpp"

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

        // Mouse-look via absolute-cursor delta (no relative mouse in-engine).
        const glm::vec2 mouse = inp->GetMousePosition();
        if (_haveMouse) {
            const glm::vec2 d = mouse - _lastMouse;
            _cam->Yaw(d.x * 0.003f);
            _cam->Pitch(-d.y * 0.003f);
        }
        _lastMouse = mouse;
        _haveMouse = true;

        // Edge-triggered actions latched between fixed ticks.
        if (inp->IsMouseButtonPressed()) _pendingRail = true;
        if (inp->IsKeyPressed(Key::F)) _pendingRocket = true;
        if (inp->IsKeyPressed(Key::Q)) _pendingDash = true;

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
            _net->SubmitInput(_tick++, forward, strafe, yaw, pitch, jump, dash, shield, fr, fk);

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
    glm::vec2 _lastMouse{ 0.0f, 0.0f };
    bool _haveMouse = false;
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

    void MakeMaterial(const std::string& name, glm::vec3 diffuse) {
        MaterialProps props;
        props.diffuse = diffuse;
        AssetManager::Get().CreateMaterial(name, props);
    }

    GameObject* MakeBox(const sim::Box& b, MeshHandle cube) {
        auto* go =
            CreateGameObject(glm::vec3((b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f, (b.minZ + b.maxZ) * 0.5f));
        go->SetScale(glm::vec3(b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ));
        go->AddComponent<MeshComponent>(cube);
        return go;
    }

    void OnLoad() override {
        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        // Low-poly greybox palette (concrete grey + red accents).
        MakeMaterial("dm_floor_mat", glm::vec3(0.30f, 0.30f, 0.33f));
        MakeMaterial("dm_box_mat", glm::vec3(0.48f, 0.48f, 0.52f));
        MakeMaterial("dm_enemy_mat", glm::vec3(0.80f, 0.18f, 0.16f));
        MakeMaterial("dm_rocket_mat", glm::vec3(1.0f, 0.55f, 0.12f));

        auto& am = AssetManager::Get();
        auto floorMesh = am.CreatePlaneMesh("dm_floor", 2.0f * sim::kArenaHalf, 2.0f * sim::kArenaHalf);
        am.GetMeshPtr(floorMesh)->SetMaterial(am.GetMaterialHandle("dm_floor_mat"));
        CreateGameObject(glm::vec3(0.0f))->AddComponent<MeshComponent>(floorMesh);

        auto cubeMesh = am.CreateCubeMesh("dm_cube", 1.0f);
        am.GetMeshPtr(cubeMesh)->SetMaterial(am.GetMaterialHandle("dm_box_mat"));
        const sim::Box* boxes = sim::Boxes();
        for (int i = 0; i < sim::kNumBoxes; i++)
            MakeBox(boxes[i], cubeMesh);

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
            // Preset metallic look: gunmetal base, full metalness, low roughness.
            // Keep back-face culling (the material default): the blob stays
            // convex, and culling means when the player walks up to shoot the
            // dummy and their camera clips inside the mesh, they see through it
            // instead of the interior covering the screen.
            if (auto* mat = vat->GetMaterial()) {
                // Matte-ish roughness: without image-based lighting a near-mirror
                // metal is one blown-out hotspot that BloomPass smears across the
                // whole frame, so keep the highlight broad and dim.
                MetalMaps m = MakePresetMetalMaps(glm::vec3(0.59f, 0.16f, 0.15f), /*roughness*/ 0.55f);
                mat->baseMap = m.base;
                mat->metallicMap = m.metallic;
                mat->roughnessMap = m.roughness;
                mat->diffuse = glm::vec3(1.0f);// tint comes from baseMap now
            }
        } else {
            // Networked opponent (or a bake failure): plain capsule.
            auto capsuleMesh = am.CreateCapsuleMesh("dm_capsule", sim::kCapsuleRadius, sim::kCapsuleHeight);
            am.GetMeshPtr(capsuleMesh)->SetMaterial(am.GetMaterialHandle("dm_enemy_mat"));
            _enemyGO->AddComponent<MeshComponent>(capsuleMesh);
        }

        auto rocketMesh = am.CreateSphereMesh("dm_rocket", sim::kRocketRadius, 10);
        am.GetMeshPtr(rocketMesh)->SetMaterial(am.GetMaterialHandle("dm_rocket_mat"));
        for (int i = 0; i < kRocketPoolSize; i++) {
            auto* go = CreateGameObject(kParked);
            go->AddComponent<MeshComponent>(rocketMesh);
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
            cam->SetPerspective(glm::radians(90.0f), static_cast<float>(ws.width) / ws.height, 0.05f, 200.0f);
            cam->gameObject->AddComponent<DeathmatchController>(&_net, localAuth);// the local player
        }

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

        if (InputSubsystem::Get()->IsKeyDown(Key::ESCAPE)) Quit();
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
