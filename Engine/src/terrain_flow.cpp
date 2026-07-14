#include "terrain_flow.hpp"
#include "spline.hpp"
#include "spline_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>

// D8 flow routing + drainage accumulation on a coarse global grid, then trace
// the high-drainage cells into smoothed river polylines. Everything is plain
// CPU math so results are deterministic per (heightFn, params).

namespace {

    // 8-neighbour offsets (E, NE, N, NW, W, SW, S, SE) and their step lengths
    // (diagonals are sqrt(2) apart) for steepest-descent slope.
    constexpr int DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
    constexpr int DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    const float DLEN[8] = { 1.0f, 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.41421356f };

    inline int Idx(int x, int y, int n) {
        return y * n + x;
    }

}// namespace

std::vector<RiverPolyline> BuildRiverNetwork(
    const std::function<float(float, float)>& height01,
    float worldSize,
    float heightScale,
    const RiverNetworkParams& params
) {
    const int n = std::max(16, params.gridResolution);
    const float cell = worldSize / static_cast<float>(n);// metres per cell
    const float half = 0.5f * worldSize;

    auto worldOf = [&](int x, int y) { return glm::vec2((x + 0.5f) * cell - half, (y + 0.5f) * cell - half); };

    // ── Sample the height field on the global grid (metres) ──────────────────
    std::vector<float> h(static_cast<size_t>(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const glm::vec2 w = worldOf(x, y);
            h[Idx(x, y, n)] = height01(w.x, w.y) * heightScale;
        }


    // ── Depression filling (priority-flood + epsilon, Barnes 2014) ───────────
    // An un-eroded FBm surface is riddled with local pits that trap water and
    // fragment rivers into stubs. Flood inward from the map edge, raising each
    // pit cell to just above the lowest lip it can spill over, so every cell
    // ends up with a monotone downhill path to the edge — long, connected
    // rivers instead of hundreds of disconnected basins. The epsilon keeps a
    // gradient across filled flats so flow never stalls. `filled` is what the
    // routing below uses; `h` (true height) still drives the ribbon draping.
    const float eps = 1e-3f;// metres — below visible relief, above float noise
    std::vector<float> filled(static_cast<size_t>(n) * n, std::numeric_limits<float>::max());
    {
        struct Spill {
            float z;
            int idx;
            bool operator>(const Spill& o) const {
                return z > o.z;
            }
        };
        std::priority_queue<Spill, std::vector<Spill>, std::greater<Spill>> open;
        std::vector<uint8_t> closed(static_cast<size_t>(n) * n, 0);
        auto push = [&](int x, int y) {
            const int i = Idx(x, y, n);
            filled[i] = h[i];
            closed[i] = 1;
            open.push({ h[i], i });
        };
        for (int x = 0; x < n; ++x) {
            push(x, 0);
            push(x, n - 1);
        }
        for (int y = 1; y < n - 1; ++y) {
            push(0, y);
            push(n - 1, y);
        }
        while (!open.empty()) {
            const Spill c = open.top();
            open.pop();
            const int cx = c.idx % n, cy = c.idx / n;
            for (int k = 0; k < 8; ++k) {
                const int nx = cx + DX[k], ny = cy + DY[k];
                if (nx < 0 || ny < 0 || nx >= n || ny >= n) continue;
                const int ni = Idx(nx, ny, n);
                if (closed[ni]) continue;
                filled[ni] = std::max(h[ni], filled[c.idx] + eps);// raise if below spill
                closed[ni] = 1;
                open.push({ filled[ni], ni });
            }
        }
    }

    // ── D8: each cell drains to its steepest strictly-lower neighbour ────────
    // down[i] = flat index of the downstream cell, or -1 for a pit / edge sink.
    // Routes on the FILLED surface so pits don't dead-end the flow.
    std::vector<int> down(static_cast<size_t>(n) * n, -1);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const int i = Idx(x, y, n);
            const float hi = filled[i];
            float bestSlope = 0.0f;
            int best = -1;
            for (int k = 0; k < 8; ++k) {
                const int nx = x + DX[k], ny = y + DY[k];
                if (nx < 0 || ny < 0 || nx >= n || ny >= n) {
                    // Off-grid neighbour = the sea beyond the world edge: a real
                    // downhill sink, so border cells can terminate a river.
                    const float slope = hi / (cell * DLEN[k]);
                    if (slope > bestSlope) {
                        bestSlope = slope;
                        best = -2;// edge sink sentinel
                    }
                    continue;
                }
                const float slope = (hi - filled[Idx(nx, ny, n)]) / (cell * DLEN[k]);
                if (slope > bestSlope) {
                    bestSlope = slope;
                    best = Idx(nx, ny, n);
                }
            }
            down[i] = best;// -1 pit (only truly flat edges), -2 edge, else downstream
        }
    }

    // ── Drainage accumulation: push each cell's area downstream, highest first
    // so a cell is fully summed before it drains (topological order by filled
    // height, matching the routing surface).
    std::vector<int> order(static_cast<size_t>(n) * n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return filled[a] > filled[b]; });

    std::vector<float> acc(static_cast<size_t>(n) * n, 1.0f);// self area = 1 cell
    for (int i : order) {
        const int d = down[i];
        if (d >= 0) acc[d] += acc[i];
    }

    // ── River cells: drainage above the threshold (fraction of the whole grid)
    const float totalCells = static_cast<float>(n) * n;
    const float accThresh = std::max(2.0f, params.riverThreshold * totalCells);
    std::vector<uint8_t> isRiver(static_cast<size_t>(n) * n, 0);
    for (int i = 0; i < n * n; ++i)
        if (acc[i] >= accThresh) isRiver[i] = 1;

    // ── Trace polylines: start at a river SOURCE (a river cell with no river
    // cell draining into it) and follow `down` to the mouth. Mark cells used so
    // tributaries stop when they hit an already-traced trunk (one polyline per
    // strand; the trunk continues past the confluence).
    std::vector<int> inRiverCount(static_cast<size_t>(n) * n, 0);
    for (int i = 0; i < n * n; ++i)
        if (isRiver[i] && down[i] >= 0 && isRiver[down[i]]) inRiverCount[down[i]]++;

    const float accMax = *std::max_element(acc.begin(), acc.end());

    struct RawLine {
        std::vector<int> cells;
        float drainage = 0.0f;// max acc reached (for ranking)
    };
    std::vector<RawLine> raw;
    std::vector<uint8_t> visited(static_cast<size_t>(n) * n, 0);

    for (int s = 0; s < n * n; ++s) {
        if (!isRiver[s] || inRiverCount[s] != 0) continue;// not a source
        RawLine line;
        int cur = s;
        int guard = 0;
        while (cur >= 0 && isRiver[cur] && guard++ < n * n) {
            line.cells.push_back(cur);
            line.drainage = std::max(line.drainage, acc[cur]);
            if (visited[cur] && cur != s) break;// merged into a traced trunk
            visited[cur] = 1;
            cur = down[cur];// -2 (edge) / -1 (pit) ends the strand
        }
        if (static_cast<int>(line.cells.size()) >= params.minNodes) raw.push_back(std::move(line));
    }

    // Keep only the largest rivers to bound mesh cost.
    std::sort(raw.begin(), raw.end(), [](const RawLine& a, const RawLine& b) { return a.drainage > b.drainage; });
    if (static_cast<int>(raw.size()) > params.maxRivers) raw.resize(params.maxRivers);

    // ── Build nodes (world pos + width from drainage) and smooth the path ────
    auto widthOf = [&](float a) {
        const float t = std::sqrt(std::clamp((a - accThresh) / std::max(accMax - accThresh, 1.0f), 0.0f, 1.0f));
        return params.minWidth + params.widthScale * t;
    };

    std::vector<RiverPolyline> out;
    out.reserve(raw.size());
    for (const auto& line : raw) {
        // Grid-cell centres -> world points + widths.
        std::vector<glm::vec2> pts;
        std::vector<float> widths;
        pts.reserve(line.cells.size());
        for (int c : line.cells) {
            pts.push_back(worldOf(c % n, c / n));
            widths.push_back(widthOf(acc[c]));
        }

        // Resample the blocky 45°-grid path into a flowing curve with the
        // shared Catmull-Rom Spline; widths lerp along the same segment param.
        Spline<glm::vec2> spline;
        spline.SetPoints(pts);
        RiverPolyline poly;
        const int seg = 4;
        const int segCount = static_cast<int>(pts.size()) - 1;
        const int total = segCount * seg;
        auto flowOf = [&](float wpx) {
            return std::clamp((wpx - params.minWidth) / std::max(params.widthScale, 1e-3f), 0.05f, 1.0f);
        };
        for (int t = 0; t <= total; ++t) {
            const float gt = static_cast<float>(t) / static_cast<float>(total);// 0..1 over the spline
            const float fseg = gt * segCount;// which control segment
            const int k = std::min(segCount - 1, static_cast<int>(fseg));
            const float local = fseg - k;
            const size_t k1 = std::min<size_t>(k + 1, widths.size() - 1);
            RiverNode nd;
            nd.pos = spline.Sample(gt);
            nd.width = widths[k] + (widths[k1] - widths[k]) * local;
            nd.flow = flowOf(nd.width);
            // Raw water surface = the true (unfilled) terrain elevation, so the
            // river follows the valley's real downhill grade (not the flat
            // priority-flood surface). Smoothed below to remove the ripples the
            // carve then slices out of the bed.
            nd.surfaceY = height01(nd.pos.x, nd.pos.y) * heightScale;
            poly.nodes.push_back(nd);
        }

        // ── Thalweg snapping ────────────────────────────────────────────────
        // The coarse D8 grid can only place a node at a cell centre, so on a
        // side-slope the traced centreline sits partway up the valley wall
        // instead of on its floor — the river then reads as running across a
        // slope. Slide each node perpendicular to the flow to the lowest ground
        // within thalwegSnapRadius, sampling a mildly-smoothed height so it
        // seeks the valley floor (a real landform) and not a one-cell noise pit.
        if (params.thalwegSnapRadius > 0.0f) {
            auto& nodes = poly.nodes;
            // Landform height: average a few taps so snapping tracks the valley,
            // not the FBm speckle on top of it.
            auto landAt = [&](float x, float z) {
                float s = height01(x, z);
                const float rr = 22.0f;
                s += height01(x + rr, z) + height01(x - rr, z) + height01(x, z + rr) + height01(x, z - rr);
                return s / 5.0f;
            };
            std::vector<glm::vec2> snapped(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i) {
                const glm::vec2& a = nodes[i > 0 ? i - 1 : i].pos;
                const glm::vec2& b = nodes[i + 1 < nodes.size() ? i + 1 : i].pos;
                glm::vec2 tan = b - a;
                const float tl = std::sqrt(tan.x * tan.x + tan.y * tan.y);
                if (tl < 1e-4f) {
                    snapped[i] = nodes[i].pos;
                    continue;
                }
                tan /= tl;
                const glm::vec2 perp{ -tan.y, tan.x };
                float bestOff = 0.0f, bestH = landAt(nodes[i].pos.x, nodes[i].pos.y);
                const float step = std::max(cell * 0.5f, 6.0f);
                for (float d = -params.thalwegSnapRadius; d <= params.thalwegSnapRadius; d += step) {
                    const float x = nodes[i].pos.x + perp.x * d, z = nodes[i].pos.y + perp.y * d;
                    const float hh = landAt(x, z);
                    if (hh < bestH) {
                        bestH = hh;
                        bestOff = d;
                    }
                }
                snapped[i] = nodes[i].pos + perp * bestOff;
            }
            // Smooth the snapped path so per-node offsets don't produce a
            // zig-zag ribbon (each node snapped independently, so neighbours can
            // land on opposite banks). A few 1-2-1 passes pull it back to a
            // flowing curve that still tracks the valley floor.
            for (int pass = 0; pass < 4; ++pass) {
                std::vector<glm::vec2> tmp(snapped.size());
                for (size_t i = 0; i < snapped.size(); ++i) {
                    const glm::vec2 p0 = snapped[i > 0 ? i - 1 : i];
                    const glm::vec2 p2 = snapped[i + 1 < snapped.size() ? i + 1 : i];
                    tmp[i] = 0.25f * p0 + 0.5f * snapped[i] + 0.25f * p2;
                }
                snapped.swap(tmp);
            }
            for (size_t i = 0; i < nodes.size(); ++i) {
                nodes[i].pos = snapped[i];
                nodes[i].surfaceY = height01(nodes[i].pos.x, nodes[i].pos.y) * heightScale;
            }
        }

        // Smooth the surface profile along the river (a few moving-average
        // passes) so the water reads as a calm sheet following the valley, not
        // a jittery drape over every bump. Kept per-river; the carve floors the
        // bed a fixed depth below this, so the banks still rise around it.
        auto& nodes = poly.nodes;
        for (int pass = 0; pass < 3; ++pass) {
            std::vector<float> s(nodes.size());
            for (size_t i = 0; i < nodes.size(); ++i) {
                const size_t lo = i >= 2 ? i - 2 : 0;
                const size_t hi = std::min(nodes.size() - 1, i + 2);
                float sum = 0.0f;
                int cnt = 0;
                for (size_t j = lo; j <= hi; ++j) {
                    sum += nodes[j].surfaceY;
                    ++cnt;
                }
                s[i] = sum / cnt;
            }
            for (size_t i = 0; i < nodes.size(); ++i)
                nodes[i].surfaceY = s[i];
        }

        if (static_cast<int>(nodes.size()) >= params.minNodes) out.push_back(std::move(poly));
    }

    return out;
}

void BuildRiverMesh(
    const std::vector<RiverPolyline>& rivers,
    const std::function<float(float, float)>& height01,
    float heightScale,
    const RiverMeshParams& params,
    std::vector<Vertex>& verts,
    std::vector<uint16_t>& indices
) {
    // Position each centreline at the water surface, then hand it to the shared
    // ribbon mesher. Prefer the node's carved water-surface profile (surfaceY,
    // filled by BuildHydrology) so the ribbon is a smooth flat sheet sitting in
    // the incised channel; fall back to draping on the height source when the
    // network wasn't carved (surfaceY unset).
    for (const auto& river : rivers) {
        if (river.nodes.size() < 2) continue;
        std::vector<glm::vec3> centreline;
        std::vector<float> halfWidths;
        centreline.reserve(river.nodes.size());
        halfWidths.reserve(river.nodes.size());
        for (const auto& nd : river.nodes) {
            const float bed = (nd.surfaceY > 0.0f) ? nd.surfaceY : height01(nd.pos.x, nd.pos.y) * heightScale;
            centreline.emplace_back(nd.pos.x, bed + params.bankLift, nd.pos.y);
            halfWidths.push_back(nd.width * params.widthGain);
        }
        BuildRibbonMesh(centreline, halfWidths, params.uvMetresPerV, verts, indices);
    }
}

// ── River incision (erosion) ────────────────────────────────────────────────

float RiverCarveField::SampleFloor(float wx, float wz) const {
    const float half = 0.5f * _worldSize;
    const float fx = (wx + half) / _worldSize * _n - 0.5f;
    const float fz = (wz + half) / _worldSize * _n - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx)), z0 = static_cast<int>(std::floor(fz));
    const float tx = fx - x0, tz = fz - z0;
    auto at = [&](int x, int z) {
        if (x < 0 || z < 0 || x >= _n || z >= _n) return NO_FLOOR;
        return _floor[static_cast<size_t>(z) * _n + x];
    };
    const float a = at(x0, z0), b = at(x0 + 1, z0), c = at(x0, z0 + 1), d = at(x0 + 1, z0 + 1);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

TerrainHydrology BuildHydrology(
    const std::function<float(float, float)>& base01,
    float worldSize,
    float heightScale,
    const RiverNetworkParams& network,
    const CarveParams& carveParams
) {
    TerrainHydrology hydro;
    hydro.rivers = BuildRiverNetwork(base01, worldSize, heightScale, network);
    // surfaceY is already the filled-surface water level (set in BuildRiverNetwork).

    const int cn = std::max(16, carveParams.gridResolution);
    const float cell = worldSize / static_cast<float>(cn);
    const float half = 0.5f * worldSize;
    auto carve = std::make_shared<RiverCarveField>(cn, worldSize);
    auto& grid = carve->grid();

    // Stamp a FLAT channel floor elevation: within channelHalf the floor is
    // (surfaceY - bedDepth); past it, it ramps back UP above the terrain over
    // bankBlend so the carve tapers smoothly to nothing at the banks. Store the
    // MIN floor across nodes (deepest channel wins at confluences). Carved
    // terrain = min(base, floor), computed at full resolution below, so the
    // coarse grid can't leave the bed above the water.
    for (const auto& river : hydro.rivers) {
        for (const auto& nd : river.nodes) {
            // Waterline half-width = the ribbon's half-width (clamped to at least
            // a grid cell so a thin stream still lands on cells). The U reaches
            // the water surface here so the ribbon fills the channel edge-to-edge.
            const float wl = std::max(nd.width, cell);
            const float outer = wl + std::max(nd.width * carveParams.bankBlend, cell);

            const int cx = static_cast<int>((nd.pos.x + half) / cell);
            const int cz = static_cast<int>((nd.pos.y + half) / cell);
            const int r = static_cast<int>(std::ceil(outer / cell)) + 1;
            for (int dz = -r; dz <= r; ++dz) {
                for (int dx = -r; dx <= r; ++dx) {
                    const int gx = cx + dx, gz = cz + dz;
                    if (gx < 0 || gz < 0 || gx >= cn || gz >= cn) continue;
                    const float wx = (gx + 0.5f) * cell - half, wz = (gz + 0.5f) * cell - half;
                    const float dist = std::sqrt((wx - nd.pos.x) * (wx - nd.pos.x) + (wz - nd.pos.y) * (wz - nd.pos.y));
                    if (dist >= outer) continue;
                    float f;
                    if (dist <= wl) {
                        // U cross-section: bedDepth below the water at the centre,
                        // rising to exactly the water surface at the waterline.
                        const float u = dist / wl;// 0 centre .. 1 waterline
                        f = nd.surfaceY - carveParams.bedDepth * (1.0f - u * u);
                    } else {
                        // Past the waterline, ramp the floor up above the terrain
                        // so min(base,floor) leaves the natural bank uncarved.
                        const float u = (dist - wl) / std::max(outer - wl, 1e-3f);
                        f = nd.surfaceY + (u * u * (3.0f - 2.0f * u)) * (carveParams.bedDepth + 60.0f);
                    }
                    float& g = grid[static_cast<size_t>(gz) * cn + gx];
                    if (f < g) g = f;
                }
            }
        }
    }

    hydro.carve = carve;
    const float hs = heightScale > 0.0f ? heightScale : 1.0f;
    hydro.carvedHeight01 = [base01, carve, hs](float wx, float wz) {
        const float baseM = base01(wx, wz) * hs;
        const float floorM = carve->SampleFloor(wx, wz);// huge outside rivers -> no-op
        return std::clamp(std::min(baseM, floorM) / hs, 0.0f, 1.0f);
    };
    return hydro;
}
