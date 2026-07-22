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
//
// --local runs a single-process test mode: an embedded HideAndSeekAuthority is
// pumped in-process and this client talks to it over loopback, so you can move
// around and test without launching a separate server (you're alone until the
// other role would join — enough to exercise movement/prediction/rendering).
#include "Atmospheric.hpp"
#include "authority.hpp"
#include "client_net.hpp"
#include "render_view.hpp"
#include "sim_common.hpp"
#include <Atmospheric/net_debug_controls.hpp>
#include <Atmospheric/net_hud.hpp>

#include <chrono>
#include <cstring>

namespace {
    struct CliOptions {
        std::string serverIp = "127.0.0.1";
        uint16_t serverPort = 9100;
        proto::Role role = proto::Role::Hider;
        bool local = false;
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
    HideAndSeekAuthority _localAuthority;// only bound/pumped in --local mode
    FontHandle _fontID = 0;
    float _accum = 0.0f;
    uint32_t _tick = 0;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);
        if (gcli.local) {
            // Embed the authority in this process and talk to it over loopback.
            if (!_localAuthority.Bind(0)) {
                APP_ERROR("Failed to bind embedded authority");
                return;
            }
            gcli.serverIp = "127.0.0.1";
            gcli.serverPort = _localAuthority.BoundPort();
        }
        if (!_net.Connect(gcli.serverIp, gcli.serverPort, gcli.role)) {
            APP_ERROR("Failed to connect to {}:{}", gcli.serverIp, gcli.serverPort);
        }
    }

    void OnUpdate(float dt, float /*time*/) override {
        uint32_t nowMs = NowMs();
        if (gcli.local) _localAuthority.Pump();// drive the embedded server first
        _net.Pump(nowMs);

        auto* inp = InputSubsystem::Get();
        // Dial the inbound link emulator live (1/2 latency, 3/4 jitter, 5/6
        // loss, 0 reset) so the netgraph responds on screen — works in --local.
        DialConditioner(inp, _net.Conditioner());

        _accum += std::min(dt, 0.25f);
        while (_accum >= sim::kTickDt) {
            _accum -= sim::kTickDt;
            float dx = 0.0f, dy = 0.0f;
            if (inp->IsKeyDown(Key::A) || inp->IsKeyDown(Key::LEFT)) dx -= 1.0f;
            if (inp->IsKeyDown(Key::D) || inp->IsKeyDown(Key::RIGHT)) dx += 1.0f;
            if (inp->IsKeyDown(Key::W) || inp->IsKeyDown(Key::UP)) dy -= 1.0f;
            if (inp->IsKeyDown(Key::S) || inp->IsKeyDown(Key::DOWN)) dy += 1.0f;
            _net.SubmitInput(nowMs, _tick++, dx, dy);
        }

        RenderHideAndSeekView(_net, _fontID, nowMs);
        auto ws = Window::Get()->GetLogicalSize();
        DrawNetHud(GraphicsSubsystem::Get(), _fontID, _net.Metrics(), _net.Conditioner(), ws.width - 258.0f, 20.0f);

        if (inp->IsKeyDown(Key::ESCAPE)) Quit();
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
        } else if (std::strcmp(argv[i], "--local") == 0) {
            gcli.local = true;
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
