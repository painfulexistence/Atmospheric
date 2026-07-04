#pragma once
#include "layer.hpp"
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
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
    Application* _app = nullptr;
};
