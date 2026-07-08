#pragma once
#include "graphics_subsystem.hpp"// GraphicsSubsystem, FontHandle
#include "net_conditioner.hpp"
#include "net_metrics.hpp"

// DrawNetHud — a compact netgraph shared by every realtime example (lockstep,
// HideAndSeek, Deathmatch). Renders RTT, inbound loss, up/down bandwidth, and
// (when the model provides them) prediction error and pending-input depth,
// plus the live NetConditioner knobs so a dialed-in impairment is visible on
// screen next to its measured effect.
//
// Purely presentational: reads the two structs, draws via GraphicsSubsystem,
// mutates nothing. Fields that don't apply to a model are omitted
// (NetMetrics::predErr < 0 or pendingInputs < 0). Anchored at (x, y) in
// screen space. Lives in the engine (not per-example) because it needs
// GraphicsSubsystem and is identical across all three consumers.
void DrawNetHud(
    GraphicsSubsystem* gfx,
    FontHandle font,
    const NetMetrics& m,
    const NetConditioner& cond,
    float x,
    float y
);
