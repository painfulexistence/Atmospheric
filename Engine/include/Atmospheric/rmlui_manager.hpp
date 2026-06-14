#pragma once
#include <RmlUi/Core.h>
#include <memory>
#include <string>

class RmlUiRenderer;
class RmlUiSystem;

class RmlUiManager {
public:
    static RmlUiManager* Get();

    RmlUiManager();
    ~RmlUiManager();

    // Initialize RmlUi with window dimensions
    bool Initialize(int width, int height, class Renderer* renderer);

    // Shutdown RmlUi
    void Shutdown();

    // Check if RmlUi is initialized
    bool IsInitialized() const {
        return m_initialized;
    }

    // Update RmlUi (call once per frame)
    void Update(float deltaTime);

    // Render all UI contexts
    void Render();

    // Handle window resize
    void OnResize(int width, int height);

    // Document management
    Rml::ElementDocument* LoadDocument(const std::string& path);
    void UnloadDocument(Rml::ElementDocument* document);
    void ShowDocument(const std::string& id);
    void HideDocument(const std::string& id);

    // Context access
    Rml::Context* GetContext() {
        return m_context;
    }

    // Input handling - to be called from the input system
    void ProcessKeyDown(Rml::Input::KeyIdentifier key, int key_modifier);
    void ProcessKeyUp(Rml::Input::KeyIdentifier key, int key_modifier);
    void ProcessTextInput(Rml::Character character);
    void ProcessMouseMove(int x, int y, int key_modifier);
    void ProcessMouseButtonDown(int button_index, int key_modifier);
    void ProcessMouseButtonUp(int button_index, int key_modifier);
    void ProcessMouseWheel(float wheel_delta, int key_modifier);

private:
    static RmlUiManager* s_instance;

    std::unique_ptr<RmlUiRenderer> m_renderer;
    std::unique_ptr<RmlUiSystem> m_system;
    Rml::Context* m_context = nullptr;

    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
};
