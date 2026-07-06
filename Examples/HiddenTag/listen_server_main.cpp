// HiddenTagListenServer — one player's own process hosts the authoritative
// simulation (HiddenTagAuthority) *and* plays as a normal client against it,
// contrasted with HiddenTagDedicatedServer, which has no attached player.
//
//   ./HiddenTagListenServer --role seeker [--port <n>]     (default 9100)
//   ./HiddenTagListenServer --role hider  [--port <n>]
//
// The other player connects normally with HiddenTagClient, pointed at
// whatever address reaches this process (see Examples/RelayServer if that
// address is behind NAT — the transport doesn't care whether the far end is
// a dedicated server or a listen server).
//
// This process's own local player talks to its embedded authority exactly
// like a remote client would: over loopback UDP via a normal ClientNet
// connected to 127.0.0.1. That's a deliberate simplicity tradeoff — see
// authority.hpp's header comment for why — rather than a special
// zero-latency path for the host.
//
// Controls: WASD / arrows move · ESC quit
#include "Atmospheric.hpp"
#include "authority.hpp"
#include "client_net.hpp"
#include "render_view.hpp"
#include "sim_common.hpp"

#include <chrono>
#include <cstring>

namespace {
    struct CliOptions {
        uint16_t port = 9100;
        proto::Role role = proto::Role::Seeker;
    } gcli;

    uint32_t NowMs() {
        using namespace std::chrono;
        static const steady_clock::time_point start = steady_clock::now();
        return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
    }
}// namespace

class HiddenTagListenServerGame : public Application {
    using Application::Application;

    HiddenTagAuthority _authority;
    ClientNet _net;
    FontHandle _fontID = 0;
    float _accum = 0.0f;
    uint32_t _tick = 0;

    void OnInit() override {
        GoScene("main", [this] { OnLoad(); });
    }

    void OnLoad() override {
        _fontID = GraphicsSubsystem::Get()->LoadFont("assets/fonts/NotoSans-SemiBold.ttf", 24.0f);

        if (!_authority.Bind(gcli.port)) {
            ConsoleSubsystem::Get()->Error(fmt::format("Failed to bind authority on UDP port {}", gcli.port));
            return;
        }
        ConsoleSubsystem::Get()->Info(fmt::format("Hosting on UDP :{}", _authority.BoundPort()));

        if (!_net.Connect("127.0.0.1", _authority.BoundPort(), gcli.role)) {
            ConsoleSubsystem::Get()->Error("Failed to connect local client to the embedded authority");
        }
    }

    void OnUpdate(float dt, float /*time*/) override {
        // Drive the authority first so this tick's own snapshot (received
        // below via _net.Pump) is already fresh.
        _authority.Pump();

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

        RenderHiddenTagView(_net, _fontID, nowMs);

        if (InputSubsystem::Get()->IsKeyDown(Key::ESCAPE)) Quit();
    }
};

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            gcli.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            std::string r = argv[++i];
            gcli.role = (r == "hider") ? proto::Role::Hider : proto::Role::Seeker;
        }
    }

    HiddenTagListenServerGame game(
        { .windowTitle = "HiddenTag (Listen Server)",
          .windowWidth = 800,
          .windowHeight = 600,
          .enableAudio = false,
          .useDefaultTextures = true,
          .useDefaultShaders = true }
    );
    game.Run();
    return 0;
}
