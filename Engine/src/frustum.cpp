#include "frustum.hpp"

// Gribb–Hartmann plane extraction from a combined view-projection matrix:
// https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
// glm is column-major, so viewingMatrix[c][r] indexes column c, row r; the
// row sums below combine matrix rows to form each plane's (n, d).
Frustum::Frustum(const glm::mat4& m) {
    // Left  = row3 + row0
    _planes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]);
    // Right = row3 - row0
    _planes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]);
    // Bottom = row3 + row1
    _planes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]);
    // Top   = row3 - row1
    _planes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]);
    // Near  = row3 + row2 (matches GL / glm depth range [-1, 1])
    _planes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]);
    // Far   = row3 - row2
    _planes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]);

    for (glm::vec4& p : _planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 0.0f) p /= len;
    }
}

bool Frustum::Intersects(glm::vec3 point) const {
    for (const glm::vec4& p : _planes) {
        if (glm::dot(glm::vec3(p), point) + p.w < 0.0f) return false;
    }
    return true;
}

bool Frustum::Intersects(const AABB& box) const {
    // p-vertex trick: per plane, pick the AABB corner farthest along the
    // plane normal (component-wise via sign). If that corner is behind the
    // plane, every corner is — box is fully outside. Conservative: false
    // positives near edges, no false negatives.
    for (const glm::vec4& p : _planes) {
        const glm::vec3 pv(
            (p.x >= 0.0f) ? box.max.x : box.min.x,
            (p.y >= 0.0f) ? box.max.y : box.min.y,
            (p.z >= 0.0f) ? box.max.z : box.min.z
        );
        if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f) return false;
    }
    return true;
}

bool Frustum::IntersectsSphere(glm::vec3 center, float radius) const {
    // Planes are already unit-normalized, so `dot(n, c) + d` is the signed
    // distance directly; sphere is out if that's below -radius on any plane.
    for (const glm::vec4& p : _planes) {
        if (glm::dot(glm::vec3(p), center) + p.w < -radius) return false;
    }
    return true;
}

AABB AABB::Transform(const AABB& local, const glm::mat4& xform) {
    if (local.IsEmpty()) return { glm::vec3(0.0f), glm::vec3(0.0f) };

    // Arvo's optimal transform: for each output axis, split each column into
    // its positive and negative contribution and pair them with (max, min)
    // respectively. 3× fewer mat×vec than the naïve 8-corner sweep, same
    // result (the tightest axis-aligned box enclosing the transformed box).
    const glm::vec3 t(xform[3][0], xform[3][1], xform[3][2]);
    glm::vec3 outMin = t;
    glm::vec3 outMax = t;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const float a = xform[j][i] * local.min[j];
            const float b = xform[j][i] * local.max[j];
            if (a < b) {
                outMin[i] += a;
                outMax[i] += b;
            } else {
                outMin[i] += b;
                outMax[i] += a;
            }
        }
    }
    return { outMin, outMax };
}
