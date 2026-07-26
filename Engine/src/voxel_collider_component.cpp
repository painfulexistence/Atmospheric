#include "voxel_collider_component.hpp"
#include "bullet_collision.hpp"
#include "bullet_dynamics.hpp"
#include "bullet_linear_math.hpp"
#include "console_subsystem.hpp"
#include "game_object.hpp"
#include "rigidbody_component.hpp"
#include "voxel_volume_component.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <imgui.h>

VoxelColliderComponent::VoxelColliderComponent(GameObject* owner, const VoxelColliderProps& props) : _props(props) {
    gameObject = owner;
}

VoxelColliderComponent::~VoxelColliderComponent() = default;

void VoxelColliderComponent::OnAttach() {
    _build();
}

void VoxelColliderComponent::Rebuild() {
    // Drop the old body first: it holds a raw pointer to the shape we are about
    // to free.
    if (gameObject) {
        if (auto* rb = gameObject->GetComponent<RigidbodyComponent>()) gameObject->RemoveComponent(rb);
    }
    _releaseShape();
    _build();
}

void VoxelColliderComponent::_releaseShape() {
    _shape.reset();
    _meshInterface.reset();
    _vertices.clear();
    _indices.clear();
    _triangleCount = 0;
    _hullPointCount = 0;
}

void VoxelColliderComponent::_build() {
    if (!gameObject) return;
    auto* volume = gameObject->GetComponent<VoxelVolumeComponent>();
    if (volume == nullptr || !volume->HasSolid()) {
        if (auto* console = ConsoleSubsystem::Get()) {
            console->Warn("VoxelCollider: no generated VoxelVolume on this object — no body created");
        }
        return;
    }

    float mass = 0.0f;
    if (_props.dynamic) {
        // A convex hull: what a moving body needs (Bullet cannot move a mesh).
        std::vector<glm::vec3> points;
        volume->BuildConvexHullPoints(points, _props.hullDirections);
        if (points.size() < 4) return;// degenerate: nothing to hull
        auto hull = std::make_unique<btConvexHullShape>();
        for (const glm::vec3& p : points) hull->addPoint(btVector3(p.x, p.y, p.z), false);
        hull->recalcLocalAabb();
        _hullPointCount = static_cast<int>(points.size());
        _shape = std::move(hull);

        mass = _props.mass;
        if (mass <= 0.0f) {
            const float v = volume->voxelSize;
            mass = static_cast<float>(volume->solidCount) * v * v * v * _props.density;
            mass = std::max(mass, 0.01f);// never hand Bullet a zero mass for a dynamic body
        }
    } else {
        const auto voxels = static_cast<uint64_t>(volume->gridDim) * static_cast<uint64_t>(volume->gridDim)
                            * static_cast<uint64_t>(volume->gridDim);
        if (_props.maxMeshVoxels != 0 && voxels > _props.maxMeshVoxels) {
            if (auto* console = ConsoleSubsystem::Get()) {
                console->Warn(fmt::format(
                    "VoxelCollider: {}^3 volume exceeds maxMeshVoxels ({}) — no static mesh built",
                    volume->gridDim,
                    _props.maxMeshVoxels
                ));
            }
            return;
        }
        volume->BuildSurfaceMesh(_vertices, _indices);
        if (_indices.size() < 3) return;
        _triangleCount = static_cast<int>(_indices.size() / 3);

        // Bullet reads these arrays on every query, so they stay owned here.
        btIndexedMesh mesh;
        mesh.m_numTriangles = _triangleCount;
        mesh.m_triangleIndexBase = reinterpret_cast<const unsigned char*>(_indices.data());
        mesh.m_triangleIndexStride = 3 * static_cast<int>(sizeof(uint32_t));
        mesh.m_numVertices = static_cast<int>(_vertices.size());
        mesh.m_vertexBase = reinterpret_cast<const unsigned char*>(_vertices.data());
        mesh.m_vertexStride = static_cast<int>(sizeof(glm::vec3));
        mesh.m_indexType = PHY_INTEGER;
        mesh.m_vertexType = PHY_FLOAT;

        _meshInterface = std::make_unique<btTriangleIndexVertexArray>();
        _meshInterface->addIndexedMesh(mesh, PHY_INTEGER);
        _shape = std::make_unique<btBvhTriangleMeshShape>(_meshInterface.get(), /*useQuantizedAabbCompression=*/true);
        mass = 0.0f;// static
    }

    if (!_shape) return;
    RigidbodyProps rbProps;
    rbProps.shape = _shape.get();
    rbProps.mass = mass;
    rbProps.friction = _props.friction;
    rbProps.restitution = _props.restitution;
    gameObject->AddComponent(new RigidbodyComponent(gameObject, rbProps));

    if (auto* console = ConsoleSubsystem::Get()) {
        console->Info(
            _props.dynamic
                ? fmt::format("VoxelCollider: convex hull, {} points, mass {:.1f} kg", _hullPointCount, mass)
                : fmt::format("VoxelCollider: static mesh, {} triangles", _triangleCount)
        );
    }
}

void VoxelColliderComponent::DrawImGui() {
    ImGui::Text("%s", _props.dynamic ? "convex hull (dynamic)" : "triangle mesh (static)");
    if (_props.dynamic) {
        ImGui::Text("%d hull points", _hullPointCount);
    } else {
        ImGui::Text("%d triangles", _triangleCount);
    }
    if (ImGui::Button("Rebuild collider")) {
        Rebuild();
    }
}
