#pragma once
#include "vertex.hpp"
#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <memory>
#include <vector>

// Hydrology for the streamed terrain: derive a river network from the height
// field the tiles displace to.
//
// Rivers can't be evaluated per-tile in isolation — where water flows through
// a tile depends on everything uphill of it — so this runs ONCE on a coarse
// GLOBAL grid at load: sample the exact height source, compute D8 flow
// directions, accumulate drainage area downstream, then trace the
// high-accumulation cells into river polylines. The coarse grid only fixes the
// ROUTE; the ribbon mesh later resamples fine height along each polyline, so a
// 20-40 m/cell grid still yields smooth draped rivers.
//
// This is the procedural stand-in for a Gaea flow/rivers export: swap
// BuildRiverNetwork for a loader that reads Gaea's flow map and the rest of the
// pipeline (ribbon mesh, water shader) is identical.

struct RiverNode {
    glm::vec2 pos{ 0.0f };// world XZ (metres)
    float width = 0.0f;// channel half-width in metres (grows downstream)
    float flow = 0.0f;// normalized drainage [0,1] (for flow speed / depth)
    // Water-surface elevation (world metres), filled by BuildHydrology: the
    // river's smooth, monotonically-falling longitudinal profile. 0 = unset
    // (the ribbon then drapes on the terrain height instead).
    float surfaceY = 0.0f;
};

// One continuous river from a source down to the sea/map edge/confluence.
struct RiverPolyline {
    std::vector<RiverNode> nodes;// ordered downstream (source -> mouth)
};

struct RiverNetworkParams {
    int gridResolution = 512;// global grid per side (worldSize/gridResolution m per cell)
    // Min drainage (fraction of the whole grid) for a cell to count as river.
    // At 512^2 ~0.0004 ≈ 105 cells of catchment — streams and up; lower =
    // denser/finer network, higher = only major rivers. (Peak drainage on a
    // 10km FBm world is ~750 cells, so keep this well under ~0.003.)
    float riverThreshold = 0.0004f;
    int minNodes = 6;// drop stubby polylines shorter than this
    float widthScale = 14.0f;// metres of half-width at full drainage
    float minWidth = 6.0f;// half-width floor — also the carved channel's half-width, so keep it
                          // wide enough to read as a valley (and above the carve grid cell)
    int maxRivers = 64;// keep only the N largest (by total drainage) — bounds mesh cost
    int seaLevelPad = 0;// reserved
};

// Build the river network from a thread-safe height source (world metres ->
// [0,1]). worldSize is the full XZ extent (centred on origin); heightScale
// scales the normalized height to metres for slope-correct routing. Returns
// polylines in world XZ, ordered source->mouth, smoothed and ready to mesh.
std::vector<RiverPolyline> BuildRiverNetwork(
    const std::function<float(float, float)>& height01,
    float worldSize,
    float heightScale,
    const RiverNetworkParams& params
);

// Draped ribbon mesh for a river network. Each polyline becomes a triangle
// strip two vertices wide (±width about the centreline), with y sampled from
// the exact height source (+bankLift so it sits just above the bed) so the
// water hugs the terrain. UV.x runs 0..1 bank-to-bank; UV.y accumulates
// distance/uvMetresPerV downstream so the water shader can scroll foam/normals
// along the flow. Tangent points downstream (flow direction), bitangent across.
// Appends into verts/indices (indexed triangles, uint16). Widths are clamped so
// one river fits in a uint16 index space; huge networks are already bounded by
// RiverNetworkParams::maxRivers.
struct RiverMeshParams {
    float bankLift = 0.1f;// water surface above the waterline (small — the U meets it here)
    float uvMetresPerV = 8.0f;// world metres per V unit (foam/normal tiling along flow)
    float widthGain = 1.0f;// multiply node widths (art control)
};

void BuildRiverMesh(
    const std::vector<RiverPolyline>& rivers,
    const std::function<float(float, float)>& height01,
    float heightScale,
    const RiverMeshParams& params,
    std::vector<Vertex>& verts,
    std::vector<uint16_t>& indices
);

// ── River incision (erosion) ────────────────────────────────────────────────
// Without carving, the flat-ish water ribbon is draped over the raw bumpy
// terrain and the bumps poke through it. A real river cuts a channel: this
// rasterizes an incision-depth field from the network — deepest at the
// centreline, tapering out past the banks — that the terrain height source
// subtracts, so the water ends up sitting IN a valley with banks rising on
// either side. The carve grid is sampled bilinearly and is read-only after
// build (thread-safe for the worker-thread height source).
struct CarveParams {
    int gridResolution = 2048;// carve grid per side (~5 m/cell at 10 km — crisp banks)
    // The channel is a U cut to the RIVER WIDTH: deepest (bedDepth below the
    // water) at the centreline, rising back to exactly the water surface at
    // ±width — so the water ribbon (same width) fills it bank to bank and its
    // edges meet the ground instead of hanging over a flat-bottomed pool. Past
    // ±width the floor ramps above the terrain over bankBlend so nothing else
    // is carved.
    float bedDepth = 6.0f;// U-channel centre depth below the water surface (metres)
    float bankBlend = 1.5f;// taper past the waterline before the carve clears the terrain (* width)
};

// Bilinearly-sampled channel-FLOOR-elevation field (world metres). Stores the
// target floor, not a depth: the carved terrain is min(base, floor), which can
// never leave the ground above the floor no matter how steep the full-res base
// is between grid cells (a depth field can, which pokes the water). Cells with
// no river hold a sentinel far above any terrain, so the min is a no-op there.
class RiverCarveField {
public:
    static constexpr float NO_FLOOR = 1e9f;
    RiverCarveField(int n, float worldSize)
      : _n(n), _worldSize(worldSize), _floor(static_cast<size_t>(n) * n, NO_FLOOR) {
    }
    float SampleFloor(float wx, float wz) const;// metres, bilinear; huge outside rivers
    std::vector<float>& grid() {
        return _floor;
    }
    int n() const {
        return _n;
    }
    float worldSize() const {
        return _worldSize;
    }

private:
    int _n;
    float _worldSize;
    std::vector<float> _floor;
};

// Rivers + the carve field that incises them, built from a base height source.
struct TerrainHydrology {
    std::vector<RiverPolyline> rivers;
    std::shared_ptr<RiverCarveField> carve;
    // base height with the river channels subtracted, clamped to [0,1] — feed
    // this to StreamingTerrainProps::heightFn so tiles/colliders/scatter all
    // follow the carved valleys, and drape the river ribbons onto it.
    std::function<float(float, float)> carvedHeight01;
};

// One-shot: derive the network from base01, build the carve field, and return
// the carved height source. worldSize/heightScale match the terrain.
TerrainHydrology BuildHydrology(
    const std::function<float(float, float)>& base01,
    float worldSize,
    float heightScale,
    const RiverNetworkParams& network,
    const CarveParams& carve
);
