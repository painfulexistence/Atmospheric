#include "lighting_2d.hpp"
#include "graphics_server.hpp"
#include <algorithm>
#include <cmath>

void LightingSystem2D::Apply(GraphicsServer* gfx, int screenW, int screenH) const {
    // ---------------------------------------------------------------------------
    // 1. Dark ambient overlay
    // ---------------------------------------------------------------------------
    float ambientBrightness = std::max({ambientR * ambientA,
                                        ambientG * ambientA,
                                        ambientB * ambientA});
    float darkness = std::max(0.0f, 1.0f - ambientBrightness * 1.5f);

    if (darkness > 0.01f) {
        gfx->DrawQuad(screenW * 0.5f, screenH * 0.5f, (float)screenW, (float)screenH, 0.0f,
                      glm::vec4(ambientR * 0.05f,
                                ambientG * 0.05f,
                                ambientB * 0.05f,
                                darkness));
    }

    // ---------------------------------------------------------------------------
    // 2. Soft light circles per point-light
    // ---------------------------------------------------------------------------
    // Draw rings from innermost (bright) to outermost (transparent) using a
    // quadratic falloff so the centre is clearly the brightest point.
    // 16 rings gives a smooth-enough gradient without being too costly.
    static const int RINGS = 16;

    for (const auto& light : lights) {
        float clampedIntensity = std::min(light.intensity, 2.0f);

        for (int k = 0; k < RINGS; k++) {
            // t = 0 → innermost ring, t = 1 → outermost ring
            float t     = (float)(k + 1) / RINGS;
            float rad   = light.radius * t;
            // Quadratic falloff: brightest at centre, fades quickly outward
            float alpha = clampedIntensity * (1.0f - t) * (1.0f - t) * 0.30f;
            if (alpha < 0.003f) continue;

            // DrawQuad(x, y, ...) centres the quad on (x, y)
            gfx->DrawQuad(
                light.x, light.y,
                rad * 2.0f, rad * 2.0f,
                0.0f,
                glm::vec4(light.r, light.g, light.b, alpha));
        }
    }
}
