#include "spline_mesh.hpp"

#include <algorithm>
#include <glm/geometric.hpp>

void BuildRibbonMesh(
    const std::vector<glm::vec3>& centreline,
    const std::vector<float>& halfWidths,
    float uvMetresPerV,
    std::vector<Vertex>& verts,
    std::vector<uint16_t>& indices
) {
    const size_t count = std::min(centreline.size(), halfWidths.size());
    if (count < 2) return;
    const size_t n = std::min<size_t>(count, 32000);// uint16 strip cap (2 verts/point)

    const uint16_t base = static_cast<uint16_t>(verts.size());
    const float mPerV = std::max(uvMetresPerV, 1e-3f);

    float distance = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const glm::vec3 p = centreline[i];
        // Forward tangent from neighbours (central difference), flattened to the
        // horizontal plane so the cross-section stays level; side is its 90°.
        const glm::vec3 a = centreline[i > 0 ? i - 1 : i];
        const glm::vec3 b = centreline[i + 1 < n ? i + 1 : i];
        glm::vec3 fwd(b.x - a.x, 0.0f, b.z - a.z);
        const float fl = glm::length(fwd);
        fwd = fl > 1e-4f ? fwd / fl : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 side(-fwd.z, 0.0f, fwd.x);

        if (i > 0) {
            const glm::vec3 d = p - centreline[i - 1];
            distance += glm::length(d);
        }
        const float v = distance / mPerV;
        const float w = halfWidths[i];

        for (int s = 0; s < 2; ++s) {
            const float sign = (s == 0) ? -1.0f : 1.0f;
            Vertex vert;
            vert.position = p + side * (sign * w);
            vert.uv = glm::vec2(s == 0 ? 0.0f : 1.0f, v);
            vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vert.tangent = fwd;
            vert.bitangent = side;
            verts.push_back(vert);
        }
    }

    for (size_t i = 0; i + 1 < n; ++i) {
        const uint16_t l0 = static_cast<uint16_t>(base + i * 2);
        const uint16_t r0 = static_cast<uint16_t>(l0 + 1);
        const uint16_t l1 = static_cast<uint16_t>(l0 + 2);
        const uint16_t r1 = static_cast<uint16_t>(l0 + 3);
        indices.insert(indices.end(), { l0, r0, l1, r0, r1, l1 });
    }
}
