#pragma once
#include "globals.hpp"
#include "imgui.h"

class Application;

class Subsystem {
public:
    Subsystem() = default;
    virtual ~Subsystem() = default;

    // Polymorphic base: copying through a base reference would slice derived
    // server state, so copying is disabled (Core Guidelines C.67).
    Subsystem(const Subsystem&) = delete;
    Subsystem& operator=(const Subsystem&) = delete;

    virtual void Init(Application* app);
    virtual void Process(float dt);
    virtual void DrawImGui(float dt);

protected:
    Application* _app = nullptr;
    bool _initialized = false;
};