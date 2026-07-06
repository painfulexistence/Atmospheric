// HideAndSeekServer — HideAndSeekAuthority (see authority.hpp for the
// full rationale) run as a standalone process with no attached player.
//
// A plain loop, not an Application: this process has no GameObject/Scene/
// rendering needs, so the AddSubsystem<T>/headless Application machinery
// would only add an unproven dependency (see Examples/RelayServer's
// RelayServer-vs-RelayServerApp split for the same reasoning applied there).
// Contrast with HideAndSeekListenServer, which embeds the same
// HideAndSeekAuthority inside a windowed Application instead.
//
//   ./HideAndSeekServer [--port <n>]     (default 9100)
#include "authority.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
    std::atomic<bool> g_stopRequested{ false };
}

int main(int argc, char* argv[]) {
    uint16_t port = 9100;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
    }

    std::signal(SIGINT, [](int) { g_stopRequested = true; });
#ifdef SIGTERM
    std::signal(SIGTERM, [](int) { g_stopRequested = true; });
#endif

    HideAndSeekAuthority authority;
    if (!authority.Bind(port)) {
        spdlog::error("HideAndSeekServer: failed to bind UDP port {}", port);
        return 1;
    }
    spdlog::info("HideAndSeekServer: listening on UDP :{} (Ctrl+C to stop)", authority.BoundPort());

    while (!g_stopRequested) {
        authority.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    authority.Shutdown();
    spdlog::info("HideAndSeekServer: shut down");
    return 0;
}
