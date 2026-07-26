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

    // Bullet's 4 cm default margin is meant for metre-scale shapes; at 5 cm
    // voxels it is a whole voxel wide. Keep it well under one voxel.
    const float margin = _props.collisionMargin > 0.0f ? _props.collisionMargin : volume->voxelSize * 0.2f;

    float mass = 0.0f;
    if (_props.dynamic) {
        // A convex hull: what a moving body needs (Bullet cannot move a mesh).
        std::vector<glm::vec3> points;
        volume->BuildConvexHullPoints(points, _props.hullDirections);
        if (points.size() < 4) return;// degenerate: nothing to hull
        auto hull = std::make_unique<btConvexHullShape>();
        for (const glm::vec3& p : points) hull->addPoint(btVector3(p.x, p.y, p.z), false);
        hull->setMargin(margin);
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
        volume->BuildSurfaceMesh(_vertices, _indices, _props.meshDownsample);
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
        _shape->setMargin(margin);
        mass = 0.0f;// static
    }

    if (!_shape) return;
    RigidbodyProps rbProps;
    rbProps.shape = _shape.get();
    rbProps.mass = mass;
    rbProps.friction = _props.friction;
    rbProps.restitution = _props.restitution;
    auto* body = new RigidbodyComponent(gameObject, rbProps);
    gameObject->AddComponent(body);

    if (_props.dynamic && _props.continuousCollision) {
        // Sweep once the body would move more than half its thinnest side in a
        // step; the swept sphere is a fraction of that so it approximates the
        // shape without over-triggering. Props here fall ~10 m, reaching ~0.25 m
        // per step — comparable to a small prop's own size.
        const glm::vec3 size = volume->GetLocalBoundsMax() - volume->GetLocalBoundsMin();
        const float minExtent = std::max(std::min({ size.x, size.y, size.z }), volume->voxelSize);
        body->SetContinuousCollision(minExtent * 0.5f, minExtent * 0.2f);
    }

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
