// DeathmatchClient — 2-player, server-authoritative, Quake-style arena shooter.
//
//   ./DeathmatchClient --connect <ip> [port]     (default 127.0.0.1:9200)
//
// Run DeathmatchServer (dedicated) and connect two clients. Every client is
// identical — deathmatch is symmetric, unlike HideAndSeek's Seeker/Hider.
//
// This client demonstrates the FPS-lineage client stack: it predicts its own
// movement and reconciles it against the server with exact rewind-replay (see
// client_net.hpp), interpolates the enemy in the past, and shows a cosmetic
// prediction of its own rockets. The server does the lag compensation the
// prediction here makes fair to shoot with — see authority.hpp.
//
// Controls: WASD / arrows move · mouse aim · LMB railgun · SPACE rocket · ESC quit
#include "Atmospheric.hpp"
#include "client_net.hpp"
#include "render_view.hpp"
#include "sim_common.hpp"

#include <chrono>
#include <cmath>
#include <cstring>

namespace {
    struct CliOptions {
        std::string serverIp = "127.0.0.1";
        uint16_t serverPort = 9200;
    } gcli;

    uint32_t NowMs() {
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
    }
}// namespace

class DeathmatchGame : public Application {
    using Application::Application;

    ClientNet _net;
    FontHandle _fontID = 0;
    float _accum = 0.0f;
    uint32_t _tick = 0;
    bool _pendingRail = false;
    bool _pendingRocket = false;

    // Railgun beam flash (purely cosmetic, in arena coords).
    float _beamTimer = 0.0f;
    sim::Vec2 _beamFrom, _beamTo;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);
        if (!_net.Connect(gcli.serverIp, gcli.serverPort)) {
            ConsoleSubsystem::Get()->Error(fmt::format("Failed to connect to {}:{}", gcli.serverIp, gcli.serverPort));
        }
    }

    void OnUpdate(float dt, float /*time*/) override {
        const uint32_t nowMs = NowMs();
        _net.Pump(nowMs);
        _net.UpdateCosmetic(dt);

        auto* inp = InputSubsystem::Get();
        // Edge-triggered fires latched between fixed ticks.
        if (inp->IsMouseButtonPressed()) _pendingRail = true;
        if (inp->IsKeyPressed(Key::SPACE)) _pendingRocket = true;

        auto ws = Window::Get()->GetLogicalSize();
        const float sx = static_cast<float>(ws.width) / sim::kArenaW;
        const float sy = static_cast<float>(ws.height) / sim::kArenaH;

        _accum += std::min(dt, 0.25f);
        while (_accum >= sim::kTickDt) {
            _accum -= sim::kTickDt;

            float mx = 0.0f, my = 0.0f;
            if (inp->IsKeyDown(Key::A) || inp->IsKeyDown(Key::LEFT)) mx -= 1.0f;
            if (inp->IsKeyDown(Key::D) || inp->IsKeyDown(Key::RIGHT)) mx += 1.0f;
            if (inp->IsKeyDown(Key::W) || inp->IsKeyDown(Key::UP)) my -= 1.0f;
            if (inp->IsKeyDown(Key::S) || inp->IsKeyDown(Key::DOWN)) my += 1.0f;

            // Aim from our own position toward the mouse, in arena coords.
            const glm::vec2 mouse = inp->GetMousePosition();
            const sim::Vec2 own = _net.GetOwnPos();
            const float aimx = mouse.x / sx - own.x;
            const float aimy = mouse.y / sy - own.y;

            const bool fireRail = _pendingRail;
            const bool fireRocket = _pendingRocket;
            _pendingRail = _pendingRocket = false;

            _net.SubmitInput(_tick++, mx, my, fireRail, fireRocket, aimx, aimy);

            if (fireRail) {
                const float len = std::sqrt(aimx * aimx + aimy * aimy);
                if (len > 1e-4f) {
                    _beamFrom = own;
                    _beamTo = { own.x + aimx / len * 2000.0f, own.y + aimy / len * 2000.0f };
                    _beamTimer = 0.08f;
                }
            }
        }

        RenderDeathmatchView(_net, _fontID, nowMs);

        if (_beamTimer > 0.0f) {
            _beamTimer -= dt;
            GraphicsSubsystem::Get()->DrawLine(
                _beamFrom.x * sx, _beamFrom.y * sy, _beamTo.x * sx, _beamTo.y * sy, { 0.6f, 0.9f, 1.0f, 0.9f }
            );
        }

        if (inp->IsKeyDown(Key::ESCAPE)) Quit();
    }
};

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            gcli.serverIp = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') gcli.serverPort = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
    }

    DeathmatchGame game(
        { .windowTitle = "Deathmatch",
          .windowWidth = 800,
          .windowHeight = 600,
          .enableAudio = false,
          .useDefaultTextures = true,
          .useDefaultShaders = true }
    );
    game.Run();
    return 0;
}
