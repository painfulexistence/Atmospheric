#include "rigidbody_2d_component.hpp"
#include "game_object.hpp"
#include "transform_component.hpp"

Rigidbody2DComponent::Rigidbody2DComponent(GameObject* gameObject, const Rigidbody2DProps& props)
  : _props(props), _shapeDef(props.shape) {
    _bodyId = b2_nullBodyId;
}

Rigidbody2DComponent::~Rigidbody2DComponent() {
    DestroyBody();
}

void Rigidbody2DComponent::OnAttach() {
    CreateBody();
    Physics2DServer::Get()->RegisterRigidbody2D(this);
}

void Rigidbody2DComponent::OnDetach() {
    Physics2DServer::Get()->UnregisterRigidbody2D(this);
    DestroyBody();
}

void Rigidbody2DComponent::CreateBody() {
    auto* physics = Physics2DServer::Get();
    // Check if world is created (using v3 ID check)
    if (!physics || !b2World_IsValid(physics->GetWorldId())) return;

    // Get initial position from props or from transform
    glm::vec2 posPixels = _props.position;
    if (gameObject) {
        auto* transform = gameObject->GetComponent<TransformComponent>();
        if (transform) {
            glm::vec3 pos = transform->GetPosition();
            posPixels = glm::vec2(pos.x, pos.y);
        }
    }

    // Create body definition
    b2BodyDef bodyDef = b2DefaultBodyDef();

    switch (_props.type) {
    case BodyType2D::Static:
        bodyDef.type = b2_staticBody;
        break;
    case BodyType2D::Kinematic:
        bodyDef.type = b2_kinematicBody;
        break;
    case BodyType2D::Dynamic:
    default:
        bodyDef.type = b2_dynamicBody;
        break;
    }

    glm::vec2 posMeters = Physics2DServer::PixelsToMeters(posPixels);
    bodyDef.position = { posMeters.x, posMeters.y };
    bodyDef.rotation = b2MakeRot(_props.angle);
    bodyDef.linearDamping = _props.linearDamping;
    bodyDef.angularDamping = _props.angularDamping;
    bodyDef.gravityScale = _props.gravityScale;
    bodyDef.fixedRotation = _props.fixedRotation;
    bodyDef.enableSleep = true;
    // Bullet flag logic handled differently in v3? b2BodyDef has isBullet? No, use EnableContinuous
    // bodyDef.isBullet = _props.bullet; // Removed in v3? Checked docs: b2Body_SetBullet exists? No, moved to
    // Continuous collision. Assuming default for now or look up API. v3 enables CCD automatically? b2BodyDef has
    // enableContinuousCollision? No. Assuming safe default.

    // Store pointer to this component for collision callbacks
    bodyDef.userData = reinterpret_cast<void*>(this);

    _bodyId = physics->CreateBody(&bodyDef);

    if (b2Body_IsValid(_bodyId)) {
        CreateShape();
    }
}

void Rigidbody2DComponent::CreateShape() {
    if (!b2Body_IsValid(_bodyId)) return;

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = _shapeDef.density;
    shapeDef.material.friction = _shapeDef.friction;
    shapeDef.material.restitution = _shapeDef.restitution;
    shapeDef.isSensor = _shapeDef.isSensor;

    switch (_shapeDef.type) {
    case ShapeType2D::Box: {
        glm::vec2 halfSize = Physics2DServer::PixelsToMeters(_shapeDef.boxSize * 0.5f);
        b2Polygon box = b2MakeBox(halfSize.x, halfSize.y);
        b2CreatePolygonShape(_bodyId, &shapeDef, &box);
        break;
    }
    case ShapeType2D::Circle: {
        b2Circle circle;
        circle.center = { 0.0f, 0.0f };
        circle.radius = Physics2DServer::PixelsToMeters(_shapeDef.circleRadius);
        b2CreateCircleShape(_bodyId, &shapeDef, &circle);
        break;
    }
    case ShapeType2D::Polygon: {
        if (_shapeDef.polygonVertices.size() >= 3 && _shapeDef.polygonVertices.size() <= 8) {// v3 Hull limit usually 8
            std::vector<b2Vec2> points;
            points.reserve(_shapeDef.polygonVertices.size());
            for (const auto& v : _shapeDef.polygonVertices) {
                glm::vec2 vMeters = Physics2DServer::PixelsToMeters(v);
                points.push_back({ vMeters.x, vMeters.y });
            }

            b2Hull hull = b2ComputeHull(points.data(), (int32_t)points.size());
            b2Polygon poly = b2MakePolygon(&hull, 0.0f);
            b2CreatePolygonShape(_bodyId, &shapeDef, &poly);
        }
        break;
    }
    }
}

void Rigidbody2DComponent::DestroyBody() {
    if (b2Body_IsValid(_bodyId)) {
        auto* physics = Physics2DServer::Get();
        if (physics) {
            physics->DestroyBody(_bodyId);
        }
        _bodyId = b2_nullBodyId;
    }
}

void Rigidbody2DComponent::SyncToTransform(float dt) {
    if (!b2Body_IsValid(_bodyId) || !gameObject) return;

    // Sync Box2D body position/rotation to GameObject transform
    auto* transform = gameObject->GetComponent<TransformComponent>();
    if (transform) {
        b2Vec2 pos = b2Body_GetPosition(_bodyId);
        glm::vec2 posPixels = Physics2DServer::MetersToPixels(glm::vec2(pos.x, pos.y));

        glm::vec3 newPos(posPixels.x, posPixels.y, transform->GetPosition().z);
        transform->SetPosition(newPos);

        b2Rot rotation = b2Body_GetRotation(_bodyId);
        float angle = b2Rot_GetAngle(rotation);

        glm::vec3 rot = transform->GetRotation();
        rot.z = glm::degrees(angle);
        transform->SetRotation(rot);
    }
}

glm::vec2 Rigidbody2DComponent::GetPosition() const {
    if (b2Body_IsValid(_bodyId)) {
        b2Vec2 pos = b2Body_GetPosition(_bodyId);
        return Physics2DServer::MetersToPixels(glm::vec2(pos.x, pos.y));
    }
    return glm::vec2(0.0f);
}

void Rigidbody2DComponent::SetPosition(const glm::vec2& position) {
    if (b2Body_IsValid(_bodyId)) {
        glm::vec2 posM = Physics2DServer::PixelsToMeters(position);
        b2Rot rot = b2Body_GetRotation(_bodyId);
        b2Body_SetTransform(_bodyId, { posM.x, posM.y }, rot);
    }
}

float Rigidbody2DComponent::GetAngle() const {
    if (b2Body_IsValid(_bodyId)) {
        b2Rot rot = b2Body_GetRotation(_bodyId);
        return b2Rot_GetAngle(rot);
    }
    return 0.0f;
}

void Rigidbody2DComponent::SetAngle(float angle) {
    if (b2Body_IsValid(_bodyId)) {
        b2Vec2 pos = b2Body_GetPosition(_bodyId);
        b2Body_SetTransform(_bodyId, pos, b2MakeRot(angle));
    }
}

glm::vec2 Rigidbody2DComponent::GetLinearVelocity() const {
    if (b2Body_IsValid(_bodyId)) {
        b2Vec2 vel = b2Body_GetLinearVelocity(_bodyId);
        return Physics2DServer::MetersToPixels(glm::vec2(vel.x, vel.y));
    }
    return glm::vec2(0.0f);
}

void Rigidbody2DComponent::SetLinearVelocity(const glm::vec2& velocity) {
    if (b2Body_IsValid(_bodyId)) {
        glm::vec2 velM = Physics2DServer::PixelsToMeters(velocity);
        b2Body_SetLinearVelocity(_bodyId, { velM.x, velM.y });
    }
}

float Rigidbody2DComponent::GetAngularVelocity() const {
    if (b2Body_IsValid(_bodyId)) {
        return b2Body_GetAngularVelocity(_bodyId);
    }
    return 0.0f;
}

void Rigidbody2DComponent::SetAngularVelocity(float omega) {
    if (b2Body_IsValid(_bodyId)) {
        b2Body_SetAngularVelocity(_bodyId, omega);
    }
}

void Rigidbody2DComponent::ApplyForce(const glm::vec2& force, const glm::vec2& point) {
    if (b2Body_IsValid(_bodyId)) {
        glm::vec2 forceM = Physics2DServer::PixelsToMeters(force);
        glm::vec2 pointM = Physics2DServer::PixelsToMeters(point);
        b2Body_ApplyForce(_bodyId, { forceM.x, forceM.y }, { pointM.x, pointM.y }, true);
    }
}

void Rigidbody2DComponent::ApplyForceToCenter(const glm::vec2& force) {
    if (b2Body_IsValid(_bodyId)) {
        glm::vec2 forceM = Physics2DServer::PixelsToMeters(force);
        b2Body_ApplyForceToCenter(_bodyId, { forceM.x, forceM.y }, true);
    }
}

void Rigidbody2DComponent::ApplyTorque(float torque) {
    if (b2Body_IsValid(_bodyId)) {
        b2Body_ApplyTorque(_bodyId, torque, true);
    }
}

void Rigidbody2DComponent::ApplyLinearImpulse(const glm::vec2& impulse, const glm::vec2& point) {
    if (b2Body_IsValid(_bodyId)) {
        glm::vec2 impulseM = Physics2DServer::PixelsToMeters(impulse);
        glm::vec2 pointM = Physics2DServer::PixelsToMeters(point);
        b2Body_ApplyLinearImpulse(_bodyId, { impulseM.x, impulseM.y }, { pointM.x, pointM.y }, true);
    }
}

void Rigidbody2DComponent::ApplyLinearImpulseToCenter(const glm::vec2& impulse) {
    if (b2Body_IsValid(_bodyId)) {
        glm::vec2 impulseM = Physics2DServer::PixelsToMeters(impulse);
        b2Body_ApplyLinearImpulseToCenter(_bodyId, { impulseM.x, impulseM.y }, true);
    }
}

void Rigidbody2DComponent::ApplyAngularImpulse(float impulse) {
    if (b2Body_IsValid(_bodyId)) {
        b2Body_ApplyAngularImpulse(_bodyId, impulse, true);
    }
}

BodyType2D Rigidbody2DComponent::GetBodyType() const {
    if (b2Body_IsValid(_bodyId)) {
        b2BodyType type = b2Body_GetType(_bodyId);
        switch (type) {
        case b2_staticBody:
            return BodyType2D::Static;
        case b2_kinematicBody:
            return BodyType2D::Kinematic;
        case b2_dynamicBody:
        default:
            return BodyType2D::Dynamic;
        }
    }
    return _props.type;
}

void Rigidbody2DComponent::SetBodyType(BodyType2D type) {
    if (b2Body_IsValid(_bodyId)) {
        switch (type) {
        case BodyType2D::Static:
            b2Body_SetType(_bodyId, b2_staticBody);
            break;
        case BodyType2D::Kinematic:
            b2Body_SetType(_bodyId, b2_kinematicBody);
            break;
        case BodyType2D::Dynamic:
            b2Body_SetType(_bodyId, b2_dynamicBody);
            break;
        }
    }
}

void Rigidbody2DComponent::SetGravityScale(float scale) {
    if (b2Body_IsValid(_bodyId)) {
        b2Body_SetGravityScale(_bodyId, scale);
    }
}

float Rigidbody2DComponent::GetGravityScale() const {
    if (b2Body_IsValid(_bodyId)) {
        return b2Body_GetGravityScale(_bodyId);
    }
    return _props.gravityScale;
}

void Rigidbody2DComponent::SetFixedRotation(bool fixed) {
    if (b2Body_IsValid(_bodyId)) {
        b2Body_SetFixedRotation(_bodyId, fixed);
    }
}

bool Rigidbody2DComponent::IsFixedRotation() const {
    if (b2Body_IsValid(_bodyId)) {
        return b2Body_IsFixedRotation(_bodyId);
    }
    return _props.fixedRotation;
}

float Rigidbody2DComponent::GetMass() const {
    if (b2Body_IsValid(_bodyId)) {
        return b2Body_GetMass(_bodyId);
    }
    return 0.0f;
}
