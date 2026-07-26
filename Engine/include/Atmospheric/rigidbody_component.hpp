#pragma once
#include "bullet_collision.hpp"
#include "bullet_dynamics.hpp"
#include "bullet_linear_math.hpp"
#include "component.hpp"
#include "globals.hpp"
#include <memory>

struct RigidbodyProps {
    float mass = 1.0f;
    float friction = 1.0f;
    float restitution = 0.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    glm::vec3 linearFactor = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec3 angularFactor = glm::vec3(1.0f, 1.0f, 1.0f);
    btCollisionShape* shape = nullptr;
    bool useGravity = true;
    bool isKinematic = false;
    // Centre of mass in the object's local frame. Bullet has no separate
    // notion of one: a body's local origin IS its centre of mass, and the
    // inertia tensor is taken about it. Colliders whose mass is not centred on
    // the object pivot (a voxel volume's local origin is the grid's bottom
    // centre) must build their shape about the real centroid and report it
    // here; the motion state then carries the offset so the transform read
    // back is still the object's own.
    glm::vec3 centerOfMass = glm::vec3(0.0f);
};

class GameObject;
class Physics3DSubsystem;

class RigidbodyComponent : public Component {
public:
    RigidbodyComponent(
        GameObject* gameObject,
        btCollisionShape* shape,
        float mass = 0.0f,
        glm::vec3 linearFactor = glm::vec3(1.0f),
        glm::vec3 angularFactor = glm::vec3(1.0f)
    );
    RigidbodyComponent(GameObject* gameObject, const RigidbodyProps& props);
    ~RigidbodyComponent();

    std::string GetName() const override;

    void OnAttach() override;
    void OnDetach() override;
    void DrawImGui() override;

    float GetMass() const;
    void SetMass(float mass);

    glm::mat4 GetWorldTransform();
    void SetWorldTransform(const glm::vec3& position, const glm::vec3& rotation);

    void WakeUp();
    void Sleep();

    // Continuous collision detection. A body moving further than
    // motionThreshold in one step is swept as a sphere of sweptSphereRadius
    // instead of tested only at its end pose, which is what stops fast or
    // heavy bodies from passing through thin static geometry (a triangle mesh
    // is a surface, so once something is through it there is nothing left to
    // push it back). Pass motionThreshold = 0 to disable.
    void SetContinuousCollision(float motionThreshold, float sweptSphereRadius);

    // Route this body's contacts through the global contact-added callback.
    // Triangle-mesh colliders need it so internal-edge contact normals can be
    // snapped back to the face normal; nothing else should turn it on.
    void SetCustomMaterialCallback(bool enabled);

    void AddForce(const glm::vec3& force);
    void AddForceAtPosition(const glm::vec3& force, const glm::vec3& position);
    void AddImpulse(const glm::vec3& impulse);
    void AddImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position);
    void AddTorque(const glm::vec3& torque);
    void AddTorqueImpulse(const glm::vec3& torque);

    // This will override the dynamics world gravity
    void SetGravity(const glm::vec3& acc);

    glm::vec3 GetLinearFactor();
    void SetLinearFactor(const glm::vec3& fac);
    glm::vec3 GetAngularFactor();
    void SetAngularFactor(const glm::vec3& fac);

    glm::vec3 GetLinearVelocity();
    void SetLinearVelocity(const glm::vec3& vel);
    glm::vec3 GetAngularVelocity();
    void SetAngularVelocity(const glm::vec3& vel);

    bool IsKinematic() const;

private:
    // The component owns its Bullet objects; btRigidBody does not delete its
    // motion state, so we hold that separately. OnDetach removes the body
    // from the world before either is destroyed.
    std::unique_ptr<btDefaultMotionState> _motionState;
    std::unique_ptr<btRigidBody> _rigidbody;
    friend class Physics3DSubsystem;
};