#include "frustum.hpp"

Frustum::Frustum(glm::mat4 viewingMatrix) {
    // NOTES:
    // https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
    const float* mat = glm::value_ptr(viewingMatrix);
    _near.a = mat[2];
    _near.b = mat[6];
    _near.c = mat[10];
    _near.d = mat[14];
    _far.a = mat[3] - mat[2];
    _far.b = mat[7] - mat[6];
    _far.c = mat[11] - mat[10];
    _far.d = mat[15] - mat[14];
    _bottom.a = mat[3] + mat[1];
    _bottom.b = mat[7] + mat[5];
    _bottom.c = mat[11] + mat[9];
    _bottom.d = mat[15] + mat[13];
    _top.a = mat[3] - mat[1];
    _top.b = mat[7] - mat[5];
    _top.c = mat[11] - mat[9];
    _top.d = mat[15] - mat[13];
    _left.a = mat[3] + mat[0];
    _left.b = mat[7] + mat[4];
    _left.c = mat[11] + mat[8];
    _left.d = mat[15] + mat[12];
    _right.a = mat[3] - mat[0];
    _right.b = mat[7] - mat[4];
    _right.c = mat[11] - mat[8];
    _right.d = mat[15] - mat[12];
}

bool Frustum::Intersects(glm::vec3 point) const {
    // NOTES: a * x + b * y + c * z + d > 0 if (x, y, z) is in the "inside" halfsapce
    if (_near.Halfspace(point) < 0 || _far.Halfspace(point) < 0) return false;
    if (_bottom.Halfspace(point) < 0 || _top.Halfspace(point) < 0) return false;
    if (_left.Halfspace(point) < 0 || _right.Halfspace(point) < 0) return false;

    return true;
}

bool Frustum::Intersects(std::array<glm::vec3, 8> points) const {
    for (int i = 0; i < 8; ++i) {
        if (Intersects(points[i])) return true;
    }
    return false;
}

bool Frustum::IntersectsSphere(glm::vec3 center, float radius) const {
    // For each plane, the signed distance is (dot(n,p) + d) / |n|.
    // If signed_dist < -radius the sphere is fully outside that plane.
    for (const Plane* p : { &_near, &_far, &_top, &_bottom, &_left, &_right }) {
        if (p->SignedDistance(center) < -radius) return false;
    }
    return true;
}