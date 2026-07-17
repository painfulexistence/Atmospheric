#include "Atmospheric.hpp"
#include "Atmospheric/gfx_factory.hpp"
#include "Atmospheric/net_debug_controls.hpp"
#include "Atmospheric/net_hud.hpp"
#include "components.hpp"
#include "game_sim.hpp"
#include "net_lockstep.hpp"

#include <RmlUi/Core.h>
#include <cstring>
#include <ctime>

// ─────────────────────────────────────────────────────────────────────────────
// NoitaLike: two-player networked falling-sand arena (deterministic lockstep).
//
//   ./NoitaLikeDemo                       solo sandbox
//   ./NoitaLikeDemo --host [port]         host a 2P game (player 0), direct UDP
//   ./NoitaLikeDemo --join <ip> [port]    join a 2P game  (player 1), direct UDP
//
//   ./NoitaLikeDemo --relay-host <relayIp> <relayPort> <roomId>   host via relay
//   ./NoitaLikeDemo --relay-join <relayIp> <relayPort> <roomId>   join via relay
//     Use when direct UDP can't reach the other peer (NAT/firewall). Both
//     players point at the same UdpRelay (see Examples/RelayServer) and pick
//     any shared roomId; the relay pairs the first two senders it sees.
//
//   options: --seed <n>  --delay <ticks: input delay, default 3 = 50 ms>
//
// Controls: A/D move · W/Space jump+levitate · mouse aim · LMB cast
//           1-7 select spell · ESC quit
// Netgraph (top-right): the number row is spells here, so the link-emulator
// keys are J/K latency · N/M jitter · U/I loss · O reset — lockstep reacts to
// loss by stalling (waiting for the missing input), not by mispredicting.
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    struct CliOptions {
        LockstepNet::Mode mode = LockstepNet::Mode::Solo;
        std::string joinIp;
        uint16_t port = 7777;
        uint32_t seed = 0;
        int delay = 3;
        uint32_t autotestTicks = 0;// run N ticks headless-style, print checksum, quit
        // Relay mode (mutually exclusive with direct --host/--join above).
        bool useRelay = false;
        std::string relayIp;
        uint16_t relayPort = 0;
        uint32_t roomId = 0;
    } gcli;

    inline uint32_t Pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return static_cast<uint32_t>(r) | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b) << 16
               | static_cast<uint32_t>(a) << 24;
    }

    inline uint8_t Shift(int base, int delta) {
        return static_cast<uint8_t>(std::clamp(base + delta, 0, 255));
    }

    uint32_t CellColor(const Cell& c, uint32_t tick) {
        int v = (c.shade & 31) - 16;
        switch (Mat(c.mat)) {
        case Mat::Empty:
            return Pack(24, 21, 33);
        case Mat::Stone:
            return Pack(Shift(86, v), Shift(86, v), Shift(94, v));
        case Mat::Dirt:
            return Pack(Shift(106, v), Shift(70, v), Shift(42, v / 2));
        case Mat::Sand:
            return Pack(Shift(218, v), Shift(182, v), Shift(102, v));
        case Mat::Wood:
            return Pack(Shift(150, v), Shift(102, v), Shift(52, v / 2));
        case Mat::Water:
            return Pack(Shift(46, v / 2), Shift(112, v), Shift(222, v));
        case Mat::Oil:
            return Pack(Shift(62, v / 2), Shift(50, v / 2), Shift(34, v / 2));
        case Mat::Acid:
            return Pack(Shift(112, v), Shift(230, v), Shift(62, v));
        case Mat::Lava: {
            int f = static_cast<int>((tick + c.shade) & 15) * 5;
            return Pack(Shift(240, f / 2), Shift(96, v + f), 24);
        }
        case Mat::Fire: {
            int f = static_cast<int>((tick * 7 + c.shade) & 63);
            return Pack(255, Shift(150, f), 32);
        }
        case Mat::Smoke:
            return Pack(Shift(68, v), Shift(68, v), Shift(74, v));
        case Mat::Steam:
            return Pack(Shift(190, v), Shift(200, v), Shift(212, v));
        default:
            return Pack(255, 0, 255);
        }
    }
}// namespace

class NoitaLikeGame : public Application {
    using Application::Application;

    // Components — own all sim/net state and the fixed-update loop.
    LockstepNetComponent* _netComp = nullptr;
    PlayerInputComponent* _inputComp = nullptr;

    // Rendering state.
    uint32_t _gridTex = 0;
    std::vector<uint32_t> _pixels;
    FontHandle _fontID = 0;

    // RmlUi HUD (read-only status readout), driven by an engine UIPageComponent.
    UIPageComponent* _hudPage = nullptr;
    Rml::Element* _elStatus = nullptr;
    Rml::Element* _elHp[2] = { nullptr, nullptr };
    Rml::Element* _elScore[2] = { nullptr, nullptr };
    Rml::Element* _elSlots[static_cast<int>(SpellType::Count)] = {};
    int _hudSpell = -1;
    int _hudHp[2] = { -1, -1 };
    std::string _hudScore[2];
    std::string _hudStatus;

    void OnInit() override {
        ComponentFactory::Register("LockstepNetComponent", [](GameObject* o, Deserializer& d) -> Component* {
            return new LockstepNetComponent(o);
        });
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        _pixels.assign(static_cast<size_t>(SandWorld::W) * SandWorld::H, 0);
        // Falling-sand grid: crisp per-cell pixels when scaled up → Nearest.
        _gridTex = GfxFactory::UploadTexture2D(
            reinterpret_cast<const uint8_t*>(_pixels.data()), SandWorld::W, SandWorld::H, TextureFilter::Nearest
        );

        // The component loads the document, shows it, and ties its lifetime to
        // the entity tree; this HUD only reads back element handles to update.
        _hudPage =
            static_cast<UIPageComponent*>(CreateGameObject()->AddComponent<UIPageComponent>("assets/ui/hud.rml"));
        if (_hudPage->GetDocument()) {
            _elStatus = _hudPage->GetElement("status");
            _elHp[0] = _hudPage->GetElement("p1hp");
            _elHp[1] = _hudPage->GetElement("p2hp");
            _elScore[0] = _hudPage->GetElement("p1score");
            _elScore[1] = _hudPage->GetElement("p2score");
            for (int i = 0; i < static_cast<int>(SpellType::Count); i++)
                _elSlots[i] = _hudPage->GetElement(fmt::format("slot{}", i));
        }

        // Lockstep session: owns net + sim, drives fixed-update loop via OnTick.
        auto* netObj = CreateGameObject();
        netObj->SetName("NetSession");
        netObj->AddComponent<LockstepNetComponent>();
        _netComp = netObj->GetComponent<LockstepNetComponent>();
        if (gcli.useRelay) {
            _netComp->StartRelay(gcli.mode, gcli.relayIp, gcli.relayPort, gcli.roomId, gcli.seed, gcli.delay);
        } else {
            _netComp->Start(gcli.mode, gcli.port, gcli.seed, gcli.delay, gcli.joinIp);
        }

        // Inspector: one entity per player slot.
        for (int i = 0; i < 2; i++) {
            auto* pObj = CreateGameObject();
            pObj->SetName("Player" + std::to_string(i));
            pObj->AddComponent<PlayerInspectorComponent>(
                &_netComp->GetSim().players[i], i, &_netComp->GetNet(), _netComp->StartedPtr()
            );
        }

        // Local input: samples hardware each fixed tick, exposes curSpell for the HUD.
        auto* inputObj = CreateGameObject();
        inputObj->SetName("LocalPlayer");
        inputObj->AddComponent<PlayerInputComponent>(&_netComp->GetSim(), &_netComp->GetNet());
        _inputComp = inputObj->GetComponent<PlayerInputComponent>();
        _netComp->SetInputComponent(_inputComp);
    }

    void RenderWorld() {
        const GameSim& sim = _netComp->GetSim();
        auto ws = Window::Get()->GetLogicalSize();
        auto dpi = Window::Get()->GetDPI();
        float sx = static_cast<float>(ws.width) / static_cast<float>(SandWorld::W);
        float sy = static_cast<float>(ws.height) / static_cast<float>(SandWorld::H);

        for (int i = 0; i < SandWorld::W * SandWorld::H; i++)
            _pixels[static_cast<size_t>(i)] = CellColor(sim.world.cells[static_cast<size_t>(i)], sim.tick);
        GfxFactory::UpdateTexture2D(
            _gridTex, reinterpret_cast<const uint8_t*>(_pixels.data()), SandWorld::W, SandWorld::H
        );
        GraphicsSubsystem::Get()->DrawTexturedQuad(
            ws.width * 0.5f,
            ws.height * 0.5f,
            static_cast<float>(ws.width),
            static_cast<float>(ws.height),
            0.0f,
            _gridTex,
            glm::vec4(1.0f)
        );

        static const glm::vec4 gbodyColors[2] = {
            { 0.25f, 0.9f, 1.0f, 1.0f },
            { 1.0f, 0.62f, 0.18f, 1.0f },
        };
        for (int i = 0; i < 2; i++) {
            const Player& p = sim.players[i];
            if (!p.alive) continue;
            float px = p.x * sx, py = p.y * sy;
            float pw = static_cast<float>(Player::HW * 2 + 1) * sx;
            float ph = static_cast<float>(Player::HH * 2 + 1) * sy;
            GraphicsSubsystem::Get()->DrawQuad(px, py, pw, ph, 0.0f, gbodyColors[i]);
            float frac = static_cast<float>(p.hp) / static_cast<float>(Player::MAX_HP);
            GraphicsSubsystem::Get()->DrawQuad(
                px, py - ph * 0.5f - 8.0f, pw + 8.0f, 4.0f, 0.0f, { 0.1f, 0.1f, 0.1f, 0.8f }
            );
            GraphicsSubsystem::Get()->DrawQuad(
                px - (pw + 8.0f) * 0.5f * (1.0f - frac),
                py - ph * 0.5f - 8.0f,
                (pw + 8.0f) * frac,
                4.0f,
                0.0f,
                { 1.0f - frac, frac, 0.15f, 0.9f }
            );
        }

        for (const Projectile& pr : sim.projectiles) {
            if (!pr.alive) continue;
            glm::vec4 col;
            float r = 3.0f;
            switch (pr.type) {
            case SpellType::SparkBolt:
                col = { 0.6f, 0.9f, 1.0f, 1.0f };
                break;
            case SpellType::Fireball:
                col = { 1.0f, 0.55f, 0.1f, 1.0f };
                r = 5.0f;
                break;
            case SpellType::Grenade:
                col = { 0.35f, 0.4f, 0.35f, 1.0f };
                r = 4.0f;
                break;
            case SpellType::WaterJet:
                col = { 0.3f, 0.6f, 1.0f, 1.0f };
                break;
            case SpellType::AcidFlask:
                col = { 0.5f, 0.95f, 0.25f, 1.0f };
                r = 4.0f;
                break;
            default:
                col = { 1.0f, 1.0f, 0.7f, 1.0f };
                break;
            }
            GraphicsSubsystem::Get()->DrawCircle(pr.x * sx, pr.y * sy, r, col);
        }

        // crosshair
        glm::vec2 m = InputSubsystem::Get()->GetMousePosition() / dpi;
        GraphicsSubsystem::Get()->DrawLine(m.x - 7, m.y, m.x + 7, m.y, { 1, 1, 1, 0.8f });
        GraphicsSubsystem::Get()->DrawLine(m.x, m.y - 7, m.x, m.y + 7, { 1, 1, 1, 0.8f });
    }

    void UpdateHud() {
        if (!_hudPage || !_hudPage->GetDocument()) return;
        const GameSim& sim = _netComp->GetSim();
        const LockstepNet& net = _netComp->GetNet();

        for (int i = 0; i < 2; i++) {
            const Player& p = sim.players[i];
            int hp = _netComp->IsStarted() ? p.hp : 100;
            if (_elHp[i] && hp != _hudHp[i]) {
                _elHp[i]->SetProperty("width", fmt::format("{}%", hp));
                _hudHp[i] = hp;
            }
            std::string score = fmt::format("{} / {}", p.kills, p.deaths);
            if (_elScore[i] && score != _hudScore[i]) {
                _elScore[i]->SetInnerRML(score);
                _hudScore[i] = score;
            }
        }

        int curSpell = _inputComp ? static_cast<int>(_inputComp->GetCurSpell()) : 0;
        if (_hudSpell != curSpell) {
            for (int i = 0; i < static_cast<int>(SpellType::Count); i++)
                if (_elSlots[i]) _elSlots[i]->SetClass("sel", i == curSpell);
            _hudSpell = curSpell;
        }

        if (_elStatus) {
            std::string s;
            if (net.state == LockstepNet::State::Connecting) {
#ifdef __EMSCRIPTEN__
                s = (net.mode == LockstepNet::Mode::Host) ? "waiting for player 2 via WebRTC ..."
                                                          : "connecting via WebRTC ...";
#else
                if (net.useRelay) {
                    s = (net.mode == LockstepNet::Mode::Host)
                            ? fmt::format(
                                  "waiting for player 2 via relay {}:{} (room {}) ...",
                                  gcli.relayIp,
                                  gcli.relayPort,
                                  gcli.roomId
                              )
                            : fmt::format(
                                  "connecting via relay {}:{} (room {}) ...", gcli.relayIp, gcli.relayPort, gcli.roomId
                              );
                } else {
                    s = (net.mode == LockstepNet::Mode::Host)
                            ? fmt::format("waiting for player 2 on UDP :{} ...", gcli.port)
                            : fmt::format("connecting to {}:{} ...", gcli.joinIp, gcli.port);
                }
#endif
            } else if (net.state == LockstepNet::State::Failed) {
                s = "network error: " + net.error;
            } else if (net.desync) {
                s = "DESYNC DETECTED - peers have diverged";
            } else if (net.mode == LockstepNet::Mode::Solo) {
#ifdef __EMSCRIPTEN__
                s = "solo sandbox";
#else
                s = "solo sandbox - run with --host / --join for 2P";
#endif
            } else {
                s = fmt::format(
                    "{} | ping {} ms | delay {} ticks{}",
                    net.mode == LockstepNet::Mode::Host ? "hosting" : "connected",
                    net.rttMs < 0 ? 0 : net.rttMs,
                    net.inputDelay,
                    _netComp->IsStalled() ? " | waiting for peer..." : ""
                );
            }
            if (s != _hudStatus) {
                _elStatus->SetInnerRML(s);
                _hudStatus = s;
            }
        }
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (_netComp->IsStarted()) {
            RenderWorld();
        } else {
            const LockstepNet& net = _netComp->GetNet();
            auto ws = Window::Get()->GetLogicalSize();
            GraphicsSubsystem::Get()->DrawQuad(
                ws.width * 0.5f,
                ws.height * 0.5f,
                static_cast<float>(ws.width),
                static_cast<float>(ws.height),
                0.0f,
                { 0.09f, 0.08f, 0.13f, 1.0f }
            );
            GraphicsSubsystem::Get()->DrawText(
                _fontID,
                net.state == LockstepNet::State::Failed ? ("Error: " + net.error) : "Waiting for connection...",
                static_cast<float>(ws.width) * 0.5f - 150.0f,
                static_cast<float>(ws.height) * 0.5f,
                1.0f,
                glm::vec4(1.0f)
            );
        }

        UpdateHud();

        // Netgraph: dial the inbound link emulator (J/K latency, N/M jitter,
        // U/I loss, O reset — the number row is spells) and draw it. Only
        // meaningful once networked; Solo has no peer traffic.
        if (_netComp->GetNet().mode != LockstepNet::Mode::Solo) {
            LockstepNet& net = _netComp->GetNet();
            ConditionerKeys keys;
            keys.latencyDown = Key::J;
            keys.latencyUp = Key::K;
            keys.jitterDown = Key::N;
            keys.jitterUp = Key::M;
            keys.lossDown = Key::U;
            keys.lossUp = Key::I;
            keys.reset = Key::O;
            DialConditioner(InputSubsystem::Get(), net.Conditioner(), keys);
            auto ws = Window::Get()->GetLogicalSize();
            DrawNetHud(
                GraphicsSubsystem::Get(),
                _fontID,
                net.Metrics(),
                net.Conditioner(),
                static_cast<float>(ws.width) - 258.0f,
                20.0f
            );
        }

        if (gcli.autotestTicks > 0 && _netComp->IsStarted()) {
            const GameSim& sim = _netComp->GetSim();
            if (sim.tick >= gcli.autotestTicks) {
                Log::Info(
                    "AUTOTEST tick={} checksum={:#010x} desync={}", sim.tick, sim.Checksum(), _netComp->GetNet().desync
                );
                _netComp->Shutdown();
                Quit();
            }
        }

        if (InputSubsystem::Get()->IsKeyDown(Key::ESCAPE)) {
            _netComp->Shutdown();
            Quit();
        }
    }
};

int main(int argc, char* argv[]) {
    gcli.seed = static_cast<uint32_t>(std::time(nullptr));
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--host") == 0) {
            gcli.mode = LockstepNet::Mode::Host;
            if (i + 1 < argc && argv[i + 1][0] != '-') gcli.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            gcli.mode = LockstepNet::Mode::Client;
            gcli.joinIp = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') gcli.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            gcli.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            gcli.delay = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--autotest") == 0 && i + 1 < argc) {
            gcli.autotestTicks = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--relay-host") == 0 && i + 3 < argc) {
            gcli.mode = LockstepNet::Mode::Host;
            gcli.useRelay = true;
            gcli.relayIp = argv[++i];
            gcli.relayPort = static_cast<uint16_t>(std::atoi(argv[++i]));
            gcli.roomId = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--relay-join") == 0 && i + 3 < argc) {
            gcli.mode = LockstepNet::Mode::Client;
            gcli.useRelay = true;
            gcli.relayIp = argv[++i];
            gcli.relayPort = static_cast<uint16_t>(std::atoi(argv[++i]));
            gcli.roomId = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        }
    }

    NoitaLikeGame game(
        {
            .windowTitle = "NoitaLike - 2P falling sand arena",
            .windowWidth = 960,
            .windowHeight = 540,
            .enableAudio = false,
            .useDefaultTextures = true,
            .useDefaultShaders = true,
        }
    );
    game.Run();
    return 0;
}
