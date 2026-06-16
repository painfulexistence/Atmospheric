#include "csg.hpp"
#include <algorithm>

namespace CSG {

// ============================================================================
// Primitive Creation
// ============================================================================

NodePtr Box(glm::vec3 position, glm::vec3 size, const std::string& name) {
    auto node = std::make_shared<Node>();
    node->operation = Operation::Primitive;
    node->primitive.type = PrimitiveType::Box;
    node->primitive.position = position;
    node->primitive.size = size;
    node->primitive.name = name;
    node->name = name;
    return node;
}

// ============================================================================
// Boolean Operations
// ============================================================================

NodePtr Union(NodePtr a, NodePtr b) {
    auto node = std::make_shared<Node>();
    node->operation = Operation::Union;
    node->left = a;
    node->right = b;
    return node;
}

NodePtr Subtract(NodePtr a, NodePtr b) {
    auto node = std::make_shared<Node>();
    node->operation = Operation::Subtract;
    node->left = a;
    node->right = b;
    return node;
}

NodePtr Intersect(NodePtr a, NodePtr b) {
    auto node = std::make_shared<Node>();
    node->operation = Operation::Intersect;
    node->left = a;
    node->right = b;
    return node;
}

// ============================================================================
// Box Subtract Algorithm
// ============================================================================
//
// When subtracting box B from box A:
// 1. Find the intersection region
// 2. If no intersection, return A unchanged
// 3. Otherwise, split A into up to 6 remaining boxes around the hole
//
// Visual (2D top-down view):
//
//   A (original)        B (subtract)       Result (up to 6 boxes)
//   ┌───────────┐       ┌───┐             ┌──┬───┬──┐
//   │           │   -   │   │      =      │L │ T │ R│
//   │           │       └───┘             ├──┼───┼──┤
//   │           │                         │  │hole│  │
//   └───────────┘                         ├──┼───┼──┤
//                                         │  │ B │  │
//                                         └──┴───┴──┘
//
// ============================================================================

std::vector<AABB> SubtractBox(const AABB& a, const AABB& b) {
    std::vector<AABB> result;

    // Calculate intersection
    AABB intersection = AABB::Intersect(a, b);

    // If no intersection, return original box unchanged
    if (!intersection.IsValid()) {
        result.push_back(a);
        return result;
    }

    // If B completely contains A, nothing remains
    if (b.Contains(a)) {
        return result;  // Empty
    }

    // Generate up to 6 boxes around the hole
    // Order: +X, -X, +Y, -Y, +Z, -Z

    // +X direction (right side)
    if (intersection.max.x < a.max.x) {
        AABB right;
        right.min = glm::vec3(intersection.max.x, a.min.y, a.min.z);
        right.max = glm::vec3(a.max.x, a.max.y, a.max.z);
        if (right.IsValid()) {
            result.push_back(right);
        }
    }

    // -X direction (left side)
    if (intersection.min.x > a.min.x) {
        AABB left;
        left.min = glm::vec3(a.min.x, a.min.y, a.min.z);
        left.max = glm::vec3(intersection.min.x, a.max.y, a.max.z);
        if (left.IsValid()) {
            result.push_back(left);
        }
    }

    // +Y direction (top) - only the middle column
    if (intersection.max.y < a.max.y) {
        AABB top;
        top.min = glm::vec3(intersection.min.x, intersection.max.y, a.min.z);
        top.max = glm::vec3(intersection.max.x, a.max.y, a.max.z);
        if (top.IsValid()) {
            result.push_back(top);
        }
    }

    // -Y direction (bottom) - only the middle column
    if (intersection.min.y > a.min.y) {
        AABB bottom;
        bottom.min = glm::vec3(intersection.min.x, a.min.y, a.min.z);
        bottom.max = glm::vec3(intersection.max.x, intersection.min.y, a.max.z);
        if (bottom.IsValid()) {
            result.push_back(bottom);
        }
    }

    // +Z direction (front) - only the center piece
    if (intersection.max.z < a.max.z) {
        AABB front;
        front.min = glm::vec3(intersection.min.x, intersection.min.y, intersection.max.z);
        front.max = glm::vec3(intersection.max.x, intersection.max.y, a.max.z);
        if (front.IsValid()) {
            result.push_back(front);
        }
    }

    // -Z direction (back) - only the center piece
    if (intersection.min.z > a.min.z) {
        AABB back;
        back.min = glm::vec3(intersection.min.x, intersection.min.y, a.min.z);
        back.max = glm::vec3(intersection.max.x, intersection.max.y, intersection.min.z);
        if (back.IsValid()) {
            result.push_back(back);
        }
    }

    return result;
}

// ============================================================================
// CSG Compiler
// ============================================================================

namespace {

std::vector<AABB> CompilePrimitive(const Primitive& prim) {
    if (prim.type != PrimitiveType::Box) {
        // Only Box supported for now
        return {};
    }
    return { AABB::FromCenterSize(prim.position, prim.size) };
}

std::vector<AABB> CompileUnion(const NodePtr& node) {
    auto leftBoxes = Compile(node->left);
    auto rightBoxes = Compile(node->right);

    // Union: combine both lists
    leftBoxes.insert(leftBoxes.end(), rightBoxes.begin(), rightBoxes.end());
    return leftBoxes;
}

std::vector<AABB> CompileSubtract(const NodePtr& node) {
    auto leftBoxes = Compile(node->left);
    auto rightBoxes = Compile(node->right);

    // For each box in left, subtract all boxes in right
    std::vector<AABB> result;

    for (const auto& leftBox : leftBoxes) {
        std::vector<AABB> current = { leftBox };

        for (const auto& rightBox : rightBoxes) {
            std::vector<AABB> next;
            for (const auto& box : current) {
                auto subtracted = SubtractBox(box, rightBox);
                next.insert(next.end(), subtracted.begin(), subtracted.end());
            }
            current = std::move(next);
        }

        result.insert(result.end(), current.begin(), current.end());
    }

    return result;
}

std::vector<AABB> CompileIntersect(const NodePtr& node) {
    auto leftBoxes = Compile(node->left);
    auto rightBoxes = Compile(node->right);

    std::vector<AABB> result;

    // Intersect: for each pair, compute intersection
    for (const auto& leftBox : leftBoxes) {
        for (const auto& rightBox : rightBoxes) {
            AABB intersection = AABB::Intersect(leftBox, rightBox);
            if (intersection.IsValid()) {
                result.push_back(intersection);
            }
        }
    }

    return result;
}

} // anonymous namespace

std::vector<AABB> Compile(const NodePtr& node) {
    if (!node) {
        return {};
    }

    switch (node->operation) {
        case Operation::Primitive:
            return CompilePrimitive(node->primitive);

        case Operation::Union:
            return CompileUnion(node);

        case Operation::Subtract:
            return CompileSubtract(node);

        case Operation::Intersect:
            return CompileIntersect(node);

        default:
            return {};
    }
}

} // namespace CSG
