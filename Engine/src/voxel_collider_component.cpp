#include "voxel_collider_component.hpp"
#include "bullet_collision.hpp"
#include "bullet_dynamics.hpp"
#include "bullet_linear_math.hpp"
#include <BulletCollision/CollisionDispatch/btInternalEdgeUtility.h>
#include <BulletCollision/CollisionShapes/btTriangleInfoMap.h>
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
    _centerOfMass = glm::vec3(0.0f);
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

        // The hull comes back in the volume's local frame, whose origin is the
        // grid's bottom centre — for a 1.6 m crate the real centre of mass is
        // 0.8 m above it. Bullet has no separate centre-of-mass concept: it
        // takes the shape's origin to be one, spins the body about it, and
        // btPolyhedralConvexShape::calculateLocalInertia assumes the same. Left
        // uncentred, a prop pivots about its own base and gets the wrong
        // tensor. Build the hull about the centroid instead and hand the offset
        // to the body, which puts it back where it is drawn.
        _centerOfMass = volume->GetSolidCentroidLocal();
        auto hull = std::make_unique<btConvexHullShape>();
        for (const glm::vec3& p : points) {
            const glm::vec3 q = p - _centerOfMass;
            hull->addPoint(btVector3(q.x, q.y, q.z), false);
        }
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
        auto meshShape =
            std::make_unique<btBvhTriangleMeshShape>(_meshInterface.get(), /*useQuantizedAabbCompression=*/true);
        meshShape->setMargin(margin);

        // Internal-edge adjacency. Without it, contacts landing on the shared
        // edge between two triangles get a normal along the edge instead of out
        // of the surface, which shoves resting bodies sideways (jitter) and
        // down into the shell (sinking through). The default edge threshold is
        // 0.1 m — larger than a collision cell here, which would classify every
        // contact as an edge contact — so scale it to the voxel grid.
        _triangleInfo = std::make_unique<btTriangleInfoMap>();
        _triangleInfo->m_edgeDistanceThreshold = volume->voxelSize * 0.5f;
        btGenerateInternalEdgeInfo(meshShape.get(), _triangleInfo.get());

        _shape = std::move(meshShape);
        mass = 0.0f;// static
    }

    if (!_shape) return;
    RigidbodyProps rbProps;
    rbProps.shape = _shape.get();
    rbProps.mass = mass;
    rbProps.friction = _props.friction;
    rbProps.restitution = _props.restitution;
    // Zero for the static mesh, which is built in the object's own frame.
    rbProps.centerOfMass = _centerOfMass;
    auto* body = new RigidbodyComponent(gameObject, rbProps);
    gameObject->AddComponent(body);

    if (!_props.dynamic) {
        // Opt the mesh body into the contact-added callback that performs the
        // internal-edge correction (see Physics3DSubsystem).
        body->SetCustomMaterialCallback(true);
    }

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
        ImGui::Text("CoM %.2f, %.2f, %.2f (local)", _centerOfMass.x, _centerOfMass.y, _centerOfMass.z);
    } else {
        ImGui::Text("%d triangles", _triangleCount);
    }
    if (ImGui::Button("Rebuild collider")) {
        Rebuild();
    }
}
