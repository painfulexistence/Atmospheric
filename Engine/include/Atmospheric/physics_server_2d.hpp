#pragma once

#include "server.hpp"
#include <box2d/box2d.h>
#include <functional>
#include <glm/vec2.hpp>
#include <vector>

class Rigidbody2DComponent;
class GameObject;

// Collision callback types
using CollisionCallback = std::function<void(Rigidbody2DComponent*, Rigidbody2DComponent*)>;

// 2D Physics Server - manages Box2D world and all 2D physics bodies
class Physics2DServer : public Server {
private:
    static Physics2DServer* _instance;

public:
    static Physics2DServer* Get() {
        return _instance;
    }

    Physics2DServer();
    ~Physics2DServer();

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    // World management
    void SetGravity(const glm::vec2& gravity);
    glm::vec2 GetGravity() const;

    // Body management
    b2BodyId CreateBody(const b2BodyDef* def);
    void DestroyBody(b2BodyId bodyId);

    // Registration
    Rigidbody2DComponent* RegisterRigidbody2D(Rigidbody2DComponent* rb);
    void UnregisterRigidbody2D(Rigidbody2DComponent* rb);

    // Callbacks
    void SetBeginContactCallback(CollisionCallback callback);
    void SetEndContactCallback(CollisionCallback callback);

    // Debug drawing
    void SetDebugDraw(bool enabled) {
        _debugDrawEnabled = enabled;
    }
    bool IsDebugDrawEnabled() const {
        return _debugDrawEnabled;
    }

    // Raycast
    struct RaycastResult {
        bool hit = false;
        glm::vec2 point;
        glm::vec2 normal;
        Rigidbody2DComponent* body = nullptr;
        float fraction = 1.0f;
    };
    RaycastResult Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance);

    // Query AABB
    std::vector<Rigidbody2DComponent*> QueryAABB(const glm::vec2& lowerBound, const glm::vec2& upperBound);

    // Pixel to meter conversion (Box2D uses meters)
    static constexpr float PIXELS_PER_METER = 100.0f;
    static float PixelsToMeters(float pixels) {
        return pixels / PIXELS_PER_METER;
    }
    static float MetersToPixels(float meters) {
        return meters * PIXELS_PER_METER;
    }
    static glm::vec2 PixelsToMeters(const glm::vec2& pixels) {
        return pixels / PIXELS_PER_METER;
    }
    static glm::vec2 MetersToPixels(const glm::vec2& meters) {
        return meters * PIXELS_PER_METER;
    }

    b2WorldId GetWorldId() {
        return _worldId;
    }

    // Callback storage (public so static callbacks can access if needed, or friends)
    CollisionCallback _onBeginContact;
    CollisionCallback _onEndContact;

private:
    b2WorldId _worldId;
    std::vector<Rigidbody2DComponent*> _rigidbodies;
    bool _debugDrawEnabled = false;

    // Simulation parameters
    int _subSteps = 4;
    float _accumulator = 0.0f;
    const float _fixedTimeStep = 1.0f / 60.0f;
};
