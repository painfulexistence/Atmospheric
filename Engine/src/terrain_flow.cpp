#include "terrain_flow.hpp"

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

    // Catmull-Rom point for smoothing the blocky grid-traced path.
    glm::vec2 CatmullRom(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, float t) {
        const float t2 = t * t, t3 = t2 * t;
        return 0.5f
               * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
                  + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
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

        // Catmull-Rom resample (4 samples/segment) for a flowing curve instead
        // of 45-degree grid steps; widths lerp along the same parameter.
        RiverPolyline poly;
        const int seg = 4;
        for (size_t k = 0; k + 1 < pts.size(); ++k) {
            const glm::vec2& p1 = pts[k];
            const glm::vec2& p2 = pts[k + 1];
            const glm::vec2& p0 = pts[k > 0 ? k - 1 : k];
            const glm::vec2& p3 = pts[k + 2 < pts.size() ? k + 2 : k + 1];
            const int steps = (k + 2 == pts.size()) ? seg : seg;// include endpoint on last seg below
            for (int t = 0; t < steps; ++t) {
                const float u = static_cast<float>(t) / seg;
                RiverNode nd;
                nd.pos = CatmullRom(p0, p1, p2, p3, u);
                nd.flow = std::clamp((widths[k] - params.minWidth) / std::max(params.widthScale, 1e-3f), 0.05f, 1.0f);
                nd.width = widths[k] + (widths[k + 1] - widths[k]) * u;
                poly.nodes.push_back(nd);
            }
        }
        // Final node (mouth).
        if (!pts.empty()) {
            RiverNode nd;
            nd.pos = pts.back();
            nd.width = widths.back();
            nd.flow = std::clamp((widths.back() - params.minWidth) / std::max(params.widthScale, 1e-3f), 0.05f, 1.0f);
            poly.nodes.push_back(nd);
        }
        if (static_cast<int>(poly.nodes.size()) >= params.minNodes) out.push_back(std::move(poly));
    }

    return out;
}
