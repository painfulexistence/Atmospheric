#pragma once
#include "globals.hpp"
#include "server.hpp"

class GameObject;

enum class PhysicsDebugMode {
    NONE = 0,
    WIREFRAME = 1,
};

struct RaycastHit {
    glm::vec3 point;
    glm::vec3 normal;
    GameObject* gameObject = nullptr;
    float hitDistance = 0.0f;
    float hitFraction = 0.0f;
};

class btCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btConstraintSolver;
class btDiscreteDynamicsWorld;
class btCollisionShape;
class RaycastCallback;
class PhysicsDebugDrawer;
class RigidbodyComponent;
class BulletTaskScheduler;

using ColliderID = uint32_t;

class PhysicsServer : public Server
{
private:
    static PhysicsServer* _instance;

public:
    static PhysicsServer* Get()
    {
        return _instance;
    }

    PhysicsServer();
    ~PhysicsServer();

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;
    void Reset();

    void AddRigidbody(RigidbodyComponent*);
    void RemoveRigidbody(RigidbodyComponent*);

    ColliderID CreateCollider(const Shape& shape);
    void DestroyCollider(ColliderID col);

    bool Raycast(const glm::vec3& from, const glm::vec3& to, RaycastHit& hit);
    void SetGravity(const glm::vec3& acc);

    void DrawDebug();
    void EnableDebugUI(bool enable = true);

private:
    // Members are destroyed in reverse declaration order, so _world (which
    // references the four subsystems and the debug drawer) is declared last
    // and dies first — same order the old manual destructor enforced by hand.
    std::unique_ptr<btCollisionConfiguration> _config;
    std::unique_ptr<btCollisionDispatcher> _dispatcher;
    std::unique_ptr<btBroadphaseInterface> _broadphase;
    std::unique_ptr<btConstraintSolver> _solver;
    std::unique_ptr<PhysicsDebugDrawer> _debugDrawer;
    std::unique_ptr<btDiscreteDynamicsWorld> _world;
    std::unique_ptr<BulletTaskScheduler> _taskScheduler;
    std::unordered_map<ColliderID, std::unique_ptr<btCollisionShape>> _colliders;
    std::vector<RigidbodyComponent*> _impostors;
    float _timeAccum = 0.0f;

    bool _debugUIEnabled = false;
    ColliderID _nextColliderID = 0;
};