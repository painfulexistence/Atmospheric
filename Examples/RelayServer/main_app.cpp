// RelayServerApp — the same relay as RelayServer/main.cpp, but driven through
// a headless Application + RelaySubsystem instead of a bare hand-rolled loop.
//
// This is the "embed UdpRelay inside an Application-driven process" path
// documented in Atmospheric/udp_relay.hpp: worth it once a project wants the
// relay sharing a process with matchmaking/game logic (all ticked together
// through the same Application), or wants a live ImGui debug panel. For a
// relay that only ever needs to relay, RelayServer/main.cpp is simpler and
// has one less moving part — see its header comment for that tradeoff.
//
//   ./RelayServerApp [--port <n>]     (default 9000)
//
// NOTE: relies on AppConfig::headless, a recent addition still being shaken
// out (HeadlessSmokeTest exists but isn't yet registered with ctest — see
// Engine/CMakeLists.txt). If this crashes, that's the underlying headless
// path, not something specific to the relay.
#include <Atmospheric/application.hpp>
#include <Atmospheric/relay_subsystem.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>

namespace {
    std::atomic<bool> g_stopRequested{ false };
}

class RelayServerApp : public Application {
public:
    RelayServerApp(AppConfig config, uint16_t port) : Application(config), _port(port) {
        // Must happen in the constructor, before Run(): Run() calls Init() on
        // every subsystem in _subsystems, then OnInit() — a subsystem added
        // from inside OnInit() would miss that Init() pass entirely.
        _relay = AddSubsystem<RelaySubsystem>().get();
    }

    void OnInit() override {
        if (!_relay->Relay().Start(_port)) {
            spdlog::error("RelayServerApp: failed to bind UDP port {}", _port);
            _failed = true;
            return;
        }
        spdlog::info("RelayServerApp: relaying on UDP :{} (Ctrl+C to stop)", _relay->Relay().BoundPort());
    }

    void OnLoad() override {
        // No scene: headless mode never gates Update() on one.
    }

    void OnUpdate(float /*dt*/, float /*time*/) override {
        // Quit() only takes effect on the loop's next iteration (called here,
        // mid-tick, rather than from OnInit — see the comment there and in
        // Application::HeadlessLoop for why OnInit is too early).
        if (_failed || g_stopRequested) Quit();
    }

private:
    uint16_t _port;
    RelaySubsystem* _relay = nullptr;
    bool _failed = false;
};

int main(int argc, char* argv[]) {
    uint16_t port = 9000;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
    }

    std::signal(SIGINT, [](int) { g_stopRequested = true; });
#ifdef SIGTERM
    std::signal(SIGTERM, [](int) { g_stopRequested = true; });
#endif

    RelayServerApp app({ .headless = true }, port);
    app.Run();
    return 0;
}
