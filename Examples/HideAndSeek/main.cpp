// HideAndSeekClient — 1 Seeker vs 1 Hider, server-authoritative hide-and-tag.
//
//   ./HideAndSeekClient --role seeker --connect <ip> [port]
//   ./HideAndSeekClient --role hider  --connect <ip> [port]
//   (default port 9100; run HideAndSeekServer or HideAndSeekListenServer
//   separately — this client doesn't care which kind of authority it's
//   talking to, see authority.hpp)
//
// Unlike MultiplayerSandbox's peer-symmetric lockstep, this client does not
// simulate the other player at all — it has no information to simulate with
// beyond what the server chooses to send (see protocol.hpp / authority.hpp
// for the visibility rule). Its own movement is predicted locally
// (ClientNet::SubmitInput) and reconciled against the server's authoritative
// position as snapshots arrive; the other entity, when visible, is rendered
// via ClientNet's interpolated position (see render_view.hpp).
//
// Controls: WASD / arrows move · ESC quit
#include "Atmospheric.hpp"
#include "client_net.hpp"
#include "render_view.hpp"
#include "sim_common.hpp"

#include <chrono>
#include <cstring>

namespace {
    struct CliOptions {
        std::string serverIp = "127.0.0.1";
        uint16_t serverPort = 9100;
        proto::Role role = proto::Role::Hider;
    } gcli;

    uint32_t NowMs() {
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
    }
}// namespace

class HideAndSeekGame : public Application {
    using Application::Application;

    ClientNet _net;
    FontHandle _fontID = 0;
    float _accum = 0.0f;
    uint32_t _tick = 0;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);
        if (!_net.Connect(gcli.serverIp, gcli.serverPort, gcli.role)) {
            ConsoleSubsystem::Get()->Error(fmt::format("Failed to connect to {}:{}", gcli.serverIp, gcli.serverPort));
        }
    }

    void OnUpdate(float dt, float /*time*/) override {
        uint32_t nowMs = NowMs();
        _net.Pump(nowMs);

        _accum += std::min(dt, 0.25f);
        while (_accum >= sim::kTickDt) {
            _accum -= sim::kTickDt;
            auto* inp = InputSubsystem::Get();
            float dx = 0.0f, dy = 0.0f;
            if (inp->IsKeyDown(Key::A) || inp->IsKeyDown(Key::LEFT)) dx -= 1.0f;
            if (inp->IsKeyDown(Key::D) || inp->IsKeyDown(Key::RIGHT)) dx += 1.0f;
            if (inp->IsKeyDown(Key::W) || inp->IsKeyDown(Key::UP)) dy -= 1.0f;
            if (inp->IsKeyDown(Key::S) || inp->IsKeyDown(Key::DOWN)) dy += 1.0f;
            _net.SubmitInput(_tick++, dx, dy);
        }

        RenderHideAndSeekView(_net, _fontID, nowMs);

        if (InputSubsystem::Get()->IsKeyDown(Key::ESCAPE)) Quit();
    }
};

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            gcli.serverIp = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') gcli.serverPort = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            std::string r = argv[++i];
            gcli.role = (r == "seeker") ? proto::Role::Seeker : proto::Role::Hider;
        }
    }

    HideAndSeekGame game(
        { .windowTitle = "HideAndSeek",
          .windowWidth = 800,
          .windowHeight = 600,
          .enableAudio = false,
          .useDefaultTextures = true,
          .useDefaultShaders = true }
    );
    game.Run();
    return 0;
}
