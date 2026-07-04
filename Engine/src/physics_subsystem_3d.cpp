#include "physics_subsystem_3d.hpp"
#include "log.hpp"
#include "LinearMath/btThreads.h"
#include "bullet_task_scheduler.hpp"
#include "game_object.hpp"
#include "job_system.hpp"
#include "physics_debug_drawer.hpp"
#include "rigidbody_component.hpp"
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include <algorithm>
#include <spdlog/spdlog.h>

class RaycastCallback : public btCollisionWorld::ClosestRayResultCallback {
private:
    btVector3 _m_rayFromWorld;
    btVector3 _m_rayToWorld;

public:
    // float _hitDistance;

    RaycastCallback(const btVector3& rayFromWorld, const btVector3& rayToWorld)
      : btCollisionWorld::ClosestRayResultCallback(rayFromWorld, rayToWorld), _m_rayFromWorld(rayFromWorld),
        _m_rayToWorld(rayToWorld) {
    }

    btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace) override {
        btScalar result = ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
        // _hitDistance = rayResult._hitFraction * (_rayFromWorld.distance(_rayToWorld));
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

void Physics3DSubsystem::Init(Application* app) {
    Subsystem::Init(app);

    // Create and set the custom task scheduler
    _taskScheduler = std::make_unique<BulletTaskScheduler>(*JobSystem::Get());
    btSetTaskScheduler(_taskScheduler.get());

    auto* scheduler = btGetTaskScheduler();
    Log::Info("[Physics] Bullet worker threads: {}", scheduler ? scheduler->getNumThreads() : 0);
    Log::Info("[Physics] JobSystem threads: {}", JobSystem::Get()->GetThreadCount());

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

    _world =
        std::make_unique<btDiscreteDynamicsWorld>(_dispatcher.get(), _broadphase.get(), _solver.get(), _config.get());
    SetGravity(glm::vec3(0, -GRAVITY, 0));

    _debugDrawer = std::make_unique<PhysicsDebugDrawer>();
    _world->setDebugDrawer(_debugDrawer.get());
    _debugDrawer->setDebugMode(1);

    _timeAccum = 0.0f;
}

void Physics3DSubsystem::Process(float dt) {
#ifdef TRACY_ENABLE
    ZoneScopedN("Physics3DSubsystem::Process");
#endif
    _timeAccum += dt;
    while (_timeAccum >= FIXED_TIME_STEP) {
        _world->stepSimulation(FIXED_TIME_STEP, 0);
        _timeAccum -= FIXED_TIME_STEP;
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
        btVector3 hitPoint = callback._hitPointWorld;
        btVector3 hitNormal = callback._hitNormalWorld;
        auto* hitObject = static_cast<GameObject*>(callback._collisionObject->getUserPointer());
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