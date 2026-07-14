#pragma once
#include "vertex.hpp"
#include <cstdint>
#include <glm/vec3.hpp>
#include <vector>

// Reusable "ribbon along a path" mesher — a flat triangle strip two vertices
// wide, offset ±halfWidth about a centreline in the strip's local horizontal
// plane. Pairs with Spline<glm::vec3> (sample it densely, then feed the points
// here) for roads, trails, river beds, trails-of-particles, etc.
//
// The caller supplies the centreline already positioned in world space (so it
// can drape onto terrain, arc through the air, whatever) plus a half-width per
// point. UVs: x runs 0..1 across the ribbon, y accumulates world distance /
// uvMetresPerV along it (for scrolling/tiling). Normal is up; tangent points
// along the ribbon (forward), bitangent across. Appends into verts/indices
// (indexed triangles, uint16); one ribbon is capped at 32k centreline points.
void BuildRibbonMesh(
    const std::vector<glm::vec3>& centreline,
    const std::vector<float>& halfWidths,
    float uvMetresPerV,
    std::vector<Vertex>& verts,
    std::vector<uint16_t>& indices
);
