#include "Atmospheric.hpp"
#include "Atmospheric/rmlui_manager.hpp"
#include "game_sim.hpp"
#include "net_lockstep.hpp"
#include "components.hpp"

#include <cstring>
#include <ctime>
#ifndef __EMSCRIPTEN__
#include <glad/glad.h>
#else
#include <GLES3/gl3.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// NoitaLike: two-player networked falling-sand arena (deterministic lockstep).
//
//   ./NoitaLikeDemo                       solo sandbox
//   ./NoitaLikeDemo --host [port]         host a 2P game (player 0)
//   ./NoitaLikeDemo --join <ip> [port]    join a 2P game  (player 1)
//   options: --seed <n>  --delay <ticks: input delay, default 3 = 50 ms>
//
// Controls: A/D move · W/Space jump+levitate · mouse aim · LMB cast
//           1-7 select spell · ESC quit
// ─────────────────────────────────────────────────────────────────────────────

namespace {
struct CliOptions {
    LockstepNet::Mode mode = LockstepNet::Mode::Solo;
    std::string joinIp;
    uint16_t port = 7777;
    uint32_t seed = 0;
    int delay = 3;
    uint32_t autotestTicks = 0;// run N ticks headless-style, print checksum, quit
} g_cli;

inline uint32_t Pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24;
}

inline uint8_t Shift(int base, int delta) {
    return uint8_t(std::clamp(base + delta, 0, 255));
}

uint32_t CellColor(const Cell& c, uint32_t tick) {
    int v = int(c.shade & 31) - 16;
    switch (Mat(c.mat)) {
    case Mat::Empty: return Pack(24, 21, 33);
    case Mat::Stone: return Pack(Shift(86, v), Shift(86, v), Shift(94, v));
    case Mat::Dirt:  return Pack(Shift(106, v), Shift(70, v), Shift(42, v / 2));
    case Mat::Sand:  return Pack(Shift(218, v), Shift(182, v), Shift(102, v));
    case Mat::Wood:  return Pack(Shift(150, v), Shift(102, v), Shift(52, v / 2));
    case Mat::Water: return Pack(Shift(46, v / 2), Shift(112, v), Shift(222, v));
    case Mat::Oil:   return Pack(Shift(62, v / 2), Shift(50, v / 2), Shift(34, v / 2));
    case Mat::Acid:  return Pack(Shift(112, v), Shift(230, v), Shift(62, v));
    case Mat::Lava: {
        int f = int((tick + c.shade) & 15) * 5;
        return Pack(Shift(240, f / 2), Shift(96, v + f), 24);
    }
    case Mat::Fire: {
        int f = int((tick * 7 + c.shade) & 63);
        return Pack(255, Shift(150, f), 32);
    }
    case Mat::Smoke: return Pack(Shift(68, v), Shift(68, v), Shift(74, v));
    case Mat::Steam: return Pack(Shift(190, v), Shift(200, v), Shift(212, v));
    default: return Pack(255, 0, 255);
    }
}
}// namespace

class NoitaLikeGame : public Application {
    using Application::Application;

    // Components — own all sim/net state and the fixed-update loop.
    LockstepNetComponent* _netComp   = nullptr;
    PlayerInputComponent* _inputComp = nullptr;

    // Rendering state.
    GLuint gridTex = 0;
    std::vector<uint32_t> pixels;
    FontHandle fontID = 0;

    // RmlUi HUD elements.
    Rml::ElementDocument* hud = nullptr;
    Rml::Element* elStatus = nullptr;
    Rml::Element* elHp[2]    = { nullptr, nullptr };
    Rml::Element* elScore[2] = { nullptr, nullptr };
    Rml::Element* elSlots[int(SpellType::Count)] = {};
    int hudSpell = -1;
    int hudHp[2] = { -1, -1 };
    std::string hudScore[2];
    std::string hudStatus;

    void OnInit() override {
        ComponentFactory::Register("LockstepNetComponent",
          [](GameObject* o, Deserializer& d) -> Component* {
              return new LockstepNetComponent(o);
          });
        GoScene("main", [this]{ OnLoad(); });
    }

    void OnLoad() override {
        fontID = graphics.LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        pixels.assign(size_t(SandWorld::W) * SandWorld::H, 0);
        glGenTextures(1, &gridTex);
        glBindTexture(GL_TEXTURE_2D, gridTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SandWorld::W, SandWorld::H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        hud = RmlUiManager::Get()->LoadDocument("assets/ui/hud.rml");
        if (hud) {
            hud->Show();
            elStatus   = hud->GetElementById("status");
            elHp[0]    = hud->GetElementById("p1hp");
            elHp[1]    = hud->GetElementById("p2hp");
            elScore[0] = hud->GetElementById("p1score");
            elScore[1] = hud->GetElementById("p2score");
            for (int i = 0; i < int(SpellType::Count); i++)
                elSlots[i] = hud->GetElementById(fmt::format("slot{}", i));
        }

        // Lockstep session: owns net + sim, drives fixed-update loop via OnTick.
        auto* netObj = CreateGameObject();
        netObj->SetName("NetSession");
        netObj->AddComponent<LockstepNetComponent>();
        _netComp = netObj->GetComponent<LockstepNetComponent>();
        _netComp->Start(g_cli.mode, g_cli.port, g_cli.seed, g_cli.delay, g_cli.joinIp);

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
        auto ws  = GetWindow()->GetLogicalSize();
        auto dpi = GetWindow()->GetDPI();
        float sx = float(ws.width)  / float(SandWorld::W);
        float sy = float(ws.height) / float(SandWorld::H);

        for (int i = 0; i < SandWorld::W * SandWorld::H; i++)
            pixels[size_t(i)] = CellColor(sim.world.cells[size_t(i)], sim.tick);
        glBindTexture(GL_TEXTURE_2D, gridTex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SandWorld::W, SandWorld::H,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        graphics.DrawTexturedQuad(ws.width * 0.5f, ws.height * 0.5f,
                                  float(ws.width), float(ws.height), 0.0f,
                                  gridTex, glm::vec4(1.0f));

        static const glm::vec4 bodyColors[2] = {
            { 0.25f, 0.9f, 1.0f, 1.0f },
            { 1.0f,  0.62f, 0.18f, 1.0f },
        };
        for (int i = 0; i < 2; i++) {
            const Player& p = sim.players[i];
            if (!p.alive) continue;
            float px = p.x * sx, py = p.y * sy;
            float pw = float(Player::HW * 2 + 1) * sx;
            float ph = float(Player::HH * 2 + 1) * sy;
            graphics.DrawQuad(px, py, pw, ph, 0.0f, bodyColors[i]);
            float frac = float(p.hp) / float(Player::MAX_HP);
            graphics.DrawQuad(px, py - ph * 0.5f - 8.0f, pw + 8.0f, 4.0f, 0.0f,
                              { 0.1f, 0.1f, 0.1f, 0.8f });
            graphics.DrawQuad(px - (pw + 8.0f) * 0.5f * (1.0f - frac),
                              py - ph * 0.5f - 8.0f, (pw + 8.0f) * frac, 4.0f, 0.0f,
                              { 1.0f - frac, frac, 0.15f, 0.9f });
        }

        for (const Projectile& pr : sim.projectiles) {
            if (!pr.alive) continue;
            glm::vec4 col; float r = 3.0f;
            switch (pr.type) {
            case SpellType::SparkBolt: col = { 0.6f, 0.9f, 1.0f, 1.0f }; break;
            case SpellType::Fireball:  col = { 1.0f, 0.55f, 0.1f, 1.0f }; r = 5.0f; break;
            case SpellType::Grenade:   col = { 0.35f, 0.4f, 0.35f, 1.0f }; r = 4.0f; break;
            case SpellType::WaterJet:  col = { 0.3f, 0.6f, 1.0f, 1.0f }; break;
            case SpellType::AcidFlask: col = { 0.5f, 0.95f, 0.25f, 1.0f }; r = 4.0f; break;
            default:                   col = { 1.0f, 1.0f, 0.7f, 1.0f }; break;
            }
            graphics.DrawCircle(pr.x * sx, pr.y * sy, r, col);
        }

        //crosshair
        glm::vec2 m = input.GetMousePosition() / dpi;
        graphics.DrawLine(m.x - 7, m.y, m.x + 7, m.y, { 1, 1, 1, 0.8f });
        graphics.DrawLine(m.x, m.y - 7, m.x, m.y + 7, { 1, 1, 1, 0.8f });
    }

    void UpdateHud() {
        if (!hud) return;
        const GameSim&     sim = _netComp->GetSim();
        const LockstepNet& net = _netComp->GetNet();

        for (int i = 0; i < 2; i++) {
            const Player& p = sim.players[i];
            int hp = _netComp->IsStarted() ? p.hp : 100;
            if (elHp[i] && hp != hudHp[i]) {
                elHp[i]->SetProperty("width", fmt::format("{}%", hp));
                hudHp[i] = hp;
            }
            std::string score = fmt::format("{} / {}", p.kills, p.deaths);
            if (elScore[i] && score != hudScore[i]) {
                elScore[i]->SetInnerRML(score);
                hudScore[i] = score;
            }
        }

        int curSpell = _inputComp ? int(_inputComp->GetCurSpell()) : 0;
        if (hudSpell != curSpell) {
            for (int i = 0; i < int(SpellType::Count); i++)
                if (elSlots[i]) elSlots[i]->SetClass("sel", i == curSpell);
            hudSpell = curSpell;
        }

        if (elStatus) {
            std::string s;
            if (net.state == LockstepNet::State::Connecting) {
                s = (net.mode == LockstepNet::Mode::Host)
                  ? fmt::format("waiting for player 2 on UDP :{} ...", g_cli.port)
                  : fmt::format("connecting to {}:{} ...", g_cli.joinIp, g_cli.port);
            } else if (net.state == LockstepNet::State::Failed) {
                s = "network error: " + net.error;
            } else if (net.desync) {
                s = "DESYNC DETECTED - peers have diverged";
            } else if (net.mode == LockstepNet::Mode::Solo) {
                s = "solo sandbox - run with --host / --join for 2P";
            } else {
                s = fmt::format(
                  "{} | ping {} ms | delay {} ticks{}",
                  net.mode == LockstepNet::Mode::Host ? "hosting" : "connected",
                  net.rttMs < 0 ? 0 : net.rttMs, net.inputDelay,
                  _netComp->IsStalled() ? " | waiting for peer..." : ""
                );
            }
            if (s != hudStatus) { elStatus->SetInnerRML(s); hudStatus = s; }
        }
    }

    void OnUpdate(float dt, float /*time*/) override {
        if (_netComp->IsStarted()) {
            RenderWorld();
        } else {
            const LockstepNet& net = _netComp->GetNet();
            auto ws = GetWindow()->GetLogicalSize();
            graphics.DrawQuad(ws.width * 0.5f, ws.height * 0.5f,
                              float(ws.width), float(ws.height), 0.0f,
                              { 0.09f, 0.08f, 0.13f, 1.0f });
            graphics.DrawText(fontID,
              net.state == LockstepNet::State::Failed
                ? ("Error: " + net.error) : "Waiting for connection...",
              float(ws.width) * 0.5f - 150.0f, float(ws.height) * 0.5f,
              1.0f, glm::vec4(1.0f));
        }

        UpdateHud();

        if (g_cli.autotestTicks > 0 && _netComp->IsStarted()) {
            const GameSim& sim = _netComp->GetSim();
            if (sim.tick >= g_cli.autotestTicks) {
                console.Info(fmt::format("AUTOTEST tick={} checksum={:#010x} desync={}",
                  sim.tick, sim.Checksum(), _netComp->GetNet().desync));
                _netComp->Shutdown();
                Quit();
            }
        }

        if (input.IsKeyDown(Key::ESCAPE)) {
            _netComp->Shutdown();
            Quit();
        }
    }
};

int main(int argc, char* argv[]) {
    g_cli.seed = uint32_t(std::time(nullptr));
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--host") == 0) {
            g_cli.mode = LockstepNet::Mode::Host;
            if (i + 1 < argc && argv[i + 1][0] != '-') g_cli.port = uint16_t(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--join") == 0 && i + 1 < argc) {
            g_cli.mode = LockstepNet::Mode::Client;
            g_cli.joinIp = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') g_cli.port = uint16_t(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            g_cli.seed = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--delay") == 0 && i + 1 < argc) {
            g_cli.delay = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--autotest") == 0 && i + 1 < argc) {
            g_cli.autotestTicks = uint32_t(std::atoi(argv[++i]));
        }
    }

    NoitaLikeGame game({
      .windowTitle = "NoitaLike - 2P falling sand arena",
      .windowWidth = 960,
      .windowHeight = 540,
      .enableAudio = false,
      .useDefaultTextures = true,
      .useDefaultShaders = true,
    });
    game.Run();
    return 0;
}
