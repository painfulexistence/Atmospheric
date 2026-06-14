#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// CSG (Constructive Solid Geometry) for Level Blockouts
// ============================================================================
//
// Usage:
//   auto room = CSG::Box({0, 2.5f, 0}, {20, 5, 30});
//   auto door = CSG::Box({0, 1.5f, 15}, {4, 3, 2});
//   auto result = CSG::Subtract(room, door);
//   auto boxes = CSG::Compile(result);
//   CSGToMesh(boxes, mesh);
//
// ============================================================================

namespace CSG {

// ============================================================================
// AABB - Axis-Aligned Bounding Box
// ============================================================================

struct AABB {
    glm::vec3 min = glm::vec3(0);
    glm::vec3 max = glm::vec3(0);

    AABB() = default;
    AABB(glm::vec3 min, glm::vec3 max) : min(min), max(max) {}

    // Create from center and size
    static AABB FromCenterSize(glm::vec3 center, glm::vec3 size) {
        glm::vec3 halfSize = size * 0.5f;
        return AABB(center - halfSize, center + halfSize);
    }

    bool IsValid() const {
        return min.x < max.x && min.y < max.y && min.z < max.z;
    }

    glm::vec3 GetCenter() const {
        return (min + max) * 0.5f;
    }

    glm::vec3 GetSize() const {
        return max - min;
    }

    // Calculate intersection of two AABBs
    static AABB Intersect(const AABB& a, const AABB& b) {
        return AABB(
            glm::max(a.min, b.min),
            glm::min(a.max, b.max)
        );
    }

    bool Intersects(const AABB& other) const {
        return Intersect(*this, other).IsValid();
    }

    bool Contains(const AABB& other) const {
        return other.min.x >= min.x && other.max.x <= max.x &&
               other.min.y >= min.y && other.max.y <= max.y &&
               other.min.z >= min.z && other.max.z <= max.z;
    }
};

// ============================================================================
// CSG Primitive Types
// ============================================================================

enum class PrimitiveType {
    Box,
    Cylinder,  // Future
    Wedge      // Future
};

struct Primitive {
    PrimitiveType type = PrimitiveType::Box;
    glm::vec3 position = glm::vec3(0);
    glm::vec3 size = glm::vec3(1);
    std::string name;  // For debugging/editor
};

// ============================================================================
// CSG Node - Tree structure for CSG operations
// ============================================================================

enum class Operation {
    Primitive,   // Leaf node - single shape
    Union,       // A + B
    Subtract,    // A - B
    Intersect    // A & B
};

struct Node {
    Operation operation = Operation::Primitive;
    Primitive primitive;
    std::shared_ptr<Node> left;
    std::shared_ptr<Node> right;
    std::string name;
};

using NodePtr = std::shared_ptr<Node>;

// ============================================================================
// CSG API - Main interface
// ============================================================================

// Create primitives
NodePtr Box(glm::vec3 position, glm::vec3 size, const std::string& name = "");

// Boolean operations
NodePtr Union(NodePtr a, NodePtr b);
NodePtr Subtract(NodePtr a, NodePtr b);
NodePtr Intersect(NodePtr a, NodePtr b);

// Compile CSG tree to list of AABBs
std::vector<AABB> Compile(const NodePtr& node);

// ============================================================================
// Box Subtract Algorithm
// ============================================================================

// Subtract box B from box A, returning resulting boxes (max 6)
std::vector<AABB> SubtractBox(const AABB& a, const AABB& b);

} // namespace CSG
