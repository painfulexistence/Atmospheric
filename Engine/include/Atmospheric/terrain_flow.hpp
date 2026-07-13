#pragma once
#include "vertex.hpp"
#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
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
    float minWidth = 1.5f;// half-width floor at the river threshold
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
    float bankLift = 0.4f;// metres the surface sits above the sampled bed
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
