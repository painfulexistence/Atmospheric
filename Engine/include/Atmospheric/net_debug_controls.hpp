#pragma once
#include "input_subsystem.hpp"
#include "net_conditioner.hpp"

// Keybinds for DialConditioner. Defaults to the number row (1/2 latency,
// 3/4 jitter, 5/6 loss, 0 reset) — free in Deathmatch and HideAndSeek.
// MultiplayerSandbox binds the number row to spells, so it passes a
// free-letter map instead.
struct ConditionerKeys {
    Key latencyDown = Key::Num1, latencyUp = Key::Num2;
    Key jitterDown = Key::Num3, jitterUp = Key::Num4;
    Key lossDown = Key::Num5, lossUp = Key::Num6;
    Key reset = Key::Num0;
};

// DialConditioner — shared debug keybinds for the realtime examples' netgraph.
// Dials the inbound NetConditioner live so prediction/reconciliation/RTT/loss
// visibly respond on screen (works even in single-process --local loopback).
// Edge-triggered; call once per frame. Header-only (no .cpp): pure input glue
// shared across the examples, deliberately kept out of the presentational
// DrawNetHud renderer.
inline void DialConditioner(InputSubsystem* inp, NetConditioner& c, const ConditionerKeys& k = {}) {
    if (!inp) return;
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    if (inp->IsKeyPressed(k.latencyDown)) c.latencyMs = clampi(c.latencyMs - 10, 0, 1000);
    if (inp->IsKeyPressed(k.latencyUp)) c.latencyMs = clampi(c.latencyMs + 10, 0, 1000);
    if (inp->IsKeyPressed(k.jitterDown)) c.jitterMs = clampi(c.jitterMs - 5, 0, 500);
    if (inp->IsKeyPressed(k.jitterUp)) c.jitterMs = clampi(c.jitterMs + 5, 0, 500);
    if (inp->IsKeyPressed(k.lossDown)) c.lossPct = clampf(c.lossPct - 1.0f, 0.0f, 100.0f);
    if (inp->IsKeyPressed(k.lossUp)) c.lossPct = clampf(c.lossPct + 1.0f, 0.0f, 100.0f);
    if (inp->IsKeyPressed(k.reset)) {
        c.latencyMs = 0;
        c.jitterMs = 0;
        c.lossPct = 0.0f;
        c.dupPct = 0.0f;
    }
}
