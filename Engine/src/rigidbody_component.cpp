#include "rigidbody_component.hpp"
#include "application.hpp"
#include "bullet_linear_math.hpp"
#include "game_object.hpp"

static glm::mat4 convertToGLMatrix(const btTransform& trans) {
    btScalar mat[16] = { 0.0f };
    trans.getOpenGLMatrix(mat);

    return glm::mat4(
      mat[0],
      mat[1],
      mat[2],
      mat[3],
      mat[4],
      mat[5],
      mat[6],
      mat[7],
      mat[8],
      mat[9],
      mat[10],
      mat[11],
      mat[12],
      mat[13],
      mat[14],
      mat[15]
    );
}

RigidbodyComponent::RigidbodyComponent(
  GameObject* gameObject, btCollisionShape* shape, float mass, glm::vec3 linearFactor, glm::vec3 angularFactor
) {
    glm::vec3 position = gameObject->GetPosition();
    glm::vec3 rotation = gameObject->GetRotation();

    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(position.x, position.y, position.z));
    t.setRotation(btQuaternion(rotation.x, rotation.y, rotation.z, 1.0f));

    auto motionState = new btDefaultMotionState(t);
    _rigidbody = new btRigidBody(btScalar(mass), motionState, shape, btVector3(1, 1, 1));
    _rigidbody->setLinearFactor(btVector3(linearFactor.x, linearFactor.y, linearFactor.z));
    _rigidbody->setAngularFactor(btVector3(angularFactor.x, angularFactor.y, angularFactor.z));
    _rigidbody->setFriction(2.0f);
    _rigidbody->setRestitution(0.0f);
    _rigidbody->setDamping(0.0f, 0.0f);
    // _rigidbody->setCollisionFlags(_rigidbody->getCollisionFlags() |
    //     btCollisionObject::CF_NO_CONTACT_RESPONSE);
    _rigidbody->setUserPointer(gameObject);
};

RigidbodyComponent::RigidbodyComponent(GameObject* gameObject, const RigidbodyProps& props) {
    glm::vec3 position = gameObject->GetPosition();
    glm::vec3 rotation = gameObject->GetRotation();

    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(position.x, position.y, position.z));
    t.setRotation(btQuaternion(rotation.x, rotation.y, rotation.z, 1.0f));

    auto motionState = new btDefaultMotionState(t);
    _rigidbody = new btRigidBody(btScalar(props.mass), motionState, props.shape, btVector3(1, 1, 1));
    _rigidbody->setLinearFactor(btVector3(props.linearFactor.x, props.linearFactor.y, props.linearFactor.z));
    _rigidbody->setAngularFactor(btVector3(props.angularFactor.x, props.angularFactor.y, props.angularFactor.z));
    if (!props.useGravity) {
        _rigidbody->setMassProps(props.mass, btVector3(0, 0, 0));
        _rigidbody->setGravity(btVector3(0, 0, 0));
        _rigidbody->setFlags(_rigidbody->getFlags() | BT_DISABLE_WORLD_GRAVITY);
    }
    _rigidbody->setFriction(props.friction);
    _rigidbody->setRestitution(props.restitution);
    _rigidbody->setDamping(props.linearDamping, props.angularDamping);
    if (props.isKinematic) {
        _rigidbody->setCollisionFlags(_rigidbody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        _rigidbody->setActivationState(DISABLE_DEACTIVATION);
    }
    // _rigidbody->setCollisionFlags(_rigidbody->getCollisionFlags() |
    //     btCollisionObject::CF_NO_CONTACT_RESPONSE);
    _rigidbody->setUserPointer(gameObject);
}

RigidbodyComponent::~RigidbodyComponent(){
    // TODO: Check if bullet objects are destoryed by the destructor in btDynamicsWorld class
    // delete this->_rigidbody;
};

std::string RigidbodyComponent::GetName() const {
    return std::string("Physics");
}

void RigidbodyComponent::OnAttach() {
    if (gameObject->GetApp()->GetPhysicsServer()) {
        gameObject->GetApp()->GetPhysicsServer()->AddRigidbody(this);
    }
}

void RigidbodyComponent::OnDetach() {
    if (gameObject->GetApp()->GetPhysicsServer()) {
        gameObject->GetApp()->GetPhysicsServer()->RemoveRigidbody(this);
    }
}

float RigidbodyComponent::GetMass() const {
    return _rigidbody->getMass();
}

void RigidbodyComponent::SetMass(float mass) {
    _rigidbody->setMassProps(mass, btVector3(0, 0, 0));
}

glm::mat4 RigidbodyComponent::GetWorldTransform() {
    btTransform t;
    _rigidbody->getMotionState()->getWorldTransform(t);
    return convertToGLMatrix(t);
};

void RigidbodyComponent::SetWorldTransform(const glm::vec3& position, const glm::vec3& rotation) {
    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(position.x, position.y, position.z));
    t.setRotation(btQuaternion(rotation.x, rotation.y, rotation.z, 1.0f));
    _rigidbody->setWorldTransform(t);
    _rigidbody->getMotionState()->setWorldTransform(t);
}

void RigidbodyComponent::WakeUp() {
    _rigidbody->activate();
}

void RigidbodyComponent::Sleep() {
    _rigidbody->setActivationState(0);
    _rigidbody->setLinearVelocity(btVector3(0, 0, 0));
    _rigidbody->setAngularVelocity(btVector3(0, 0, 0));
}

void RigidbodyComponent::AddForce(const glm::vec3& force) {
    _rigidbody->applyCentralForce(btVector3(force.x, force.y, force.z));
}

void RigidbodyComponent::AddForceAtPosition(const glm::vec3& force, const glm::vec3& position) {
    _rigidbody->applyForce(btVector3(force.x, force.y, force.z), btVector3(position.x, position.y, position.z));
}

void RigidbodyComponent::AddImpulse(const glm::vec3& impulse) {
    _rigidbody->applyImpulse(btVector3(impulse.x, impulse.y, impulse.z), btVector3(0, 0, 0));
}

void RigidbodyComponent::AddImpulseAtPosition(const glm::vec3& impulse, const glm::vec3& position) {
    _rigidbody->applyImpulse(btVector3(impulse.x, impulse.y, impulse.z), btVector3(position.x, position.y, position.z));
}

void RigidbodyComponent::AddTorque(const glm::vec3& torque) {
    _rigidbody->applyTorque(btVector3(torque.x, torque.y, torque.z));
}

void RigidbodyComponent::AddTorqueImpulse(const glm::vec3& torque) {
    _rigidbody->applyTorqueImpulse(btVector3(torque.x, torque.y, torque.z));
}

// This will override the dynamics world gravity
void RigidbodyComponent::SetGravity(const glm::vec3& acc) {
    _rigidbody->setGravity(btVector3(acc.x, acc.y, acc.z));
}

glm::vec3 RigidbodyComponent::GetLinearFactor() {
    btVector3 fac = _rigidbody->getLinearFactor();
    return glm::vec3(fac.x(), fac.y(), fac.z());
}

void RigidbodyComponent::SetLinearFactor(const glm::vec3& fac) {
    _rigidbody->setLinearFactor(btVector3(fac.x, fac.y, fac.z));
}

glm::vec3 RigidbodyComponent::GetLinearVelocity() {
    btVector3 vel = _rigidbody->getLinearVelocity();
    return glm::vec3(vel.x(), vel.y(), vel.z());
}

void RigidbodyComponent::SetLinearVelocity(const glm::vec3& vel) {
    _rigidbody->activate();
    _rigidbody->setLinearVelocity(btVector3(vel.x, vel.y, vel.z));
}

glm::vec3 RigidbodyComponent::GetAngularFactor() {
    btVector3 fac = _rigidbody->getAngularFactor();
    return glm::vec3(fac.x(), fac.y(), fac.z());
}

void RigidbodyComponent::SetAngularFactor(const glm::vec3& fac) {
    _rigidbody->setAngularFactor(btVector3(fac.x, fac.y, fac.z));
}

glm::vec3 RigidbodyComponent::GetAngularVelocity() {
    btVector3 vel = _rigidbody->getAngularVelocity();
    return glm::vec3(vel.x(), vel.y(), vel.z());
}

void RigidbodyComponent::SetAngularVelocity(const glm::vec3& vel) {
    _rigidbody->activate();
    _rigidbody->setAngularVelocity(btVector3(vel.x, vel.y, vel.z));
}

bool RigidbodyComponent::IsKinematic() const {
    return _rigidbody->isKinematicObject();
}