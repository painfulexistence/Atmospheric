#pragma once
#include "Atmospheric.hpp"
#include "client_net.hpp"
#include "sim_common.hpp"

// Rendering shared by HiddenTagClient and HiddenTagListenServer — both are
// just "a window showing what ClientNet currently knows", regardless of
// whether the ClientNet on this process is talking to a separate dedicated
// server or to a HiddenTagAuthority embedded in this same process.
inline void RenderHiddenTagView(const ClientNet& net, FontHandle fontID, uint32_t nowMs) {
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

    if (!net.IsWelcomed()) {
        gfx->DrawText(fontID, "Connecting to server...", 20.0f, 20.0f, 1.0f, glm::vec4(1.0f));
        return;
    }

    const bool isSeeker = net.GetRole() == proto::Role::Seeker;
    const sim::Vec2 own = net.GetOwnPos();

    // A visible vision-radius disc so the Seeker player can see the boundary
    // of what the server is willing to reveal to them.
    if (isSeeker) gfx->DrawCircle(own.x * sx, own.y * sy, sim::kSeekerVisionRadius * sx, { 1, 1, 1, 0.08f });

    if (net.HasRemote()) {
        const sim::Vec2 rp = net.GetRemotePos(nowMs);
        const glm::vec4 remoteColor = isSeeker ? glm::vec4(0.3f, 0.6f, 1.0f, 1.0f) : glm::vec4(1.0f, 0.35f, 0.3f, 1.0f);
        gfx->DrawCircle(rp.x * sx, rp.y * sy, 14.0f, remoteColor);
    }

    const glm::vec4 ownColor = isSeeker ? glm::vec4(1.0f, 0.35f, 0.3f, 1.0f) : glm::vec4(0.3f, 0.6f, 1.0f, 1.0f);
    gfx->DrawCircle(own.x * sx, own.y * sy, 14.0f, ownColor);

    gfx->DrawText(fontID, isSeeker ? "SEEKER" : "HIDER", 20.0f, 20.0f, 1.0f, glm::vec4(1.0f));
    if (isSeeker && !net.HasRemote())
        gfx->DrawText(fontID, "Hider not in sight", 20.0f, 50.0f, 0.7f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
}
