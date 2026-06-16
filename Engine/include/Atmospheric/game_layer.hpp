#pragma once
#include "layer.hpp"
#ifndef __EMSCRIPTEN__
#include <glad/glad.h>
#endif

class Application;

class GameLayer : public Layer {
public:
    GameLayer(Application* app);
    ~GameLayer() = default;

    void OnUpdate(float dt) override;
    void OnRender(float dt) override;

private:
    Application* _app;
};
