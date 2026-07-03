#pragma once
#include "globals.hpp"
#include "subsystem.hpp"

struct Transform;

struct Geometry;

struct Impostor;

struct ScriptSubsystem;

class EnttRegistry;

class ECSSubsystem : public Subsystem
{
public:
    ECSSubsystem();

    ~ECSSubsystem();

    void Process(float dt) override;

    uint64_t Create();

    void Destroy(uint64_t eid);

    void SyncTransformWithPhysics();

    template<class Component, class ComponentProps> void AddComponent(uint64_t eid, const ComponentProps& props);

    template<class Component> auto GetComponent(uint64_t eid);

private:
    EnttRegistry* _registry;
};