#include "physics_subsystem_3d.hpp"
#include "LinearMath/btThreads.h"
#include "bullet_task_scheduler.hpp"
#include "game_object.hpp"
#include "job_system.hpp"
#include "physics_debug_drawer.hpp"
#include "rigidbody_component.hpp"
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletCollision/CollisionDispatch/btInternalEdgeUtility.h>
#include <BulletCollision/CollisionDispatch/btManifoldResult.h>// gContactAddedCallback
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>// also declares btConstraintSolverPoolMt
#include <algorithm>
#include <spdlog/spdlog.h>

class RaycastCallback : public btCollisionWorld::ClosestRayResultCallback {
private:
    btVector3 _m_rayFromWorld;
    btVector3 _m_rayToWorld;

public:
    // float m_hitDistance;

    RaycastCallback(const btVector3& rayFromWorld, const btVector3& rayToWorld)
      : btCollisionWorld::ClosestRayResultCallback(rayFromWorld, rayToWorld), _m_rayFromWorld(rayFromWorld),
        _m_rayToWorld(rayToWorld) {
    }

    btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace) override {
        btScalar result = ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
        // m_hitDistance = rayResult.m_hitFraction * (m_rayFromWorld.distance(m_rayToWorld));
        return result;
    }
};

Physics3DSubsystem* Physics3DSubsystem::_instance = nullptr;

Physics3DSubsystem::Physics3DSubsystem() {
    if (_instance != nullptr) throw std::runtime_error("Physics server is already initialized!");

    _instance = this;
}

Physics3DSubsystem::~Physics3DSubsystem() {
    // It's important to set the task scheduler to null before the world and
    // other resources are destroyed (which happens automatically, in reverse
    // declaration order, right after this body runs).
    btSetTaskScheduler(nullptr);
    if (_instance == this) {
        _instance = nullptr;
    }
}

// Internal-edge correction for triangle-mesh colliders. A box resting across a
// triangle soup generates contacts on the shared edges between triangles, and
// the raw contact normal there points along the edge rather than out of the
// surface — so bodies get shoved sideways or downwards, which reads as endless
// jitter and, on a mesh (a shell, not a solid), as slowly sinking through it.
// Bullet fixes this by snapping such normals back to the face normal, using the
// triangle adjacency map built by btGenerateInternalEdgeInfo. Only bodies that
// opt in with CF_CUSTOM_MATERIAL_CALLBACK reach this, so nothing else is
// affected.
static bool MvInternalEdgeContactCallback(
    btManifoldPoint& cp, const btCollisionObjectWrapper* colObj0Wrap, int partId0, int index0,
    const btCollisionObjectWrapper* colObj1Wrap, int partId1, int index1
) {
    // Whichever side is the triangle is the one to correct against.
    if (colObj1Wrap->getCollisionShape()->getShapeType() == TRIANGLE_SHAPE_PROXYTYPE) {
        btAdjustInternalEdgeContacts(cp, colObj1Wrap, colObj0Wrap, partId1, index1);
    } else if (colObj0Wrap->getCollisionShape()->getShapeType() == TRIANGLE_SHAPE_PROXYTYPE) {
        btAdjustInternalEdgeContacts(cp, colObj0Wrap, colObj1Wrap, partId0, index0);
    }
    return true;
}

void Physics3DSubsystem::Init(Application* app) {
    Subsystem::Init(app);

    // Create and set the custom task scheduler
    _taskScheduler = std::make_unique<BulletTaskScheduler>(*JobSystem::Get());
    btSetTaskScheduler(_taskScheduler.get());

    auto* scheduler = btGetTaskScheduler();
    spdlog::info("[Physics] Bullet worker threads: {}", scheduler ? scheduler->getNumThreads() : 0);
    spdlog::info("[Physics] JobSystem threads: {}", JobSystem::Get()->GetThreadCount());

    _config = std::make_unique<btDefaultCollisionConfiguration>();
    // Use multithreaded dispatcher if thread safe
#ifdef BT_THREADSAFE
    _dispatcher = std::make_unique<btCollisionDispatcherMt>(_config.get(), JobSystem::Get()->GetThreadCount());
#else
    _dispatcher = std::make_unique<btCollisionDispatcher>(_config.get());
#endif
    _broadphase = std::make_unique<btDbvtBroadphase>();

    // Use parallel solver
    _solver = std::make_unique<btSequentialImpulseConstraintSolverMt>();

#ifdef BT_THREADSAFE
    // Bullet's island-level parallelism lives in btDiscreteDynamicsWorldMt, not
    // in the solver: it hands each simulation island to a solver taken from the
    // pool (one per thread, mutex-guarded, so it never spin-waits as long as
    // the pool is at least thread-count deep) and falls back to the single
    // multi-threaded solver for islands large enough to be worth parallelising
    // internally. The plain btDiscreteDynamicsWorld ignores all of that and
    // solves islands serially, which left the Mt dispatcher and Mt solver above
    // doing only half the job — exactly the part that costs most when many
    // bodies pile up and merge into one big island.
    const int solverCount = std::max(1, scheduler ? scheduler->getNumThreads() : 1);
    _solverPool = std::make_unique<btConstraintSolverPoolMt>(solverCount);
    _world = std::make_unique<btDiscreteDynamicsWorldMt>(
        _dispatcher.get(), _broadphase.get(), _solverPool.get(), _solver.get(), _config.get()
    );
    spdlog::info("[Physics] Multithreaded world, {} pooled constraint solvers", solverCount);
#else
    _world =
        std::make_unique<btDiscreteDynamicsWorld>(_dispatcher.get(), _broadphase.get(), _solver.get(), _config.get());
    spdlog::info("[Physics] Single-threaded world (BT_THREADSAFE not defined)");
#endif
    SetGravity(glm::vec3(0, -GRAVITY, 0));

    gContactAddedCallback = MvInternalEdgeContactCallback;

    // Bullet's solver defaults assume metre-scale bodies. Split impulse — the
    // mechanism that pushes overlapping bodies apart WITHOUT feeding the energy
    // back as bounce — only engages past m_splitImpulsePenetrationThreshold,
    // and its default of -4 cm is deeper than a 5 cm voxel: at this scale a
    // resting body's overlap never reaches it, so recovery goes through the
    // normal impulse instead and the body visibly buzzes. Pull the threshold
    // down to a fraction of a voxel.
    btContactSolverInfo& solverInfo = _world->getSolverInfo();
    solverInfo.m_splitImpulsePenetrationThreshold = -0.01f;

    _debugDrawer = std::make_unique<PhysicsDebugDrawer>();
    _world->setDebugDrawer(_debugDrawer.get());
    _debugDrawer->setDebugMode(1);

    _timeAccum = 0.0f;
}

void Physics3DSubsystem::Process(float dt) {
#ifdef TRACY_ENABLE
    ZoneScopedN("Physics3DSubsystem::Process");
#endif
    // Fixed-step catch-up, CAPPED. Without a cap this is the classic spiral of
    // death: one slow frame (or a long blocking load — a big voxel collider
    // build is seconds) leaves an accumulator needing more substeps than the
    // next frame can afford, so that frame is slower still and the backlog
    // grows without bound. Past the cap we drop the backlog and let simulated
    // time slip behind wall time, which is the only stable choice.
    constexpr int MAX_PHYSICS_STEPS_PER_FRAME = 4;
    _timeAccum += dt;
    int stepsThisFrame = 0;
    while (_timeAccum >= FIXED_TIME_STEP && stepsThisFrame < MAX_PHYSICS_STEPS_PER_FRAME) {
        _world->stepSimulation(FIXED_TIME_STEP, 0);
        _timeAccum -= FIXED_TIME_STEP;
        stepsThisFrame++;
    }
    if (_timeAccum > FIXED_TIME_STEP * MAX_PHYSICS_STEPS_PER_FRAME) {
        _timeAccum = FIXED_TIME_STEP;
    }

    int numManifolds = _dispatcher->getNumManifolds();
    for (int i = 0; i < numManifolds; i++) {
        btPersistentManifold* contactManifold = _dispatcher->getManifoldByIndexInternal(i);

        auto* objA = const_cast<btCollisionObject*>(contactManifold->getBody0());
        auto* objB = const_cast<btCollisionObject*>(contactManifold->getBody1());

        auto* gameObjA = static_cast<GameObject*>(objA->getUserPointer());
        auto* gameObjB = static_cast<GameObject*>(objB->getUserPointer());

        if (gameObjA && gameObjB) {
            if (contactManifold->getNumContacts() > 0) {
                gameObjA->OnCollision(gameObjB);
                gameObjB->OnCollision(gameObjA);
            }
        }
    }

    if (_debugUIEnabled) {
        _world->debugDrawWorld();// TODO: check if this cost performance when debug mode is NoDebug
    }
}

void Physics3DSubsystem::DrawImGui(float dt) {
    if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Number of manifolds: %d", _dispatcher->getNumManifolds());
        if (ImGui::Button("Debug UI")) {
            EnableDebugUI(!_debugUIEnabled);
        }

        ImGui::Separator();

        if (ImGui::TreeNode("Rigidbodies")) {
            for (auto i : _impostors) {
                ImGui::Text("%s (rigidbody)", i->gameObject->GetName().c_str());
            }
            ImGui::TreePop();
        }
    }
}

void Physics3DSubsystem::Reset() {
    // Components are owned by their GameObjects (which unregister themselves
    // via OnDetach on destruction); here we only detach whatever is left from
    // the simulation, we never delete the components.
    for (auto* impostor : _impostors) {
        _world->removeRigidBody(impostor->_rigidbody.get());
    }
    _impostors.clear();
}

void Physics3DSubsystem::AddRigidbody(RigidbodyComponent* impostor) {
    _world->addRigidBody(impostor->_rigidbody.get());
    _impostors.push_back(impostor);
}

void Physics3DSubsystem::RemoveRigidbody(RigidbodyComponent* impostor) {
    _world->removeRigidBody(impostor->_rigidbody.get());
    _impostors.erase(std::remove(_impostors.begin(), _impostors.end(), impostor), _impostors.end());
}

ColliderID Physics3DSubsystem::CreateCollider(const Shape& shape) {
    btCollisionShape* col = nullptr;
    switch (shape.type) {
    case ShapeType::Cube:
        col = new btBoxShape(btVector3(
            0.5f * shape.data.cubeData.size.x, 0.5f * shape.data.cubeData.size.y, 0.5f * shape.data.cubeData.size.z
        ));
        break;
    case ShapeType::Sphere:
        col = new btSphereShape(shape.data.sphereData.radius);
        break;
    case ShapeType::Capsule:
        col = new btCapsuleShape(shape.data.capsuleData.radius, shape.data.capsuleData.height);
        break;
    case ShapeType::Cylinder:
        col = new btCylinderShape(
            btVector3(shape.data.cylinderData.radius, shape.data.cylinderData.height, shape.data.cylinderData.radius)
        );
        break;
    case ShapeType::Cone:
        col = new btConeShape(shape.data.coneData.radius, shape.data.coneData.height);
        break;
    default:
        throw std::runtime_error("Invalid shape type");
    }
    _colliders[_nextColliderID] = std::unique_ptr<btCollisionShape>(col);
    return _nextColliderID++;
}

void Physics3DSubsystem::DestroyCollider(ColliderID col) {
    _colliders.erase(col);
}

bool Physics3DSubsystem::Raycast(const glm::vec3& from, const glm::vec3& to, RaycastHit& hit) {
    btVector3 rayFrom(from.x, from.y, from.z);
    btVector3 rayTo(to.x, to.y, to.z);

    RaycastCallback callback(rayFrom, rayTo);
    _world->rayTest(rayFrom, rayTo, callback);

    if (callback.hasHit()) {
        btVector3 hitPoint = callback.m_hitPointWorld;
        btVector3 hitNormal = callback.m_hitNormalWorld;
        auto* hitObject = static_cast<GameObject*>(callback.m_collisionObject->getUserPointer());
        hit.point = glm::vec3(hitPoint.x(), hitPoint.y(), hitPoint.z());
        hit.normal = glm::vec3(hitNormal.x(), hitNormal.y(), hitNormal.z());
        hit.gameObject = hitObject;
        return true;
    } else {
        return false;
    }
}

void Physics3DSubsystem::SetGravity(const glm::vec3& acc) {
    _world->setGravity(btVector3(acc.x, acc.y, acc.z));
}

void Physics3DSubsystem::EnableDebugUI(bool enable) {
    _debugUIEnabled = enable;
}