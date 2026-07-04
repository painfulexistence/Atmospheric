#include "rmlui_manager.hpp"
#include "file_system.hpp"
#include "rmlui_renderer.hpp"
#include "rmlui_system.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <cassert>
#include <cstring>
#include <spdlog/spdlog.h>
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

// RmlUi FileInterface backed by FileSystem so all paths go through
// NormalizePath() and pick up the SDL_GetBasePath() prefix on native builds.
class AeRmlFileInterface : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        auto bytes = std::make_unique<FileSystem::Bytes>(FileSystem::Get().ReadSync(path));
        if (bytes->empty()) return 0;
        // Encode position alongside the buffer: heap-allocate a small struct.
        auto* state = new State{ std::move(*bytes), 0 };
        return reinterpret_cast<Rml::FileHandle>(state);
    }
    void Close(Rml::FileHandle fh) override {
        delete reinterpret_cast<State*>(fh);
    }
    size_t Read(void* buffer, size_t size, Rml::FileHandle fh) override {
        auto* s = reinterpret_cast<State*>(fh);
        size_t remaining = s->data.size() - s->pos;
        size_t n = std::min(size, remaining);
        if (n) {
            std::memcpy(buffer, s->data.data() + s->pos, n);
            s->pos += n;
        }
        return n;
    }
    bool Seek(Rml::FileHandle fh, long offset, int origin) override {
        auto* s = reinterpret_cast<State*>(fh);
        long base = (origin == SEEK_SET)   ? 0
                    : (origin == SEEK_END) ? static_cast<long>(s->data.size())
                                           : static_cast<long>(s->pos);
        long newPos = base + offset;
        if (newPos < 0 || newPos > static_cast<long>(s->data.size())) return false;
        s->pos = static_cast<size_t>(newPos);
        return true;
    }
    size_t Tell(Rml::FileHandle fh) override {
        return reinterpret_cast<State*>(fh)->pos;
    }
    size_t Length(Rml::FileHandle fh) override {
        return reinterpret_cast<State*>(fh)->data.size();
    }

private:
    struct State {
        FileSystem::Bytes data;
        size_t pos;
    };
};

static AeRmlFileInterface gsRmlFileInterface;

RmlUiManager* RmlUiManager::s_instance = nullptr;

RmlUiManager* RmlUiManager::Get() {
    assert(s_instance && "RmlUiManager is owned by Application — construct the Application first");
    return s_instance;
}

RmlUiManager::RmlUiManager() {
    assert(!s_instance && "RmlUiManager is a single-instance service owned by Application");
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
    Rml::SetFileInterface(&gsRmlFileInterface);
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
            "Failed to load default font, UI text may not render correctly. Consider using 'rmlui-debugger-font' in "
            "your "
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
        // TODO: width/height must be logical pixels (GetSize()), not framebuffer pixels (GetPhysicalSize()),
        // to match RmlUi's CSS px unit expectations. Wire this to ViewportResizeCallback, not
        // FramebufferResizeCallback.
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
    // After Shutdown() the context (and every document it owned) is already
    // destroyed; closing a stale pointer then would be use-after-free.
    if (m_context && document) {
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
void RmlUiManager::ProcessKeyDown(Rml::Input::KeyIdentifier key, int keyModifier) {
    if (m_context) {
        m_context->ProcessKeyDown(key, keyModifier);
    }
}

void RmlUiManager::ProcessKeyUp(Rml::Input::KeyIdentifier key, int keyModifier) {
    if (m_context) {
        m_context->ProcessKeyUp(key, keyModifier);
    }
}

void RmlUiManager::ProcessTextInput(Rml::Character character) {
    if (m_context) {
        m_context->ProcessTextInput(character);
    }
}

bool RmlUiManager::ProcessMouseMove(int x, int y, int keyModifier) {
    if (m_context) {
        return m_context->ProcessMouseMove(x, y, keyModifier);
    }
    return true;// no context ⇒ nothing to interact with
}

void RmlUiManager::ProcessMouseButtonDown(int buttonIndex, int keyModifier) {
    if (m_context) {
        m_context->ProcessMouseButtonDown(buttonIndex, keyModifier);
    }
}

void RmlUiManager::ProcessMouseButtonUp(int buttonIndex, int keyModifier) {
    if (m_context) {
        m_context->ProcessMouseButtonUp(buttonIndex, keyModifier);
    }
}

void RmlUiManager::ProcessMouseWheel(float wheelDelta, int keyModifier) {
    if (m_context) {
        m_context->ProcessMouseWheel(Rml::Vector2f(0, wheelDelta), keyModifier);
    }
}
