#include "physics_server_2d.hpp"
#include "batch_renderer_2d.hpp"
#include "console.hpp"
#include "game_object.hpp"
#include "renderer.hpp"
#include "rigidbody_2d_component.hpp"
#include <algorithm>
#include <imgui.h>

Physics2DServer* Physics2DServer::_instance = nullptr;

// Helper to get user data from shape ID
static Rigidbody2DComponent* GetBodyComponent(b2ShapeId shapeId) {
    b2BodyId bodyId = b2Shape_GetBody(shapeId);
    void* userData = b2Body_GetUserData(bodyId);
    return static_cast<Rigidbody2DComponent*>(userData);
}

// Raycast callback wrapper
static float RayCastCallbackWrapper(b2ShapeId shapeId, b2Vec2 point, b2Vec2 normal, float fraction, void* context) {
    auto* result = static_cast<Physics2DServer::RaycastResult*>(context);
    result->hit = true;
    result->point = Physics2DServer::MetersToPixels(glm::vec2(point.x, point.y));
    result->normal = glm::vec2(normal.x, normal.y);
    result->fraction = fraction;

    // Get user data
    result->body = GetBodyComponent(shapeId);

    return fraction;// Continue to find closest
}

// AABB Query callback wrapper
static bool OverlapCallbackWrapper(b2ShapeId shapeId, void* context) {
    auto* results = static_cast<std::vector<Rigidbody2DComponent*>*>(context);
    Rigidbody2DComponent* rb = GetBodyComponent(shapeId);
    if (rb) {
        results->push_back(rb);
    }
    return true;// Continue
}

Physics2DServer::Physics2DServer() {
    _instance = this;
    _worldId = b2_nullWorldId;
}

Physics2DServer::~Physics2DServer() {
    if (b2World_IsValid(_worldId)) {
        b2DestroyWorld(_worldId);
    }
    if (_instance == this) {
        _instance = nullptr;
    }
}

void Physics2DServer::Init(Application* app) {
    Server::Init(app);

    // Create Box2D world
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = { 0.0f, 9.8f };
    _worldId = b2CreateWorld(&worldDef);

    Console::Get()->Info("Physics2DServer initialized with Box2D v3");
}

void Physics2DServer::Process(float dt) {
    if (!b2World_IsValid(_worldId)) return;

    _accumulator += dt;
    if (_accumulator > 0.2f) _accumulator = 0.2f;// Prevent spiral of death

    while (_accumulator >= _fixedTimeStep) {
        b2World_Step(_worldId, _fixedTimeStep, _subSteps);
        _accumulator -= _fixedTimeStep;
    }

    // Process Contact Events
    b2ContactEvents contactEvents = b2World_GetContactEvents(_worldId);

    // Begin Contact
    if (_onBeginContact) {
        for (int i = 0; i < contactEvents.beginCount; ++i) {
            b2ContactBeginTouchEvent event = contactEvents.beginEvents[i];
            Rigidbody2DComponent* rbA = GetBodyComponent(event.shapeIdA);
            Rigidbody2DComponent* rbB = GetBodyComponent(event.shapeIdB);
            if (rbA && rbB) {
                _onBeginContact(rbA, rbB);
            }
        }
    }

    // End Contact
    if (_onEndContact) {
        for (int i = 0; i < contactEvents.endCount; ++i) {
            b2ContactEndTouchEvent event = contactEvents.endEvents[i];
            Rigidbody2DComponent* rbA = GetBodyComponent(event.shapeIdA);
            Rigidbody2DComponent* rbB = GetBodyComponent(event.shapeIdB);
            if (rbA && rbB) {
                _onEndContact(rbA, rbB);
            }
        }
    }

    // Sync transforms
    for (auto* rb : _rigidbodies) {
        rb->SyncToTransform(dt);
    }
}

void Physics2DServer::DrawImGui(float dt) {
    if (ImGui::CollapsingHeader("Physics 2D")) {
        ImGui::Text("Bodies: %d", (int)_rigidbodies.size());
        ImGui::Checkbox("Debug Draw", &_debugDrawEnabled);

        glm::vec2 gravity = GetGravity();
        if (ImGui::DragFloat2("Gravity", &gravity.x, 0.1f)) {
            SetGravity(gravity);
        }

        ImGui::SliderInt("Sub Steps", &_subSteps, 0, 10);
    }
}

void Physics2DServer::SetGravity(const glm::vec2& gravity) {
    if (b2World_IsValid(_worldId)) {
        b2World_SetGravity(_worldId, { gravity.x, gravity.y });
    }
}

glm::vec2 Physics2DServer::GetGravity() const {
    if (b2World_IsValid(_worldId)) {
        b2Vec2 g = b2World_GetGravity(_worldId);
        return glm::vec2(g.x, g.y);
    }
    return glm::vec2(0.0f);
}

b2BodyId Physics2DServer::CreateBody(const b2BodyDef* def) {
    if (b2World_IsValid(_worldId)) {
        return b2CreateBody(_worldId, def);
    }
    return b2_nullBodyId;
}

void Physics2DServer::DestroyBody(b2BodyId bodyId) {
    if (b2Body_IsValid(bodyId)) {
        b2DestroyBody(bodyId);
    }
}

Rigidbody2DComponent* Physics2DServer::RegisterRigidbody2D(Rigidbody2DComponent* rb) {
    if (rb) {
        _rigidbodies.push_back(rb);
    }
    return rb;
}

void Physics2DServer::UnregisterRigidbody2D(Rigidbody2DComponent* rb) {
    auto it = std::find(_rigidbodies.begin(), _rigidbodies.end(), rb);
    if (it != _rigidbodies.end()) {
        _rigidbodies.erase(it);
    }
}

void Physics2DServer::SetBeginContactCallback(CollisionCallback callback) {
    _onBeginContact = callback;
}

void Physics2DServer::SetEndContactCallback(CollisionCallback callback) {
    _onEndContact = callback;
}

Physics2DServer::RaycastResult
  Physics2DServer::Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance) {
    RaycastResult result;
    if (!b2World_IsValid(_worldId)) return result;

    glm::vec2 originM = PixelsToMeters(origin);
    glm::vec2 translationM = PixelsToMeters(direction * maxDistance);

    b2QueryFilter filter = b2DefaultQueryFilter();
    b2World_CastRay(
      _worldId, { originM.x, originM.y }, { translationM.x, translationM.y }, filter, RayCastCallbackWrapper, &result
    );

    return result;
}

std::vector<Rigidbody2DComponent*>
  Physics2DServer::QueryAABB(const glm::vec2& lowerBound, const glm::vec2& upperBound) {
    std::vector<Rigidbody2DComponent*> results;
    if (!b2World_IsValid(_worldId)) return results;

    glm::vec2 lowerM = PixelsToMeters(lowerBound);
    glm::vec2 upperM = PixelsToMeters(upperBound);

    b2AABB aabb;
    aabb.lowerBound = { lowerM.x, lowerM.y };
    aabb.upperBound = { upperM.x, upperM.y };

    b2QueryFilter filter = b2DefaultQueryFilter();
    b2World_OverlapAABB(_worldId, aabb, filter, OverlapCallbackWrapper, &results);

    return results;
}
