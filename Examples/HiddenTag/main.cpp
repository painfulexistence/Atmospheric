// HiddenTagClient — 1 Seeker vs 1 Hider, server-authoritative hide-and-tag.
//
//   ./HiddenTagClient --role seeker --connect <ip> [port]
//   ./HiddenTagClient --role hider  --connect <ip> [port]
//   (default port 9100; run HiddenTagServer separately)
//
// Unlike MultiplayerSandbox's peer-symmetric lockstep, this client does not
// simulate the other player at all — it has no information to simulate with
// beyond what the server chooses to send (see protocol.hpp / server_main.cpp
// for the visibility rule). Its own movement is predicted locally
// (ClientNet::SubmitInput) and reconciled against the server's authoritative
// position as snapshots arrive; the other entity, when visible, is rendered
// via ClientNet's interpolated position.
//
// Controls: WASD / arrows move · ESC quit
#include "Atmospheric.hpp"
#include "client_net.hpp"
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

class HiddenTagGame : public Application {
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

        RenderWorld(nowMs);

        if (InputSubsystem::Get()->IsKeyDown(Key::ESCAPE)) Quit();
    }

    void RenderWorld(uint32_t nowMs) {
        auto* gfx = GraphicsSubsystem::Get();
        auto ws = Window::Get()->GetLogicalSize();
        float sx = static_cast<float>(ws.width) / sim::kArenaW;
        float sy = static_cast<float>(ws.height) / sim::kArenaH;

        gfx->DrawQuad(
            ws.width * 0.5f,
            ws.height * 0.5f,
            static_cast<float>(ws.width),
            static_cast<float>(ws.height),
            0.0f,
            { 0.08f, 0.08f, 0.12f, 1.0f }
        );

        if (!_net.IsWelcomed()) {
            gfx->DrawText(_fontID, "Connecting to server...", 20.0f, 20.0f, 1.0f, glm::vec4(1.0f));
            return;
        }

        const bool isSeeker = _net.GetRole() == proto::Role::Seeker;
        const sim::Vec2 own = _net.GetOwnPos();

        // A visible vision-radius disc so the Seeker player can see the
        // boundary of what the server is willing to reveal to them.
        if (isSeeker) gfx->DrawCircle(own.x * sx, own.y * sy, sim::kSeekerVisionRadius * sx, { 1, 1, 1, 0.08f });

        if (_net.HasRemote()) {
            const sim::Vec2 rp = _net.GetRemotePos(nowMs);
            const glm::vec4 remoteColor =
                isSeeker ? glm::vec4(0.3f, 0.6f, 1.0f, 1.0f) : glm::vec4(1.0f, 0.35f, 0.3f, 1.0f);
            gfx->DrawCircle(rp.x * sx, rp.y * sy, 14.0f, remoteColor);
        }

        const glm::vec4 ownColor = isSeeker ? glm::vec4(1.0f, 0.35f, 0.3f, 1.0f) : glm::vec4(0.3f, 0.6f, 1.0f, 1.0f);
        gfx->DrawCircle(own.x * sx, own.y * sy, 14.0f, ownColor);

        gfx->DrawText(_fontID, isSeeker ? "SEEKER" : "HIDER", 20.0f, 20.0f, 1.0f, glm::vec4(1.0f));
        if (isSeeker && !_net.HasRemote())
            gfx->DrawText(_fontID, "Hider not in sight", 20.0f, 50.0f, 0.7f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
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

    HiddenTagGame game(
        { .windowTitle = "HiddenTag",
          .windowWidth = 800,
          .windowHeight = 600,
          .enableAudio = false,
          .useDefaultTextures = true,
          .useDefaultShaders = true }
    );
    game.Run();
    return 0;
}
