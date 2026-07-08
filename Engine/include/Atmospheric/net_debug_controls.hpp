#pragma once
#include "input_subsystem.hpp"
#include "net_conditioner.hpp"

// DialConditioner — shared debug keybinds for the realtime examples' netgraph.
// Dials the inbound NetConditioner live so prediction/reconciliation/RTT/loss
// visibly respond on screen (works even in single-process --local loopback):
//
//   1 / 2   latency  -/+ 10 ms        3 / 4   jitter  -/+ 5 ms
//   5 / 6   loss     -/+ 1 %          0       reset to a clean link
//
// Edge-triggered; call once per frame. Header-only (no .cpp): pure input glue
// shared across the examples, deliberately kept out of the presentational
// DrawNetHud renderer.
inline void DialConditioner(InputSubsystem* inp, NetConditioner& c) {
    if (!inp) return;
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    if (inp->IsKeyPressed(Key::Num1)) c.latencyMs = clampi(c.latencyMs - 10, 0, 1000);
    if (inp->IsKeyPressed(Key::Num2)) c.latencyMs = clampi(c.latencyMs + 10, 0, 1000);
    if (inp->IsKeyPressed(Key::Num3)) c.jitterMs = clampi(c.jitterMs - 5, 0, 500);
    if (inp->IsKeyPressed(Key::Num4)) c.jitterMs = clampi(c.jitterMs + 5, 0, 500);
    if (inp->IsKeyPressed(Key::Num5)) c.lossPct = clampf(c.lossPct - 1.0f, 0.0f, 100.0f);
    if (inp->IsKeyPressed(Key::Num6)) c.lossPct = clampf(c.lossPct + 1.0f, 0.0f, 100.0f);
    if (inp->IsKeyPressed(Key::Num0)) {
        c.latencyMs = 0;
        c.jitterMs = 0;
        c.lossPct = 0.0f;
        c.dupPct = 0.0f;
    }
}
