#pragma once
#include "bullet_collision.hpp"
#include "bullet_dynamics.hpp"
#include "bullet_linear_math.hpp"
#include "component.hpp"
#include "globals.hpp"

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
};

class GameObject;
class PhysicsServer;

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

    float GetMass() const;
    void SetMass(float mass);

    glm::mat4 GetWorldTransform();
    void SetWorldTransform(const glm::vec3& position, const glm::vec3& rotation);

    void WakeUp();
    void Sleep();

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
    btRigidBody* _rigidbody;
    friend class PhysicsServer;
};