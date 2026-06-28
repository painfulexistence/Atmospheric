#pragma once
#include "layer.hpp"
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
#include <glad/glad.h>
#endif
#include <unordered_map>
#include <vector>

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
    void DrawEntityTree(GameObject* entity, const std::unordered_map<GameObject*, std::vector<GameObject*>>& childrenMap);
    void DrawEntityInspector(GameObject* entity);
    void DrawEngineView();
    void ToggleRecording();
};
