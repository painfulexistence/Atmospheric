#pragma once
#include "globals.hpp"
#include "imgui.h"

class Application;

class Server {
public:
    Server() = default;
    virtual ~Server() = default;

    // Polymorphic base: copying through a base reference would slice derived
    // server state, so copying is disabled (Core Guidelines C.67).
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    virtual void Init(Application* app);
    virtual void Process(float dt);
    virtual void DrawImGui(float dt);

protected:
    Application* _app = nullptr;
    bool _initialized = false;
};