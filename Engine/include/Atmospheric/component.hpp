#pragma once
#include "globals.hpp"

class GameObject;

class Component {
public:
    virtual ~Component(){};

    virtual std::string GetName() const = 0;

    virtual void OnAttach(){};
    virtual void OnDetach(){};
    virtual void OnTick(float dt){};
    virtual void OnPhysicsTick(float dt){};
    virtual bool CanTick() const {
        return enabled;
    }
    virtual bool CanPhysicsTick() const {
        return enabled;
    }

    GameObject* gameObject = nullptr;
    bool enabled = true;
};

struct Transform;

struct RigidbodyComponent;

struct Geometry;

struct Script;

// Note that the pointers are widely used here, because the byte size of the reference type is not fixed and can change
// by implementations
// TODO: For now, the enabled and transform both represents a component's global state. But these should be rewritten
// with the concept of scene hierarchy in the future
struct TransformData {
    const Transform* current;
};

struct RigidbodyComponentData {
    bool isActive;
    const RigidbodyComponent* current;
};

struct GeometryData {
    bool isActive;
    const Geometry* current;
};

struct ScriptData {
    const std::string& script;
};