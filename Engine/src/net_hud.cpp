#include "net_hud.hpp"

#include <cstdio>
#include <string>

namespace {
    // Quick red/amber/green reading for latency- and loss-like quantities.
    glm::vec4 RatingColor(float value, float goodBelow, float badAbove) {
        if (value < goodBelow) return glm::vec4(0.55f, 0.9f, 0.55f, 1.0f); // green
        if (value < badAbove) return glm::vec4(0.95f, 0.85f, 0.45f, 1.0f); // amber
        return glm::vec4(0.95f, 0.45f, 0.42f, 1.0f);                       // red
    }

    std::string Fmt(const char* f, float v) {
        char buf[64];
        std::snprintf(buf, sizeof buf, f, static_cast<double>(v));
        return buf;
    }
}// namespace

void DrawNetHud(
    GraphicsSubsystem* gfx,
    FontHandle font,
    const NetMetrics& m,
    const NetConditioner& cond,
    float x,
    float y
) {
    if (!gfx) return;

    const float scale = 0.6f;
    const float lh = 22.0f;    // line height
    const glm::vec4 label(0.72f, 0.76f, 0.82f, 1.0f);
    const glm::vec4 dim(0.6f, 0.63f, 0.68f, 1.0f);

    // Count rows so the backing panel is sized to content.
    int rows = 3;// title + RTT + loss
    rows += 1;   // bandwidth
    if (m.predErrM >= 0.0f) rows += 1;
    if (m.pendingInputs >= 0) rows += 1;
    rows += 1;// conditioner knobs line

    gfx->DrawRect(x - 8.0f, y - 6.0f, 250.0f, lh * rows + 10.0f, glm::vec4(0.05f, 0.06f, 0.08f, 0.62f));

    float row = y;
    gfx->DrawText(font, "NET", x, row, scale, glm::vec4(0.85f, 0.9f, 1.0f, 1.0f));
    row += lh;

    gfx->DrawText(font, "rtt", x, row, scale, label);
    gfx->DrawText(font, Fmt("%.0f ms", m.rttMs), x + 60.0f, row, scale, RatingColor(m.rttMs, 60.0f, 120.0f));
    row += lh;

    gfx->DrawText(font, "loss", x, row, scale, label);
    gfx->DrawText(font, Fmt("%.1f %%", m.lossPct), x + 60.0f, row, scale, RatingColor(m.lossPct, 1.0f, 5.0f));
    row += lh;

    gfx->DrawText(font, "bw", x, row, scale, label);
    gfx->DrawText(
        font,
        Fmt("%.0f", m.kbpsIn) + "/" + Fmt("%.0f", m.kbpsOut) + " kbit dn/up",
        x + 60.0f,
        row,
        scale,
        dim
    );
    row += lh;

    if (m.predErrM >= 0.0f) {
        gfx->DrawText(font, "predErr", x, row, scale, label);
        // Millimetres read better than a tiny fractional metre. Green when the
        // rewind-replay reconciliation leaves ~no residual.
        gfx->DrawText(
            font,
            Fmt("%.0f mm", m.predErrM * 1000.0f),
            x + 60.0f,
            row,
            scale,
            RatingColor(m.predErrM, 0.05f, 0.30f)
        );
        row += lh;
    }

    if (m.pendingInputs >= 0) {
        gfx->DrawText(font, "pending", x, row, scale, label);
        gfx->DrawText(font, std::to_string(m.pendingInputs) + " inputs", x + 60.0f, row, scale, dim);
        row += lh;
    }

    // The live impairment knobs, so a dialed setting is visible next to its
    // measured effect above. Amber tint when anything is active.
    const bool active = cond.Active();
    char sim[96];
    std::snprintf(
        sim,
        sizeof sim,
        "sim %dms +/-%d  loss %.0f%%  dup %.0f%%",
        cond.latencyMs,
        cond.jitterMs,
        static_cast<double>(cond.lossPct),
        static_cast<double>(cond.dupPct)
    );
    gfx->DrawText(font, sim, x, row, scale * 0.9f, active ? glm::vec4(0.95f, 0.8f, 0.4f, 1.0f) : dim);
}
