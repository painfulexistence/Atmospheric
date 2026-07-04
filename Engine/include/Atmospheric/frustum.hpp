#pragma once
#include "globals.hpp"

struct Plane {
    enum class Halfspace { NEGATIVE = -1, ZERO = 0, POSITIVE = 1 };

    float a, b, c, d;

    float SignedDistance(glm::vec3 p) const {
        float naiveDist = a * p.x + b * p.y + c * p.z + d;
        return naiveDist / glm::length(glm::vec3(a, b, c));
    };

    // Renamed from Halfspace() — a method sharing its enum's name is a lookup
    // hazard, and enum class Halfspace forbids the old `< 0` comparison anyway.
    Halfspace ClassifyPoint(glm::vec3 p) const {
        float naiveDist = a * p.x + b * p.y + c * p.z + d;
        if (naiveDist == 0) return Halfspace::ZERO;
        return naiveDist > 0 ? Halfspace::POSITIVE : Halfspace::NEGATIVE;
    };
};

class Frustum {
    Plane _near, _far, _top, _bottom, _left, _right;

public:
    Frustum(const glm::mat4& viewingMatrix);

    bool Intersects(glm::vec3) const;

    bool Intersects(std::array<glm::vec3, 8>) const;

    // Sphere test — matches VX's camera.is_visible(bsphere).
    // Culls only when the sphere is fully outside any single frustum plane.
    bool IntersectsSphere(glm::vec3 center, float radius) const;
};