// DeathmatchServer — DeathmatchAuthority (see authority.hpp) run as a
// standalone process with no attached player. A plain loop, not an
// Application: no GameObject/Scene/rendering needs, same reasoning as
// HideAndSeekServer and UdpRelay staying plain classes.
//
//   ./DeathmatchServer [--port <n>] [--favor-target]     (default port 9200)
//
// --favor-target flips rockets from favor-the-shooter (default) to
// favor-the-target lag compensation, so the difference is observable live.
#include "authority.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
    std::atomic<bool> g_stop{ false };
}

int main(int argc, char* argv[]) {
    uint16_t port = 9200;
    bool favorTarget = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--favor-target") == 0)
            favorTarget = true;
    }

    std::signal(SIGINT, [](int) { g_stop = true; });
#ifdef SIGTERM
    std::signal(SIGTERM, [](int) { g_stop = true; });
#endif

    DeathmatchAuthority authority;
    authority.favorShooterRockets = !favorTarget;
    if (!authority.Bind(port)) {
        spdlog::error("DeathmatchServer: failed to bind UDP port {}", port);
        return 1;
    }
    spdlog::info(
        "DeathmatchServer: listening on UDP :{} (rockets favor {}) — Ctrl+C to stop",
        authority.BoundPort(),
        favorTarget ? "target" : "shooter"
    );

    while (!g_stop) {
        authority.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    authority.Shutdown();
    spdlog::info("DeathmatchServer: shut down");
    return 0;
}
