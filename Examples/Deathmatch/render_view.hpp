#pragma once
#include "Atmospheric.hpp"
#include "client_net.hpp"
#include "sim_common.hpp"

// Top-down rendering of what a ClientNet currently knows. The view is 2D, but
// the netcode is the Quake/Source FPS lineage (server-authoritative prediction
// + reconciliation + lag compensation) regardless of camera — the lesson is
// the network model, not the projection.
inline void RenderDeathmatchView(const ClientNet& net, FontHandle fontID, uint32_t nowMs) {
    auto* gfx = GraphicsSubsystem::Get();
    auto ws = Window::Get()->GetLogicalSize();
    const float sx = static_cast<float>(ws.width) / sim::kArenaW;
    const float sy = static_cast<float>(ws.height) / sim::kArenaH;

    // Arena background.
    gfx->DrawQuad(
        ws.width * 0.5f,
        ws.height * 0.5f,
        static_cast<float>(ws.width),
        static_cast<float>(ws.height),
        0.0f,
        { 0.07f, 0.08f, 0.10f, 1.0f }
    );

    // Occluder wall — the cover that makes favor-the-shooter's downside visible.
    gfx->DrawQuad(
        (sim::kWallX + sim::kWallW * 0.5f) * sx,
        (sim::kWallY + sim::kWallH * 0.5f) * sy,
        sim::kWallW * sx,
        sim::kWallH * sy,
        0.0f,
        { 0.45f, 0.45f, 0.5f, 1.0f }
    );

    if (!net.IsWelcomed()) {
        gfx->DrawText(fontID, "Connecting to server...", 20.0f, 20.0f, 1.0f, glm::vec4(1.0f));
        return;
    }

    // Rockets: authoritative (what the server says exists, dodgeable) plus our
    // own cosmetic prediction (shown leaving the barrel before the server's
    // copy is replicated back).
    for (const auto& r : net.AuthoritativeRockets()) {
        gfx->DrawCircle(r.pos.x * sx, r.pos.y * sy, sim::kRocketRadius * sx, { 1.0f, 0.6f, 0.15f, 1.0f });
    }
    for (const auto& c : net.CosmeticRockets()) {
        gfx->DrawCircle(c.pos.x * sx, c.pos.y * sy, sim::kRocketRadius * sx, { 1.0f, 0.8f, 0.4f, 0.5f });
    }

    // Enemy (interpolated in the past; only present when alive & seen).
    if (net.HasEnemy()) {
        const sim::Vec2 e = net.GetEnemyPos(nowMs);
        gfx->DrawCircle(e.x * sx, e.y * sy, sim::kPlayerRadius * sx, { 1.0f, 0.35f, 0.3f, 1.0f });
    }

    // Self (predicted + reconciled).
    const sim::Vec2 own = net.GetOwnPos();
    const glm::vec4 selfColor = net.IsAlive() ? glm::vec4(0.3f, 0.6f, 1.0f, 1.0f) : glm::vec4(0.3f, 0.3f, 0.35f, 1.0f);
    gfx->DrawCircle(own.x * sx, own.y * sy, sim::kPlayerRadius * sx, selfColor);

    // HUD.
    gfx->DrawText(
        fontID,
        "HP " + std::to_string(net.GetHealth()),
        20.0f,
        20.0f,
        0.9f,
        net.IsAlive() ? glm::vec4(1.0f) : glm::vec4(1.0f, 0.4f, 0.4f, 1.0f)
    );
    gfx->DrawText(
        fontID,
        "You " + std::to_string(net.GetScore()) + "  -  " + std::to_string(net.GetEnemyScore()) + " Them",
        20.0f,
        50.0f,
        0.8f,
        glm::vec4(0.85f, 0.85f, 0.85f, 1.0f)
    );
    if (!net.IsAlive())
        gfx->DrawText(fontID, "DOWNED — respawning...", 20.0f, 84.0f, 0.9f, glm::vec4(1.0f, 0.5f, 0.5f, 1.0f));
    gfx->DrawText(
        fontID,
        "LMB railgun   SPACE rocket   WASD move   ESC quit",
        20.0f,
        ws.height - 34.0f,
        0.6f,
        glm::vec4(0.6f, 0.6f, 0.65f, 1.0f)
    );
}
