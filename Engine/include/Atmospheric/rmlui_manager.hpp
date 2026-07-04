#pragma once
#include <RmlUi/Core.h>
#include <memory>
#include <string>

class RmlUiRenderer;
class RmlUiSystem;

// Owned by Application (see Application's service members); Get() is a
// non-owning locator into that instance. Declared after _window in
// Application so ~RmlUiManager (which calls Shutdown) runs while the GL
// context and the Renderer are still alive.
class RmlUiManager {
public:
    static RmlUiManager* Get();

    RmlUiManager();
    ~RmlUiManager();
    RmlUiManager(const RmlUiManager&) = delete;
    RmlUiManager& operator=(const RmlUiManager&) = delete;

    // Initialize RmlUi with window dimensions
    bool Initialize(int width, int height, class Renderer* renderer);

    // Shutdown RmlUi
    void Shutdown();

    // Check if RmlUi is initialized
    bool IsInitialized() const {
        return _initialized;
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
        return _context;
    }

    // Input handling - to be called from the input subsystem
    void ProcessKeyDown(Rml::Input::KeyIdentifier key, int key_modifier);
    void ProcessKeyUp(Rml::Input::KeyIdentifier key, int key_modifier);
    void ProcessTextInput(Rml::Character character);
    // Returns true when the cursor is NOT over any interactive element
    // (matching Rml::Context::ProcessMouseMove), so callers can gate
    // world-space input on the UI.
    bool ProcessMouseMove(int x, int y, int key_modifier);
    void ProcessMouseButtonDown(int button_index, int key_modifier);
    void ProcessMouseButtonUp(int button_index, int key_modifier);
    void ProcessMouseWheel(float wheel_delta, int key_modifier);

private:
    static RmlUiManager* s_instance;

    std::unique_ptr<RmlUiRenderer> _renderer;
    std::unique_ptr<RmlUiSystem> _system;
    Rml::Context* _context = nullptr;

    int _width = 0;
    int _height = 0;
    bool _initialized = false;
};
