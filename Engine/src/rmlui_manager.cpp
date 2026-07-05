#include "rmlui_manager.hpp"
#include "file_system.hpp"
#include "log.hpp"
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
    if (_initialized) {
        Log::Warn("RmlUiManager already initialized");
        return true;
    }

    _width = width;
    _height = height;

    // Create renderer and system interfaces
    _renderer = std::make_unique<RmlUiRenderer>(renderer);
    _system = std::make_unique<RmlUiSystem>();

    // Initialize renderer
    _renderer->Initialize();

    // Set interfaces
    Rml::SetFileInterface(&gsRmlFileInterface);
    Rml::SetRenderInterface(_renderer.get());
    Rml::SetSystemInterface(_system.get());

    // Initialize RmlUi
    if (!Rml::Initialise()) {
        Log::Error("Failed to initialize RmlUi");
        return false;
    }

    // Load fonts
    if (!Rml::LoadFontFace("assets/fonts/NotoSans-SemiBold.ttf")) {
        Log::Warn(
            "Failed to load default font, UI text may not render correctly. Consider using 'rmlui-debugger-font' in "
            "your "
            "RCSS."
        );
    }

    // Create the main UI context
    _context = Rml::CreateContext("main", Rml::Vector2i(width, height));
    if (!_context) {
        Log::Error("Failed to create RmlUi context");
        Rml::Shutdown();
        return false;
    }

    // Initialize debugger (useful for development)
    Rml::Debugger::Initialise(_context);

    _initialized = true;
    Log::Info("RmlUi initialized successfully ({}x{})", width, height);

    return true;
}

void RmlUiManager::Shutdown() {
    if (!_initialized) return;

    if (_context) {
        Rml::Debugger::Shutdown();
        Rml::RemoveContext(_context->GetName());
        _context = nullptr;
    }

    Rml::Shutdown();

    if (_renderer) {
        _renderer->Shutdown();
        _renderer.reset();
    }

    _system.reset();

    _initialized = false;
    Log::Info("RmlUi shutdown complete");
}

void RmlUiManager::Update(float deltaTime) {
    if (!_initialized || !_context) return;

    // Update the context
    _context->Update();
}

void RmlUiManager::Render() {
    ZoneScopedN("RmlUiManager::Render");
    if (!_initialized || !_context) return;

    // Render the context (generates commands to Renderer)
    _context->Render();
}

void RmlUiManager::OnResize(int width, int height) {
    _width = width;
    _height = height;

    if (_context) {
        // TODO: width/height must be logical pixels (GetSize()), not framebuffer pixels (GetPhysicalSize()),
        // to match RmlUi's CSS px unit expectations. Wire this to ViewportResizeCallback, not
        // FramebufferResizeCallback.
        _context->SetDimensions(Rml::Vector2i(width, height));
    }
}

Rml::ElementDocument* RmlUiManager::LoadDocument(const std::string& path) {
    if (!_context) {
        Log::Error("Cannot load document: RmlUi not initialized");
        return nullptr;
    }

    Rml::ElementDocument* document = _context->LoadDocument(path);
    if (!document) {
        Log::Error("Failed to load RmlUi document: {}", path);
        return nullptr;
    }

    Log::Info("Loaded RmlUi document: {}", path);
    return document;
}

void RmlUiManager::UnloadDocument(Rml::ElementDocument* document) {
    // After Shutdown() the context (and every document it owned) is already
    // destroyed; closing a stale pointer then would be use-after-free.
    if (_context && document) {
        document->Close();
    }
}

void RmlUiManager::ShowDocument(const std::string& id) {
    if (!_context) return;

    Rml::ElementDocument* document = _context->GetDocument(id);
    if (document) {
        document->Show();
    } else {
        Log::Warn("Document not found: {}", id);
    }
}

void RmlUiManager::HideDocument(const std::string& id) {
    if (!_context) return;

    Rml::ElementDocument* document = _context->GetDocument(id);
    if (document) {
        document->Hide();
    }
}

// Input handling methods
void RmlUiManager::ProcessKeyDown(Rml::Input::KeyIdentifier key, int keyModifier) {
    if (_context) {
        _context->ProcessKeyDown(key, keyModifier);
    }
}

void RmlUiManager::ProcessKeyUp(Rml::Input::KeyIdentifier key, int keyModifier) {
    if (_context) {
        _context->ProcessKeyUp(key, keyModifier);
    }
}

void RmlUiManager::ProcessTextInput(Rml::Character character) {
    if (_context) {
        _context->ProcessTextInput(character);
    }
}

bool RmlUiManager::ProcessMouseMove(int x, int y, int keyModifier) {
    if (_context) {
        return _context->ProcessMouseMove(x, y, keyModifier);
    }
    return true;// no context ⇒ nothing to interact with
}

void RmlUiManager::ProcessMouseButtonDown(int buttonIndex, int keyModifier) {
    if (_context) {
        _context->ProcessMouseButtonDown(buttonIndex, keyModifier);
    }
}

void RmlUiManager::ProcessMouseButtonUp(int buttonIndex, int keyModifier) {
    if (_context) {
        _context->ProcessMouseButtonUp(buttonIndex, keyModifier);
    }
}

void RmlUiManager::ProcessMouseWheel(float wheelDelta, int keyModifier) {
    if (_context) {
        _context->ProcessMouseWheel(Rml::Vector2f(0, wheelDelta), keyModifier);
    }
}
