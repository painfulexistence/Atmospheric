#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class GraphicsServer;

/// A single 2D point-light.
/// Ported from 2d-engine/src/fx/lighting.ts  PointLight interface.
struct PointLight2D {
    float x = 0, y = 0;
    float r = 1.0f, g = 0.9f, b = 0.7f;
    float radius    = 150.0f;
    float intensity = 1.0f;
};

/// Approximate 2D dynamic lighting.
///
/// Draws a semi-transparent dark overlay (ambient darkness) over the whole
/// screen, then draws soft-edged glowing circles at each light position using
/// translucent quads with additive-style colors.  This is a rasterisation
/// approximation; the full WebGPU/WGSL post-process version lives in
///   AtmosphericEngine/default_assets/shaders/lighting_2d.wgsl
/// and can replace this once native Dawn support lands.
///
/// Ported from 2d-engine/src/fx/lighting.ts  LightingSystem.
class LightingSystem2D {
public:
    /// Submit a dark ambient overlay and glowing light circles to the canvas
    /// draw queue.  Call this AFTER drawing the scene tiles/sprites so the
    /// overlay appears on top of everything but beneath the HUD.
    void Apply(GraphicsServer* gfx, int screenW, int screenH) const;

    // Ambient light colour (applied uniformly across the whole scene)
    float ambientR = 0.18f, ambientG = 0.20f, ambientB = 0.28f, ambientA = 1.0f;

    std::vector<PointLight2D> lights;
};
