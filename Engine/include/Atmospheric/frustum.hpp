#pragma once
#include "globals.hpp"

// Axis-aligned bounding box. The engine's single AABB type — used as the
// culling primitive for meshes, instancer clouds, terrain tiles and voxel
// chunks (Frustum::Intersects picks a p-vertex per plane via componentwise
// sign checks), and as the value type for CSG box operations.
struct AABB {
    glm::vec3 min{ 0.0f };
    glm::vec3 max{ 0.0f };

    AABB() = default;
    AABB(glm::vec3 min, glm::vec3 max) : min(min), max(max) {
    }

    static AABB FromCenterSize(glm::vec3 center, glm::vec3 size) {
        const glm::vec3 half = size * 0.5f;
        return { center - half, center + half };
    }

    // "Unset" bounds — the default-constructed sentinel used by Mesh to mean
    // "no bounds computed yet, skip frustum culling".
    bool IsEmpty() const {
        return min == max;
    }

    // Strict volume check — a zero-thickness slab is *not* Valid. CSG uses
    // this to reject degenerate cuts; frustum culling uses IsEmpty() instead.
    bool IsValid() const {
        return min.x < max.x && min.y < max.y && min.z < max.z;
    }

    glm::vec3 GetCenter() const {
        return (min + max) * 0.5f;
    }

    glm::vec3 GetSize() const {
        return max - min;
    }

    // Set intersection: a box whose min/max are the componentwise max/min.
    // Empty when the inputs don't overlap (IsValid returns false).
    static AABB Intersect(const AABB& a, const AABB& b) {
        return { glm::max(a.min, b.min), glm::min(a.max, b.max) };
    }

    bool Intersects(const AABB& other) const {
        return Intersect(*this, other).IsValid();
    }

    bool Contains(const AABB& other) const {
        return other.min.x >= min.x && other.max.x <= max.x && other.min.y >= min.y && other.max.y <= max.y
               && other.min.z >= min.z && other.max.z <= max.z;
    }

    // World-space AABB of `local` after `xform`. Loose (rotation expands the
    // box) but conservative, matching the pre-refactor 8-corner behaviour.
    static AABB Transform(const AABB& local, const glm::mat4& xform);
};

class Frustum {
    // 6 planes packed as (nx, ny, nz, d) with unit-length normals, in the
    // order Left, Right, Bottom, Top, Near, Far. Normalized at construction
    // so tests reduce to a single dot4 (or dot3 + add).
    glm::vec4 _planes[6];

public:
    // viewingMatrix = projection * view (column-major glm::mat4). Planes are
    // extracted via the Gribb-Hartmann row-sum method and normalized in place.
    Frustum(const glm::mat4& viewingMatrix);

    bool Intersects(glm::vec3 point) const;

    // p-vertex test: for each plane, pick the AABB corner farthest along the
    // plane normal. If that corner is on the negative side, the whole box is
    // outside. 6 dot products per box.
    bool Intersects(const AABB& box) const;

    // Sphere test — matches VX's camera.is_visible(bsphere).
    // Culls only when the sphere is fully outside any single frustum plane.
    bool IntersectsSphere(glm::vec3 center, float radius) const;
};
