#pragma once
#include "Atmospheric.hpp"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Motion components
//
// Small, self-contained behaviours that drive a GameObject's transform over
// time. They are attached with GameObject::AddComponent<T>(...) and ticked
// automatically by the engine (GameLayer -> GameObject::Tick -> OnTick).
//
// These intentionally depend only on the public GameObject / Component API so
// that they work for any example without engine modifications.
// ─────────────────────────────────────────────────────────────────────────────

namespace axex {

// Spins the owning object around its local axes at a constant angular velocity.
// `anglesPerSecond` is expressed in radians/second per axis.
class RotatorComponent : public Component {
public:
    RotatorComponent(GameObject* go, glm::vec3 anglesPerSecond) : _angularVelocity(anglesPerSecond) {
    }

    std::string GetName() const override {
        return "RotatorComponent";
    }

    void OnTick(float dt) override {
        gameObject->SetRotation(gameObject->GetRotation() + _angularVelocity * dt);
    }

    void SetAngularVelocity(glm::vec3 anglesPerSecond) {
        _angularVelocity = anglesPerSecond;
    }

private:
    glm::vec3 _angularVelocity;
};

// Adds a sinusoidal offset to the object's position around the point it was at
// when the component was attached. Use `phase = pi/2` to get cosine motion.
class OscillatorComponent : public Component {
public:
    OscillatorComponent(GameObject* go, glm::vec3 axis, float amplitude, float frequency, float phase = 0.0f)
        : _axis(axis), _amplitude(amplitude), _frequency(frequency), _phase(phase) {
    }

    std::string GetName() const override {
        return "OscillatorComponent";
    }

    void OnAttach() override {
        _base = gameObject->GetPosition();
    }

    void OnTick(float dt) override {
        _t += dt;
        float offset = std::sin(_t * _frequency + _phase) * _amplitude;
        gameObject->SetPosition(_base + _axis * offset);
    }

    // Re-baseline the oscillation centre (e.g. after manually moving the object).
    void SetBase(glm::vec3 base) {
        _base = base;
    }

private:
    glm::vec3 _axis;
    float _amplitude;
    float _frequency;
    float _phase;
    glm::vec3 _base = glm::vec3(0.0f);
    float _t = 0.0f;
};

// Moves the object at a constant velocity (units/second).
class LinearMoverComponent : public Component {
public:
    LinearMoverComponent(GameObject* go, glm::vec3 velocity) : _velocity(velocity) {
    }

    std::string GetName() const override {
        return "LinearMoverComponent";
    }

    void OnTick(float dt) override {
        gameObject->SetPosition(gameObject->GetPosition() + _velocity * dt);
    }

    void SetVelocity(glm::vec3 velocity) {
        _velocity = velocity;
    }
    glm::vec3 GetVelocity() const {
        return _velocity;
    }

private:
    glm::vec3 _velocity;
};

// Deactivates the owning object after `lifetime` seconds. Handy for transient
// effects, projectiles, pickups, etc.
class LifetimeComponent : public Component {
public:
    LifetimeComponent(GameObject* go, float lifetime) : _lifetime(lifetime) {
    }

    std::string GetName() const override {
        return "LifetimeComponent";
    }

    void OnTick(float dt) override {
        _elapsed += dt;
        if (_elapsed >= _lifetime) {
            gameObject->SetActive(false);
        }
    }

    bool IsExpired() const {
        return _elapsed >= _lifetime;
    }

private:
    float _lifetime;
    float _elapsed = 0.0f;
};

}// namespace axex
