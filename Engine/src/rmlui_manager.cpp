#include "rmlui_manager.hpp"
#include "rmlui_renderer.hpp"
#include "rmlui_system.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <spdlog/spdlog.h>
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

RmlUiManager* RmlUiManager::s_instance = nullptr;

RmlUiManager* RmlUiManager::Get() {
    if (!s_instance) {
        s_instance = new RmlUiManager();
    }
    return s_instance;
}

RmlUiManager::RmlUiManager() {
    s_instance = this;
}

RmlUiManager::~RmlUiManager() {
    Shutdown();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool RmlUiManager::Initialize(int width, int height, Renderer* renderer) {
    if (m_initialized) {
        spdlog::warn("RmlUiManager already initialized");
        return true;
    }

    m_width = width;
    m_height = height;

    // Create renderer and system interfaces
    m_renderer = std::make_unique<RmlUiRenderer>(renderer);
    m_system = std::make_unique<RmlUiSystem>();

    // Initialize renderer
    m_renderer->Initialize();

    // Set interfaces
    Rml::SetRenderInterface(m_renderer.get());
    Rml::SetSystemInterface(m_system.get());

    // Initialize RmlUi
    if (!Rml::Initialise()) {
        spdlog::error("Failed to initialize RmlUi");
        return false;
    }

    // Load fonts
    if (!Rml::LoadFontFace("assets/fonts/NotoSans-SemiBold.ttf")) {
        spdlog::warn(
          "Failed to load default font, UI text may not render correctly. Consider using 'rmlui-debugger-font' in your "
          "RCSS."
        );
    }

    // Create the main UI context
    m_context = Rml::CreateContext("main", Rml::Vector2i(width, height));
    if (!m_context) {
        spdlog::error("Failed to create RmlUi context");
        Rml::Shutdown();
        return false;
    }

    // Initialize debugger (useful for development)
    Rml::Debugger::Initialise(m_context);

    m_initialized = true;
    spdlog::info("RmlUi initialized successfully ({}x{})", width, height);

    return true;
}

void RmlUiManager::Shutdown() {
    if (!m_initialized) return;

    if (m_context) {
        Rml::Debugger::Shutdown();
        Rml::RemoveContext(m_context->GetName());
        m_context = nullptr;
    }

    Rml::Shutdown();

    if (m_renderer) {
        m_renderer->Shutdown();
        m_renderer.reset();
    }

    m_system.reset();

    m_initialized = false;
    spdlog::info("RmlUi shutdown complete");
}

void RmlUiManager::Update(float deltaTime) {
    if (!m_initialized || !m_context) return;

    // Update the context
    m_context->Update();
}

void RmlUiManager::Render() {
    ZoneScopedN("RmlUiManager::Render");
    if (!m_initialized || !m_context) return;

    // Render the context (generates commands to Renderer)
    m_context->Render();
}

void RmlUiManager::OnResize(int width, int height) {
    m_width = width;
    m_height = height;

    if (m_context) {
        // TODO: width/height must be logical pixels (GetSize()), not framebuffer pixels (GetFramebufferSize()),
        // to match RmlUi's CSS px unit expectations. Wire this to ViewportResizeCallback, not FramebufferResizeCallback.
        m_context->SetDimensions(Rml::Vector2i(width, height));
    }
}

Rml::ElementDocument* RmlUiManager::LoadDocument(const std::string& path) {
    if (!m_context) {
        spdlog::error("Cannot load document: RmlUi not initialized");
        return nullptr;
    }

    Rml::ElementDocument* document = m_context->LoadDocument(path);
    if (!document) {
        spdlog::error("Failed to load RmlUi document: {}", path);
        return nullptr;
    }

    spdlog::info("Loaded RmlUi document: {}", path);
    return document;
}

void RmlUiManager::UnloadDocument(Rml::ElementDocument* document) {
    if (document) {
        document->Close();
    }
}

void RmlUiManager::ShowDocument(const std::string& id) {
    if (!m_context) return;

    Rml::ElementDocument* document = m_context->GetDocument(id);
    if (document) {
        document->Show();
    } else {
        spdlog::warn("Document not found: {}", id);
    }
}

void RmlUiManager::HideDocument(const std::string& id) {
    if (!m_context) return;

    Rml::ElementDocument* document = m_context->GetDocument(id);
    if (document) {
        document->Hide();
    }
}

// Input handling methods
void RmlUiManager::ProcessKeyDown(Rml::Input::KeyIdentifier key, int key_modifier) {
    if (m_context) {
        m_context->ProcessKeyDown(key, key_modifier);
    }
}

void RmlUiManager::ProcessKeyUp(Rml::Input::KeyIdentifier key, int key_modifier) {
    if (m_context) {
        m_context->ProcessKeyUp(key, key_modifier);
    }
}

void RmlUiManager::ProcessTextInput(Rml::Character character) {
    if (m_context) {
        m_context->ProcessTextInput(character);
    }
}

void RmlUiManager::ProcessMouseMove(int x, int y, int key_modifier) {
    if (m_context) {
        m_context->ProcessMouseMove(x, y, key_modifier);
    }
}

void RmlUiManager::ProcessMouseButtonDown(int button_index, int key_modifier) {
    if (m_context) {
        m_context->ProcessMouseButtonDown(button_index, key_modifier);
    }
}

void RmlUiManager::ProcessMouseButtonUp(int button_index, int key_modifier) {
    if (m_context) {
        m_context->ProcessMouseButtonUp(button_index, key_modifier);
    }
}

void RmlUiManager::ProcessMouseWheel(float wheel_delta, int key_modifier) {
    if (m_context) {
        m_context->ProcessMouseWheel(Rml::Vector2f(0, wheel_delta), key_modifier);
    }
}
