// RelayServer — headless UDP relay for 2-player sessions.
//
// The deployment shape of UdpRelayServer: no window, no graphics, no audio —
// just the relay subsystem driven by a plain fixed-rate loop. Run it on any
// host both players can reach, and point LockstepNet at it with
// StartRelayHost / StartRelayClient using a shared room id.
//
//   ./RelayServer [--port <n>]     (default 9000)
//
// NOTE: this example drives the subsystem manually instead of going through
// Application, which currently always creates a window. Once Application
// grows a headless mode, this collapses to AddSubsystem<UdpRelayServer>().
#include <Atmospheric/udp_relay_server.hpp>
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
    uint16_t port = 9000;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = uint16_t(std::atoi(argv[++i]));
    }

    std::signal(SIGINT, [](int) { g_stop = true; });
#ifdef SIGTERM
    std::signal(SIGTERM, [](int) { g_stop = true; });
#endif

    UdpRelayServer relay;
    if (!relay.Start(port)) {
        spdlog::error("RelayServer: failed to bind UDP port {}", port);
        return 1;
    }
    spdlog::info("RelayServer: relaying on UDP :{} (Ctrl+C to stop)", relay.BoundPort());

    auto last = std::chrono::steady_clock::now();
    auto lastStats = last;
    int lastRooms = 0;
    while (!g_stop) {
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        relay.Process(dt);

        if (now - lastStats >= std::chrono::seconds(10)) {
            lastStats = now;
            if (relay.RoomCount() != lastRooms) {
                lastRooms = relay.RoomCount();
                spdlog::info("RelayServer: {} active room(s)", lastRooms);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    relay.Stop();
    spdlog::info("RelayServer: shut down");
    return 0;
}
