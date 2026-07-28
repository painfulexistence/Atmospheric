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

#include "job_system.hpp"

#include <algorithm>
#include <atomic>
#include <fmt/format.h>
#include <imgui.h>
#include <thread>

struct VoxelColliderComponent::PendingBuild {
    // Bullet keeps raw pointers into these two, so they move into the
    // component and stay there. A vector move hands over the same heap buffer,
    // which is what lets the btIndexedMesh built here stay valid afterwards.
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    std::unique_ptr<btTriangleIndexVertexArray> meshInterface;
    std::unique_ptr<btTriangleInfoMap> triangleInfo;
    std::unique_ptr<btCollisionShape> shape;
    glm::vec3 centerOfMass{ 0.0f };
    float mass = 0.0f;
    int triangleCount = 0;
    int hullPointCount = 0;
    // Release/acquire pairs with the main thread's read in OnTick, publishing
    // everything written above it.
    std::atomic<bool> done{ false };
};

VoxelColliderComponent::VoxelColliderComponent(GameObject* owner, const VoxelColliderProps& props) : _props(props) {
    gameObject = owner;
}

VoxelColliderComponent::~VoxelColliderComponent() {
    _waitForBuild();
}

void VoxelColliderComponent::OnAttach() {
    _beginBuild();
}

void VoxelColliderComponent::OnDetach() {
    // The worker reads the sibling VoxelVolumeComponent's voxels, and component
    // destruction order within a GameObject is not ours to pick, so the wait
    // has to happen while the object is still whole.
    _waitForBuild();
}

void VoxelColliderComponent::OnTick(float /*dt*/) {
    if (_pending && _pending->done.load(std::memory_order_acquire)) {
        _finishBuild();
    }
}

void VoxelColliderComponent::Rebuild() {
    _waitForBuild();
    // Drop the old body first: it holds a raw pointer to the shape we are about
    // to free.
    if (gameObject) {
        if (auto* rb = gameObject->GetComponent<RigidbodyComponent>()) gameObject->RemoveComponent(rb);
    }
    _releaseShape();
    _beginBuild();
}

void VoxelColliderComponent::_waitForBuild() {
    if (!_pending) return;
    // Spin rather than sleep: the worker is already running and this only ever
    // happens on teardown or an explicit Rebuild, never per frame.
    while (!_pending->done.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    _pending.reset();
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

void VoxelColliderComponent::_beginBuild() {
    if (!gameObject) return;
    auto* volume = gameObject->GetComponent<VoxelVolumeComponent>();
    if (volume == nullptr || !volume->HasSolid()) {
        if (auto* console = ConsoleSubsystem::Get()) {
            console->Warn("VoxelCollider: no generated VoxelVolume on this object — no body created");
        }
        return;
    }
    // Cheap enough to answer here, and it saves queueing a job that would do
    // nothing.
    if (!_props.dynamic) {
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
    }

    // Extraction is the expensive half — a 256^3 terrain is ~60 ms of meshing
    // plus the BVH build, which used to land inline on the frame the volume
    // finished generating. It reads finished voxel data and touches no engine
    // state, so it moves to a worker; only the body creation, which mutates the
    // Bullet world, stays on the main thread (see _finishBuild).
    //
    // The job holds the result by shared_ptr, so a component destroyed
    // mid-build does not leave it writing into freed memory. It also reads the
    // volume by raw pointer, which is why teardown waits (see _waitForBuild):
    // the volume must outlive the job, and so must the voxels, which means
    // nothing may carve while a build is in flight.
    auto pending = std::make_shared<PendingBuild>();
    _pending = pending;
    const VoxelColliderProps props = _props;
    JobSystem::Get()->Execute([pending, volume, props](int /*threadIndex*/) {
        _extract(*pending, *volume, props);
        pending->done.store(true, std::memory_order_release);
    });
}

void VoxelColliderComponent::_extract(
    PendingBuild& out, const VoxelVolumeComponent& volume, const VoxelColliderProps& props
) {
    // Bullet's 4 cm default margin is meant for metre-scale shapes; at 5 cm
    // voxels it is a whole voxel wide. Keep it well under one voxel.
    const float margin = props.collisionMargin > 0.0f ? props.collisionMargin : volume.voxelSize * 0.2f;

    if (props.dynamic) {
        // A convex hull: what a moving body needs (Bullet cannot move a mesh).
        std::vector<glm::vec3> points;
        volume.BuildConvexHullPoints(points, props.hullDirections);
        if (points.size() < 4) return;// degenerate: nothing to hull

        // The hull comes back in the volume's local frame, whose origin is the
        // grid's bottom centre — for a 1.6 m crate the real centre of mass is
        // 0.8 m above it. Bullet has no separate centre-of-mass concept: it
        // takes the shape's origin to be one, spins the body about it, and
        // btPolyhedralConvexShape::calculateLocalInertia assumes the same. Left
        // uncentred, a prop pivots about its own base and gets the wrong
        // tensor. Build the hull about the centroid instead and hand the offset
        // to the body, which puts it back where it is drawn.
        out.centerOfMass = volume.GetSolidCentroidLocal();
        auto hull = std::make_unique<btConvexHullShape>();
        for (const glm::vec3& p : points) {
            const glm::vec3 q = p - out.centerOfMass;
            hull->addPoint(btVector3(q.x, q.y, q.z), false);
        }
        hull->setMargin(margin);
        hull->recalcLocalAabb();
        out.hullPointCount = static_cast<int>(points.size());
        out.shape = std::move(hull);

        out.mass = props.mass;
        if (out.mass <= 0.0f) {
            const float v = volume.voxelSize;
            out.mass = static_cast<float>(volume.solidCount) * v * v * v * props.density;
            out.mass = std::max(out.mass, 0.01f);// never hand Bullet a zero mass for a dynamic body
        }
    } else {
        volume.BuildSurfaceMesh(out.vertices, out.indices, props.meshDownsample);
        if (out.indices.size() < 3) return;
        out.triangleCount = static_cast<int>(out.indices.size() / 3);

        // Bullet reads these arrays on every query, so they stay owned by the
        // component. Moving the vectors out of here later keeps the same heap
        // buffer, so the pointers taken now stay valid.
        btIndexedMesh mesh;
        mesh.m_numTriangles = out.triangleCount;
        mesh.m_triangleIndexBase = reinterpret_cast<const unsigned char*>(out.indices.data());
        mesh.m_triangleIndexStride = 3 * static_cast<int>(sizeof(uint32_t));
        mesh.m_numVertices = static_cast<int>(out.vertices.size());
        mesh.m_vertexBase = reinterpret_cast<const unsigned char*>(out.vertices.data());
        mesh.m_vertexStride = static_cast<int>(sizeof(glm::vec3));
        mesh.m_indexType = PHY_INTEGER;
        mesh.m_vertexType = PHY_FLOAT;

        out.meshInterface = std::make_unique<btTriangleIndexVertexArray>();
        out.meshInterface->addIndexedMesh(mesh, PHY_INTEGER);
        // The BVH is built right here, in the constructor — which is most of
        // why this whole function belongs off the main thread.
        auto meshShape =
            std::make_unique<btBvhTriangleMeshShape>(out.meshInterface.get(), /*useQuantizedAabbCompression=*/true);
        meshShape->setMargin(margin);

        // Internal-edge adjacency. Without it, contacts landing on the shared
        // edge between two triangles get a normal along the edge instead of out
        // of the surface, which shoves resting bodies sideways (jitter) and
        // down into the shell (sinking through). The default edge threshold is
        // 0.1 m — larger than a collision cell here, which would classify every
        // contact as an edge contact — so scale it to the voxel grid.
        out.triangleInfo = std::make_unique<btTriangleInfoMap>();
        out.triangleInfo->m_edgeDistanceThreshold = volume.voxelSize * 0.5f;
        btGenerateInternalEdgeInfo(meshShape.get(), out.triangleInfo.get());

        out.shape = std::move(meshShape);
        out.mass = 0.0f;// static
    }
}

void VoxelColliderComponent::_finishBuild() {
    // Take ownership of the result and clear _pending first, so IsBuilding()
    // is already false by the time anything this function calls can observe it.
    const std::shared_ptr<PendingBuild> pending = std::move(_pending);
    _pending.reset();

    if (!pending->shape) {
        if (auto* console = ConsoleSubsystem::Get()) {
            console->Warn("VoxelCollider: nothing to collide with — no body created");
        }
        return;
    }
    _vertices = std::move(pending->vertices);
    _indices = std::move(pending->indices);
    _meshInterface = std::move(pending->meshInterface);
    _triangleInfo = std::move(pending->triangleInfo);
    _shape = std::move(pending->shape);
    _centerOfMass = pending->centerOfMass;
    _triangleCount = pending->triangleCount;
    _hullPointCount = pending->hullPointCount;

    auto* volume = gameObject ? gameObject->GetComponent<VoxelVolumeComponent>() : nullptr;
    if (volume == nullptr) return;

    RigidbodyProps rbProps;
    rbProps.shape = _shape.get();
    rbProps.mass = pending->mass;
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
                ? fmt::format("VoxelCollider: convex hull, {} points, mass {:.1f} kg", _hullPointCount, pending->mass)
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
