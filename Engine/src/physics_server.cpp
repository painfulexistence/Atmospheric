#include "physics_server.hpp"
#include "LinearMath/btThreads.h"
#include "bullet_task_scheduler.hpp"
#include "game_object.hpp"
#include "job_system.hpp"
#include "physics_debug_drawer.hpp"
#include "rigidbody_component.hpp"
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include <spdlog/spdlog.h>

class RaycastCallback : public btCollisionWorld::ClosestRayResultCallback {
private:
    btVector3 m_rayFromWorld;
    btVector3 m_rayToWorld;

public:
    // float m_hitDistance;

    RaycastCallback(const btVector3& rayFromWorld, const btVector3& rayToWorld)
      : btCollisionWorld::ClosestRayResultCallback(rayFromWorld, rayToWorld), m_rayFromWorld(rayFromWorld),
        m_rayToWorld(rayToWorld) {
    }

    btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace) override {
        btScalar result = ClosestRayResultCallback::addSingleResult(rayResult, normalInWorldSpace);
        // m_hitDistance = rayResult.m_hitFraction * (m_rayFromWorld.distance(m_rayToWorld));
        return result;
    }
};

PhysicsServer* PhysicsServer::_instance = nullptr;

PhysicsServer::PhysicsServer() {
    if (_instance != nullptr) throw std::runtime_error("Physics server is already initialized!");

    _instance = this;
}

PhysicsServer::~PhysicsServer() {
    // It's important to set the task scheduler to null before the world and
    // other resources are destroyed (which happens automatically, in reverse
    // declaration order, right after this body runs).
    btSetTaskScheduler(nullptr);
}

void PhysicsServer::Init(Application* app) {
    Server::Init(app);

    // Create and set the custom task scheduler
    _taskScheduler = std::make_unique<BulletTaskScheduler>(*JobSystem::Get());
    btSetTaskScheduler(_taskScheduler.get());

    auto* scheduler = btGetTaskScheduler();
    spdlog::info("[Physics] Bullet worker threads: {}",
        scheduler ? scheduler->getNumThreads() : 0);
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

    _world = std::make_unique<btDiscreteDynamicsWorld>(_dispatcher.get(), _broadphase.get(), _solver.get(), _config.get());
    SetGravity(glm::vec3(0, -GRAVITY, 0));

    _debugDrawer = std::make_unique<PhysicsDebugDrawer>();
    _world->setDebugDrawer(_debugDrawer.get());
    _debugDrawer->setDebugMode(1);

    _timeAccum = 0.0f;
}

void PhysicsServer::Process(float dt) {
#ifdef TRACY_ENABLE
    ZoneScopedN("PhysicsServer::Process");
#endif
    _timeAccum += dt;
    while (_timeAccum >= FIXED_TIME_STEP) {
        _world->stepSimulation(FIXED_TIME_STEP, 0);
        _timeAccum -= FIXED_TIME_STEP;
    }

    int numManifolds = _dispatcher->getNumManifolds();
    for (int i = 0; i < numManifolds; i++) {
        btPersistentManifold* contactManifold = _dispatcher->getManifoldByIndexInternal(i);

        btCollisionObject* objA = const_cast<btCollisionObject*>(contactManifold->getBody0());
        btCollisionObject* objB = const_cast<btCollisionObject*>(contactManifold->getBody1());

        GameObject* gameObjA = static_cast<GameObject*>(objA->getUserPointer());
        GameObject* gameObjB = static_cast<GameObject*>(objB->getUserPointer());

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

void PhysicsServer::DrawImGui(float dt) {
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

void PhysicsServer::Reset() {
    for (auto impostor : _impostors) {
        _world->removeRigidBody(impostor->_rigidbody);
        delete impostor;
    }
    _impostors.clear();
}

void PhysicsServer::AddRigidbody(RigidbodyComponent* impostor) {
    _world->addRigidBody(impostor->_rigidbody);
    _impostors.push_back(impostor);
}

void PhysicsServer::RemoveRigidbody(RigidbodyComponent* impostor) {
    _world->removeRigidBody(impostor->_rigidbody);
}

ColliderID PhysicsServer::CreateCollider(const Shape& shape) {
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

void PhysicsServer::DestroyCollider(ColliderID col) {
    _colliders.erase(col);
}

bool PhysicsServer::Raycast(const glm::vec3& from, const glm::vec3& to, RaycastHit& hit) {
    btVector3 rayFrom(from.x, from.y, from.z);
    btVector3 rayTo(to.x, to.y, to.z);

    RaycastCallback callback(rayFrom, rayTo);
    _world->rayTest(rayFrom, rayTo, callback);

    if (callback.hasHit()) {
        btVector3 hitPoint = callback.m_hitPointWorld;
        btVector3 hitNormal = callback.m_hitNormalWorld;
        GameObject* hitObject = static_cast<GameObject*>(callback.m_collisionObject->getUserPointer());
        hit.point = glm::vec3(hitPoint.x(), hitPoint.y(), hitPoint.z());
        hit.normal = glm::vec3(hitNormal.x(), hitNormal.y(), hitNormal.z());
        hit.gameObject = hitObject;
        return true;
    } else {
        return false;
    }
}

void PhysicsServer::SetGravity(const glm::vec3& acc) {
    _world->setGravity(btVector3(acc.x, acc.y, acc.z));
}

void PhysicsServer::EnableDebugUI(bool enable) {
    _debugUIEnabled = enable;
}