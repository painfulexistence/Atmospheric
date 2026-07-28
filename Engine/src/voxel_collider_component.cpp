#include "voxel_collider_component.hpp"
#include "bullet_collision.hpp"
#include "bullet_dynamics.hpp"
#include "bullet_linear_math.hpp"
#include <BulletCollision/CollisionDispatch/btInternalEdgeUtility.h>
#include <BulletCollision/CollisionShapes/btTriangleInfoMap.h>
#include "console_subsystem.hpp"
#include "game_object.hpp"
#include "job_system.hpp"
#include "physics_subsystem_3d.hpp"
#include "rigidbody_component.hpp"
#include "voxel_volume_component.hpp"

#include <algorithm>
#include <atomic>
#include <fmt/format.h>
#include <imgui.h>
#include <thread>

// A live piece of the static collider. Everything here is referenced by raw
// pointer from Bullet, so it is owned for as long as the body exists.
struct VoxelColliderComponent::Chunk {
    glm::ivec3 min{ 0 };// voxel bounds, inclusive
    glm::ivec3 max{ 0 };
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    std::unique_ptr<btTriangleIndexVertexArray> meshInterface;
    std::unique_ptr<btTriangleInfoMap> triangleInfo;
    std::unique_ptr<btCollisionShape> shape;
    // Owned here rather than attached to the GameObject: a terrain has many of
    // these, and they are not something the object model should have to model
    // as many RigidbodyComponents. Registered with the physics subsystem
    // directly, and unregistered before destruction.
    std::unique_ptr<RigidbodyComponent> body;
    int triangleCount = 0;
};

struct VoxelColliderComponent::PendingBuild {
    // What one worker pass produced. A chunk with a null shape has become
    // empty and its body is dropped.
    struct ChunkResult {
        int index = -1;
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;
        std::unique_ptr<btTriangleIndexVertexArray> meshInterface;
        std::unique_ptr<btTriangleInfoMap> triangleInfo;
        std::unique_ptr<btCollisionShape> shape;
        int triangleCount = 0;
    };
    // Which chunks this pass covers, and their extracted geometry.
    std::vector<glm::ivec3> chunkMin, chunkMax;
    std::vector<int> chunkIndex;
    std::vector<ChunkResult> results;
    // Dynamic-hull path.
    std::unique_ptr<btCollisionShape> hull;
    glm::vec3 centerOfMass{ 0.0f };
    float mass = 0.0f;
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
    _releaseAll();
}

void VoxelColliderComponent::OnAttach() {
    _beginBuild();
}

void VoxelColliderComponent::OnDetach() {
    // The worker reads the sibling VoxelVolumeComponent's voxels, and component
    // destruction order within a GameObject is not ours to pick, so the wait
    // has to happen while the object is still whole.
    _waitForBuild();
    _releaseAll();
}

void VoxelColliderComponent::OnTick(float /*dt*/) {
    if (_pending) {
        if (_pending->done.load(std::memory_order_acquire)) _finishBuild();
        return;
    }
    // Anything marked while the last pass was running gets picked up here.
    if (!_dirtyChunks.empty()) {
        _beginBuild();
        return;
    }
    if (_props.dynamic && !_destroyed) _checkDestruction();
}

void VoxelColliderComponent::_checkDestruction() {
    auto* volume = gameObject ? gameObject->GetComponent<VoxelVolumeComponent>() : nullptr;
    if (volume == nullptr || _initialSolidCount == 0) return;

    const float remaining = static_cast<float>(volume->solidCount) / static_cast<float>(_initialSolidCount);
    if (_props.destroyBelowSolidFraction > 0.0f && remaining < _props.destroyBelowSolidFraction) {
        _destroyed = true;
        if (auto* console = ConsoleSubsystem::Get()) {
            console->Info(fmt::format(
                "VoxelCollider: prop destroyed ({:.0f}% of its voxels left)", remaining * 100.0f
            ));
        }
        // The game decides what destruction looks like; the engine only says
        // when. Called last so a handler is free to remove this component.
        if (OnDestroyed) OnDestroyed(*this);
        return;
    }

    // Still standing: re-hull once enough has been carved away for the
    // silhouette to have actually changed. Carving the middle out of a crate
    // changes nothing here, which is correct — a hull cannot represent that,
    // and re-running it every dig frame would be pure waste.
    if (_props.rehullAfterSolidFraction <= 0.0f) return;
    if (volume->solidCount >= _solidCountAtLastBuild) return;
    const float carvedSinceBuild =
        static_cast<float>(_solidCountAtLastBuild - volume->solidCount) / static_cast<float>(_initialSolidCount);
    if (carvedSinceBuild >= _props.rehullAfterSolidFraction) _beginBuild();
}

void VoxelColliderComponent::Rebuild() {
    _waitForBuild();
    if (_props.dynamic) {
        _beginBuild();
        return;
    }
    _dirtyChunks.clear();
    for (size_t i = 0; i < _chunks.size(); i++) _dirtyChunks.push_back(static_cast<int>(i));
    _beginBuild();
}

void VoxelColliderComponent::MarkDirtyRegion(const glm::ivec3& voxelMin, const glm::ivec3& voxelMax) {
    if (_props.dynamic || _chunks.empty()) return;
    for (size_t i = 0; i < _chunks.size(); i++) {
        const Chunk& c = _chunks[i];
        // Grow the test by one voxel: removing a voxel at a chunk's edge
        // uncovers a face on the far side of the boundary, which belongs to
        // the neighbour. Missing that would leave a one-voxel wall standing
        // where the dig went through.
        if (voxelMax.x < c.min.x - 1 || voxelMin.x > c.max.x + 1) continue;
        if (voxelMax.y < c.min.y - 1 || voxelMin.y > c.max.y + 1) continue;
        if (voxelMax.z < c.min.z - 1 || voxelMin.z > c.max.z + 1) continue;
        const int idx = static_cast<int>(i);
        if (std::find(_dirtyChunks.begin(), _dirtyChunks.end(), idx) == _dirtyChunks.end()) {
            _dirtyChunks.push_back(idx);
        }
    }
}

int VoxelColliderComponent::GetDirtyChunkCount() const {
    return static_cast<int>(_dirtyChunks.size());
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

void VoxelColliderComponent::_releaseChunk(Chunk& chunk) {
    if (chunk.body) {
        if (Physics3DSubsystem::Get()) Physics3DSubsystem::Get()->RemoveRigidbody(chunk.body.get());
        chunk.body.reset();
    }
    chunk.shape.reset();
    chunk.triangleInfo.reset();
    chunk.meshInterface.reset();
    chunk.vertices.clear();
    chunk.indices.clear();
    chunk.triangleCount = 0;
}

void VoxelColliderComponent::_releaseAll() {
    for (Chunk& c : _chunks) _releaseChunk(c);
    _chunks.clear();
    _dirtyChunks.clear();
    _hull.reset();
    _triangleCount = 0;
}

void VoxelColliderComponent::_initChunks(const VoxelVolumeComponent& volume) {
    _chunks.clear();
    const int N = volume.gridDim;
    // Round the requested size to a multiple of brickDim so a chunk never
    // splits a brick — the mesher's occupancy fast path assumes that, and the
    // coarsening step is a divisor of brickDim too, so regions stay aligned.
    int cv = _props.chunkVoxels;
    if (cv > 0) {
        cv = std::max(volume.brickDim, (cv / volume.brickDim) * volume.brickDim);
        cv = std::min(cv, N);
    }
    if (cv <= 0) {
        Chunk whole;
        whole.min = glm::ivec3(0);
        whole.max = glm::ivec3(N - 1);
        _chunks.push_back(std::move(whole));
    } else {
        for (int z = 0; z < N; z += cv) {
            for (int y = 0; y < N; y += cv) {
                for (int x = 0; x < N; x += cv) {
                    Chunk c;
                    c.min = glm::ivec3(x, y, z);
                    c.max = glm::min(glm::ivec3(x + cv - 1, y + cv - 1, z + cv - 1), glm::ivec3(N - 1));
                    _chunks.push_back(std::move(c));
                }
            }
        }
    }
    _dirtyChunks.clear();
    for (size_t i = 0; i < _chunks.size(); i++) _dirtyChunks.push_back(static_cast<int>(i));
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
        if (_chunks.empty()) _initChunks(*volume);
        if (_dirtyChunks.empty()) return;
    }

    // Extraction is the expensive half — a 256^3 terrain is ~60 ms of meshing
    // plus the BVH build, which used to land inline on the frame the volume
    // finished generating. It reads finished voxel data and touches no engine
    // state, so it moves to a worker; only body creation, which mutates the
    // Bullet world, stays on the main thread (see _finishBuild).
    //
    // The job holds the result by shared_ptr, so a component destroyed
    // mid-build does not leave it writing into freed memory. It also reads the
    // volume by raw pointer, which is why teardown waits (see _waitForBuild):
    // the volume must outlive the job, and so must the voxels, which means
    // nothing may carve while a build is in flight.
    // Snapshot what this build is being made from. The first one also fixes the
    // baseline the destroy threshold is measured against, so a prop spawned
    // already partly carved is judged against how it spawned, not against a
    // full grid it never had.
    if (_initialSolidCount == 0) _initialSolidCount = volume->solidCount;
    _solidCountAtLastBuild = volume->solidCount;

    auto pending = std::make_shared<PendingBuild>();
    if (!_props.dynamic) {
        for (int idx : _dirtyChunks) {
            pending->chunkIndex.push_back(idx);
            pending->chunkMin.push_back(_chunks[static_cast<size_t>(idx)].min);
            pending->chunkMax.push_back(_chunks[static_cast<size_t>(idx)].max);
        }
        _dirtyChunks.clear();
    }
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
        out.hull = std::move(hull);

        out.mass = props.mass;
        if (out.mass <= 0.0f) {
            const float v = volume.voxelSize;
            out.mass = static_cast<float>(volume.solidCount) * v * v * v * props.density;
            out.mass = std::max(out.mass, 0.01f);// never hand Bullet a zero mass for a dynamic body
        }
        return;
    }

    out.results.resize(out.chunkIndex.size());
    for (size_t k = 0; k < out.chunkIndex.size(); k++) {
        auto& r = out.results[k];
        r.index = out.chunkIndex[k];
        volume.BuildSurfaceMeshRegion(r.vertices, r.indices, out.chunkMin[k], out.chunkMax[k], props.meshDownsample);
        if (r.indices.size() < 3) continue;// chunk is empty (or became empty)
        r.triangleCount = static_cast<int>(r.indices.size() / 3);

        // Bullet reads these arrays on every query, so they stay owned by the
        // chunk. Moving the vectors over later keeps the same heap buffer, so
        // the pointers taken now stay valid.
        btIndexedMesh mesh;
        mesh.m_numTriangles = r.triangleCount;
        mesh.m_triangleIndexBase = reinterpret_cast<const unsigned char*>(r.indices.data());
        mesh.m_triangleIndexStride = 3 * static_cast<int>(sizeof(uint32_t));
        mesh.m_numVertices = static_cast<int>(r.vertices.size());
        mesh.m_vertexBase = reinterpret_cast<const unsigned char*>(r.vertices.data());
        mesh.m_vertexStride = static_cast<int>(sizeof(glm::vec3));
        mesh.m_indexType = PHY_INTEGER;
        mesh.m_vertexType = PHY_FLOAT;

        r.meshInterface = std::make_unique<btTriangleIndexVertexArray>();
        r.meshInterface->addIndexedMesh(mesh, PHY_INTEGER);
        // The BVH is built right here, in the constructor — which is most of
        // why this whole function belongs off the main thread.
        auto meshShape =
            std::make_unique<btBvhTriangleMeshShape>(r.meshInterface.get(), /*useQuantizedAabbCompression=*/true);
        meshShape->setMargin(margin);

        // Internal-edge adjacency. Without it, contacts landing on the shared
        // edge between two triangles get a normal along the edge instead of out
        // of the surface, which shoves resting bodies sideways (jitter) and
        // down into the shell (sinking through). The default edge threshold is
        // 0.1 m — larger than a collision cell here, which would classify every
        // contact as an edge contact — so scale it to the voxel grid.
        r.triangleInfo = std::make_unique<btTriangleInfoMap>();
        r.triangleInfo->m_edgeDistanceThreshold = volume.voxelSize * 0.5f;
        btGenerateInternalEdgeInfo(meshShape.get(), r.triangleInfo.get());

        r.shape = std::move(meshShape);
    }
}

void VoxelColliderComponent::_finishBuild() {
    // Take ownership of the result and clear _pending first, so IsBuilding()
    // is already false by the time anything this function calls can observe it.
    const std::shared_ptr<PendingBuild> pending = std::move(_pending);
    _pending.reset();

    auto* volume = gameObject ? gameObject->GetComponent<VoxelVolumeComponent>() : nullptr;
    if (volume == nullptr) return;

    if (_props.dynamic) {
        if (!pending->hull) {
            if (auto* console = ConsoleSubsystem::Get()) {
                console->Warn("VoxelCollider: nothing to collide with — no body created");
            }
            return;
        }
        // Hold what the body currently points at until the swap is done —
        // Bullet reads the shape, so freeing it first would leave it
        // dereferencing dead memory.
        auto oldHull = std::move(_hull);
        _hull = std::move(pending->hull);
        _centerOfMass = pending->centerOfMass;
        _hullPointCount = pending->hullPointCount;

        // A rebuild keeps the existing body so its velocity, contacts and place
        // in the world survive; only the first build creates one.
        if (auto* existing = gameObject->GetComponent<RigidbodyComponent>()) {
            existing->SwapShape(_hull.get(), pending->mass, _centerOfMass);
            return;
        }
        RigidbodyProps rbProps;
        rbProps.shape = _hull.get();
        rbProps.mass = pending->mass;
        rbProps.friction = _props.friction;
        rbProps.restitution = _props.restitution;
        rbProps.centerOfMass = _centerOfMass;
        auto* body = new RigidbodyComponent(gameObject, rbProps);
        gameObject->AddComponent(body);
        if (_props.continuousCollision) {
            // Sweep once the body would move more than half its thinnest side
            // in a step; the swept sphere is a fraction of that so it
            // approximates the shape without over-triggering.
            const glm::vec3 size = volume->GetLocalBoundsMax() - volume->GetLocalBoundsMin();
            const float minExtent = std::max(std::min({ size.x, size.y, size.z }), volume->voxelSize);
            body->SetContinuousCollision(minExtent * 0.5f, minExtent * 0.2f);
        }
        if (auto* console = ConsoleSubsystem::Get()) {
            console->Info(
                fmt::format("VoxelCollider: convex hull, {} points, mass {:.1f} kg", _hullPointCount, pending->mass)
            );
        }
        return;
    }

    for (auto& r : pending->results) {
        if (r.index < 0 || static_cast<size_t>(r.index) >= _chunks.size()) continue;
        Chunk& chunk = _chunks[static_cast<size_t>(r.index)];

        if (!r.shape) {
            // Everything in this chunk was dug away; it holds no surface now.
            _releaseChunk(chunk);
            continue;
        }
        // A chunk's body is dropped and rebuilt rather than shape-swapped: it
        // is static, so it has no velocity worth preserving, and this keeps
        // the geometry and the body that reads it changing together.
        _releaseChunk(chunk);
        chunk.vertices = std::move(r.vertices);
        chunk.indices = std::move(r.indices);
        chunk.meshInterface = std::move(r.meshInterface);
        chunk.triangleInfo = std::move(r.triangleInfo);
        chunk.shape = std::move(r.shape);
        chunk.triangleCount = r.triangleCount;

        RigidbodyProps rbProps;
        rbProps.shape = chunk.shape.get();
        rbProps.mass = 0.0f;// static
        rbProps.friction = _props.friction;
        rbProps.restitution = _props.restitution;
        chunk.body = std::make_unique<RigidbodyComponent>(gameObject, rbProps);
        // Opt into the contact-added callback that performs the internal-edge
        // correction (see Physics3DSubsystem).
        chunk.body->SetCustomMaterialCallback(true);
        if (Physics3DSubsystem::Get()) Physics3DSubsystem::Get()->AddRigidbody(chunk.body.get());
    }

    _triangleCount = 0;
    int liveChunks = 0;
    for (const Chunk& c : _chunks) {
        _triangleCount += c.triangleCount;
        if (c.body) liveChunks++;
    }
    if (auto* console = ConsoleSubsystem::Get()) {
        console->Info(fmt::format(
            "VoxelCollider: static mesh, {} triangles across {} chunk(s)", _triangleCount, liveChunks
        ));
    }
}

void VoxelColliderComponent::DrawImGui() {
    ImGui::Text("%s", _props.dynamic ? "convex hull (dynamic)" : "triangle mesh (static)");
    if (_props.dynamic) {
        ImGui::Text("%d hull points", _hullPointCount);
        ImGui::Text("CoM %.2f, %.2f, %.2f (local)", _centerOfMass.x, _centerOfMass.y, _centerOfMass.z);
        if (auto* vol = gameObject ? gameObject->GetComponent<VoxelVolumeComponent>() : nullptr) {
            const float pct = _initialSolidCount > 0
                                  ? 100.0f * static_cast<float>(vol->solidCount) / static_cast<float>(_initialSolidCount)
                                  : 100.0f;
            ImGui::Text("%.0f%% intact%s (destroys below %.0f%%)", pct, _destroyed ? " DESTROYED" : "",
                        _props.destroyBelowSolidFraction * 100.0f);
        }
    } else {
        int live = 0;
        for (const Chunk& c : _chunks)
            if (c.body) live++;
        ImGui::Text("%d triangles, %d/%zu chunks", _triangleCount, live, _chunks.size());
        ImGui::Text("%d dirty%s", GetDirtyChunkCount(), IsBuilding() ? " (building)" : "");
    }
    if (ImGui::Button("Rebuild collider")) {
        Rebuild();
    }
}
