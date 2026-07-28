#include "rigidbody_component.hpp"
#include <glm/gtc/quaternion.hpp>
#include "application.hpp"
#include "bullet_linear_math.hpp"
#include "game_object.hpp"
#include "imgui.h"

static glm::mat4 ConvertToGlMatrix(const btTransform& trans) {
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

// Inertia tensor for a dynamic body, from the shape's own geometry. This used
// to be hardcoded to btVector3(1, 1, 1), which is not a scale-free default but
// a literal 1 kg*m^2 about every axis. A 1.6 m voxel crate (589 kg) wants
// ~267 kg*m^2 and a boulder of the same size ~333, so bodies came out two to
// three hundred times too easy to spin: the faintest glancing contact span
// them up, and they could never settle. They tumbled and jittered indefinitely
// instead of coming to rest. Static bodies (mass 0) keep a zero tensor, which
// is what Bullet expects.
static btVector3 ComputeLocalInertia(btCollisionShape* shape, float mass) {
    btVector3 inertia(0.0f, 0.0f, 0.0f);
    if (shape != nullptr && mass > 0.0f) {
        shape->calculateLocalInertia(static_cast<btScalar>(mass), inertia);
    }
    return inertia;
}

RigidbodyComponent::RigidbodyComponent(
    GameObject* gameObject, btCollisionShape* shape, float mass, glm::vec3 linearFactor, glm::vec3 angularFactor
) {
    glm::vec3 position = gameObject->GetPosition();
    glm::vec3 rotation = gameObject->GetRotation();

    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(position.x, position.y, position.z));
    // Euler radians -> quaternion with the SAME conversion TransformComponent
    // uses to build its matrix (glm::quat(vec3)), so the body starts exactly
    // where the object is drawn. The old btQuaternion(x, y, z, 1) fed euler
    // angles in as raw quaternion components — identity only at zero rotation.
    const glm::quat q(rotation);
    t.setRotation(btQuaternion(q.x, q.y, q.z, q.w));

    _motionState = std::make_unique<btDefaultMotionState>(t);
    _rigidbody = std::make_unique<btRigidBody>(
        static_cast<btScalar>(mass), _motionState.get(), shape, ComputeLocalInertia(shape, mass)
    );
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
    // Euler radians -> quaternion with the SAME conversion TransformComponent
    // uses to build its matrix (glm::quat(vec3)), so the body starts exactly
    // where the object is drawn. The old btQuaternion(x, y, z, 1) fed euler
    // angles in as raw quaternion components — identity only at zero rotation.
    const glm::quat q(rotation);
    t.setRotation(btQuaternion(q.x, q.y, q.z, q.w));

    // A body's local origin is its centre of mass as far as Bullet is
    // concerned. When the collider says its mass sits elsewhere, the shape has
    // been built about that centroid, so the motion state has to translate
    // between the two frames — otherwise the body would render offset by the
    // centroid. btDefaultMotionState stores the object ("graphics") transform
    // and hands Bullet graphics * offset^-1, so the offset is the negated
    // centre of mass. Identity when the collider does not set one.
    btTransform comOffset;
    comOffset.setIdentity();
    comOffset.setOrigin(btVector3(-props.centerOfMass.x, -props.centerOfMass.y, -props.centerOfMass.z));

    _motionState = std::make_unique<btDefaultMotionState>(t, comOffset);
    _rigidbody = std::make_unique<btRigidBody>(
        static_cast<btScalar>(props.mass), _motionState.get(), props.shape, ComputeLocalInertia(props.shape, props.mass)
    );
    _rigidbody->setLinearFactor(btVector3(props.linearFactor.x, props.linearFactor.y, props.linearFactor.z));
    _rigidbody->setAngularFactor(btVector3(props.angularFactor.x, props.angularFactor.y, props.angularFactor.z));
    if (!props.useGravity) {
        // Zero inertia locks rotation outright. That is the long-standing
        // behaviour of the no-gravity path — bodies that opt out of gravity
        // are floating props, not tumbling ones — so it stays, and it is why
        // this deliberately overrides the tensor computed above.
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

RigidbodyComponent::~RigidbodyComponent() = default;

std::string RigidbodyComponent::GetName() const {
    return std::string("Physics");
}

void RigidbodyComponent::DrawImGui() {
    glm::vec3 vel = GetLinearVelocity();
    ImGui::Text("Velocity: %.3f, %.3f, %.3f", vel.x, vel.y, vel.z);
}

void RigidbodyComponent::OnAttach() {
    if (Physics3DSubsystem::Get()) {
        Physics3DSubsystem::Get()->AddRigidbody(this);
    }
}

void RigidbodyComponent::OnDetach() {
    if (Physics3DSubsystem::Get()) {
        Physics3DSubsystem::Get()->RemoveRigidbody(this);
    }
}

float RigidbodyComponent::GetMass() const {
    return _rigidbody->getMass();
}

void RigidbodyComponent::SetMass(float mass) {
    // Rescale the inertia tensor with the mass instead of zeroing it, which
    // would silently lock the body's rotation (zero inertia reads as infinite
    // resistance to Bullet). setMassProps does not do this for us — it takes
    // whatever tensor it is handed.
    _rigidbody->setMassProps(mass, ComputeLocalInertia(_rigidbody->getCollisionShape(), mass));
}

glm::mat4 RigidbodyComponent::GetWorldTransform() {
    // The object's transform, not the centre of mass's — those differ by
    // RigidbodyProps::centerOfMass, and it is the object's that the renderer
    // needs. btMotionState::getWorldTransform() would hand back the centre of
    // mass one (that is its contract with Bullet), so read the stored graphics
    // transform instead. Identical to the old code whenever the offset is
    // identity, which is every collider that does not set a centre of mass.
    return ConvertToGlMatrix(_motionState->m_graphicsWorldTrans);
};

void RigidbodyComponent::SetWorldTransform(const glm::vec3& position, const glm::vec3& rotation) {
    btTransform t;
    t.setIdentity();
    t.setOrigin(btVector3(position.x, position.y, position.z));
    // Euler radians -> quaternion with the SAME conversion TransformComponent
    // uses to build its matrix (glm::quat(vec3)), so the body starts exactly
    // where the object is drawn. The old btQuaternion(x, y, z, 1) fed euler
    // angles in as raw quaternion components — identity only at zero rotation.
    const glm::quat q(rotation);
    t.setRotation(btQuaternion(q.x, q.y, q.z, q.w));
    // t is where the object goes; Bullet is positioned by its centre of mass.
    // setCenterOfMassTransform (rather than a bare setWorldTransform) also
    // refreshes the interpolation transform and the world inertia tensor, so a
    // teleported body does not render a frame of stale motion.
    const btTransform com = t * _motionState->m_centerOfMassOffset.inverse();
    _rigidbody->setCenterOfMassTransform(com);
    _motionState->setWorldTransform(com);
}

void RigidbodyComponent::SwapShape(btCollisionShape* shape, float mass, const glm::vec3& centerOfMass) {
    if (shape == nullptr) return;

    // Where the object is drawn does not change; where its centre of mass sits
    // inside it may. Capture the graphics transform before touching the offset,
    // then rebuild the body's pose from it and the NEW offset.
    const btTransform graphics = _motionState->m_graphicsWorldTrans;
    btTransform comOffset;
    comOffset.setIdentity();
    comOffset.setOrigin(btVector3(-centerOfMass.x, -centerOfMass.y, -centerOfMass.z));
    _motionState->m_centerOfMassOffset = comOffset;

    _rigidbody->setCollisionShape(shape);
    // A carved prop is lighter and differently balanced, so both the mass and
    // the tensor are re-derived. setCenterOfMassTransform then refreshes the
    // world-space inertia tensor from them.
    _rigidbody->setMassProps(static_cast<btScalar>(mass), ComputeLocalInertia(shape, mass));
    _rigidbody->setCenterOfMassTransform(graphics * comOffset.inverse());

    // The broadphase still holds an AABB measured from the old shape.
    if (Physics3DSubsystem::Get()) {
        Physics3DSubsystem::Get()->RefreshAabb(this);
    }
    // A sleeping body would keep its stale contacts and never notice the swap.
    _rigidbody->activate();
}

void RigidbodyComponent::SetContinuousCollision(float motionThreshold, float sweptSphereRadius) {
    _rigidbody->setCcdMotionThreshold(motionThreshold);
    _rigidbody->setCcdSweptSphereRadius(sweptSphereRadius);
}

void RigidbodyComponent::SetCustomMaterialCallback(bool enabled) {
    int flags = _rigidbody->getCollisionFlags();
    if (enabled) {
        flags |= btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK;
    } else {
        flags &= ~btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK;
    }
    _rigidbody->setCollisionFlags(flags);
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