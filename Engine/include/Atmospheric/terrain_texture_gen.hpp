#pragma once
#include "globals.hpp"
#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <vector>

// Procedural stand-ins for a Gaea/WorldCreator texture export.
//
// Everything here produces the SAME artifacts a terrain-authoring tool would:
// tileable detail layers (albedo + tangent-space normal) and per-tile RGBA
// splat weights. Swapping in real exports later means replacing these calls
// with file loads — the streamer/shader plumbing is identical either way.
//
// Generate* functions synthesize a seamlessly tiling material at `res`^2 from
// periodic FBm value noise, upload both maps via AssetManager (REPEAT wrap +
// mips), and return the handles. Call once at init from the main thread (GL
// uploads); a 512^2 layer takes a few milliseconds.
//
// DefaultSplat is a thread-safe weight generator for StreamingTerrainProps::
// splatFn: slope-driven rock, altitude-driven snow with a noisy snowline,
// noise-broken dirt/scree patches, grass as the remainder. Weight order
// matches the layer order {grass, rock, dirt, snow} = RGBA.
namespace TerrainTextureGen {

    struct DetailLayer {
        TextureHandle albedo;
        TextureHandle normal;
    };

    DetailLayer GenerateGrass(int res = 512, uint32_t seed = 11);
    DetailLayer GenerateRock(int res = 512, uint32_t seed = 22);
    DetailLayer GenerateDirt(int res = 512, uint32_t seed = 33);
    DetailLayer GenerateSnow(int res = 512, uint32_t seed = 44);

    struct SplatParams {
        float heightScale = 500.0f;// metres of displacement for height 1.0 (slope units)
        float rockSlopeStart = 0.55f;// rise/run where rock starts winning
        float rockSlopeFull = 1.05f;// ... and where it fully owns the texel
        float snowStart = 0.62f;// normalized height where snow begins
        float snowFull = 0.78f;// ... and reaches full cover
        float dirtAmount = 0.55f;// 0..1 strength of the worn dirt/scree patches
        uint32_t seed = 7;
    };

    // res*res*4 bytes, RGBA = {grass, rock, dirt, snow} weights over the world
    // rect. height01 must be the streamer's exact height source (thread-safe).
    std::vector<unsigned char> DefaultSplat(
        glm::vec2 worldMin,
        glm::vec2 worldMax,
        int res,
        const std::function<float(float, float)>& height01,
        const SplatParams& params
    );

}// namespace TerrainTextureGen
