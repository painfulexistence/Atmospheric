#pragma once
#include "component.hpp"
#include "game_object.hpp"
#include "globals.hpp"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Built-in motion utility components.
//
// Each component derives from Component and is ticked automatically by the
// engine. Attach with GameObject::AddComponent<T>(...).
// ─────────────────────────────────────────────────────────────────────────────

// Spins the owning object around its local axes at a constant angular velocity.
// `anglesPerSecond` is in radians/second per axis.
class RotatorComponent : public Component {
public:
    RotatorComponent(GameObject* go, glm::vec3 anglesPerSecond) : _angularVelocity(anglesPerSecond) {
        gameObject = go;
    }

    std::string GetName() const override { return "RotatorComponent"; }

    void OnTick(float dt) override {
        gameObject->SetRotation(gameObject->GetRotation() + _angularVelocity * dt);
    }

    void SetAngularVelocity(glm::vec3 v) { _angularVelocity = v; }
    glm::vec3 GetAngularVelocity() const { return _angularVelocity; }

private:
    glm::vec3 _angularVelocity;
};

// Adds a sinusoidal offset to the object's position around the point it was at
// when the component was attached. `phase = pi/2` gives cosine motion.
class OscillatorComponent : public Component {
public:
    OscillatorComponent(
      GameObject* go, glm::vec3 axis, float amplitude, float frequency, float phase = 0.0f
    )
        : _axis(axis), _amplitude(amplitude), _frequency(frequency), _phase(phase) {
        gameObject = go;
    }

    std::string GetName() const override { return "OscillatorComponent"; }

    void OnAttach() override { _base = gameObject->GetPosition(); }

    void OnTick(float dt) override {
        _t += dt;
        gameObject->SetPosition(_base + _axis * (std::sin(_t * _frequency + _phase) * _amplitude));
    }

    // Re-baseline the oscillation centre after manually repositioning the object.
    void SetBase(glm::vec3 base) { _base = base; }

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
        gameObject = go;
    }

    std::string GetName() const override { return "LinearMoverComponent"; }

    void OnTick(float dt) override {
        gameObject->SetPosition(gameObject->GetPosition() + _velocity * dt);
    }

    void SetVelocity(glm::vec3 v) { _velocity = v; }
    glm::vec3 GetVelocity() const { return _velocity; }

private:
    glm::vec3 _velocity;
};

// Deactivates (or destroys) the owning object after `lifetime` seconds.
// Useful for bullets, VFX, pickups, and other transient entities.
class LifetimeComponent : public Component {
public:
    LifetimeComponent(GameObject* go, float lifetime) : _lifetime(lifetime) {
        gameObject = go;
    }

    std::string GetName() const override { return "LifetimeComponent"; }

    void OnTick(float dt) override {
        _elapsed += dt;
        if (_elapsed >= _lifetime) gameObject->SetActive(false);
    }

    bool IsExpired() const { return _elapsed >= _lifetime; }
    float Elapsed() const { return _elapsed; }
    float Remaining() const { return _lifetime - _elapsed; }

private:
    float _lifetime;
    float _elapsed = 0.0f;
};
