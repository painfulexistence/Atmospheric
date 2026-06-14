#pragma once
#include "layer.hpp"
#ifndef __EMSCRIPTEN__
#include <glad/glad.h>
#endif

// Forward declarations
class Application;
class GameObject;
class RigidbodyComponent;
class LightComponent;
class CameraComponent;
class MeshComponent;
class SpriteComponent;

class EditorLayer : public Layer {
public:
    EditorLayer(Application* app, bool showImGui = true);
    ~EditorLayer() = default;

    void OnAttach() override {}
    void OnDetach() override {}
    void OnUpdate(float dt) override;
    void OnRender(float dt) override;

    bool IsVisible() const { return _showImGui; }
    void SetVisible(bool show) { _showImGui = show; }

private:
    Application* _app;
    bool _showImGui;
    bool _showSystemInfo = false;
    bool _showAppView = true;
    bool _showEngineView = true;
    GameObject* _selectedEntity = nullptr;

    void DrawSystemInfo();
    void DrawAppView();
    void DrawEntityInspector(GameObject* entity);
    void DrawEngineView();
};
