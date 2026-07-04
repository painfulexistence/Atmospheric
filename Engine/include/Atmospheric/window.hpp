#pragma once
#include "config.hpp"
#include "globals.hpp"
#include <functional>
#include <unordered_map>

struct ImageSize {
    ImageSize(int width, int height) : width(width), height(height){};
    int width;
    int height;
};

struct WindowProps {
    std::string title = INIT_SCREEN_TITLE;
    int width = INIT_SCREEN_WIDTH;
    int height = INIT_SCREEN_HEIGHT;
    bool resizable = false;
    bool floating = false;
    bool fullscreen = false;
    bool vsync = VSYNC_ON;
};

// Active graphics backend.
enum class GfxBackend {
    WebGPU,  // Primary: Dawn on native / browser WebGPU on web
    OpenGL,  // Fallback: OpenGL 4.1 (native) / WebGL 2.0 (Emscripten)
};

enum class KeyState { PRESSED, RELEASED, HELD, UNKNOWN };

enum class Key {
    SPACE,
    ENTER,
    ESCAPE,
    UP,
    RIGHT,
    LEFT,
    DOWN,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Num0,
    Q,
    W,
    E,
    R,
    T,
    Y,
    U,
    I,
    O,
    P,
    A,
    S,
    D,
    F,
    G,
    H,
    J,
    K,
    L,
    Z,
    X,
    C,
    V,
    B,
    N,
    M,
    UNKNOWN
};
const Key FIRST_KEY = Key::SPACE;
const Key LAST_KEY = Key::UNKNOWN;

struct KeyEvent {
    Key key;
    uint64_t time;
};

using MouseMoveCallback = std::function<void(float, float)>;
using MouseEnterCallback = std::function<void()>;
using MouseLeaveCallback = std::function<void()>;
using KeyPressCallback = std::function<void(Key, int)>;
using KeyReleaseCallback = std::function<void(Key, int)>;
using ViewportResizeCallback = std::function<void(int, int)>;
using FramebufferResizeCallback = std::function<void(int, int)>;

using WindowEventCallbackID = uint32_t;

class Window {
private:
    static Window* _instance;

public:
    static Window* Get() {
        return _instance;
    }
    static void* GetProcAddress();

    // Returns true if the browser exposes navigator.gpu (WebGPU API).
    // Always false on native platforms. Result is cached after the first call.
    static bool IsWebGPUAvailable();

    // Returns the intended backend for this build target.
    // Actual backend may differ if GfxFactory::Init() falls back to OpenGL.
    static GfxBackend GetActiveBackend();

    Window(WindowProps props = {});
    ~Window();

    void Init();

    void InitImGui();
    void BeginImGuiFrame();
    void EndImGuiFrame();
    void DeinitImGui();

    void MainLoop(std::function<void(float, float)> callback);
    void SwapBuffers();
    void ToggleFullscreen();
    void Close();

    WindowEventCallbackID AddMouseMoveCallback(MouseMoveCallback callback);
    WindowEventCallbackID AddMouseEnterCallback(MouseEnterCallback callback);
    WindowEventCallbackID AddMouseLeaveCallback(MouseLeaveCallback callback);
    WindowEventCallbackID AddKeyPressCallback(KeyPressCallback callback);
    WindowEventCallbackID AddKeyReleaseCallback(KeyReleaseCallback callback);
    WindowEventCallbackID AddViewportResizeCallback(ViewportResizeCallback callback);
    WindowEventCallbackID AddFramebufferResizeCallback(FramebufferResizeCallback callback);
    void RemoveMouseMoveCallback(WindowEventCallbackID id);
    void RemoveMouseEnterCallback(WindowEventCallbackID id);
    void RemoveMouseLeaveCallback(WindowEventCallbackID id);
    void RemoveKeyPressCallback(WindowEventCallbackID id);
    void RemoveKeyReleaseCallback(WindowEventCallbackID id);
    void RemoveViewportResizeCallback(WindowEventCallbackID id);
    void RemoveFramebufferResizeCallback(WindowEventCallbackID id);
    void RemoveAllEventCallbacks();

    glm::vec2 GetMousePosition();

    bool GetMouseButtonState();

    // Legacy
    bool GetKeyDown(Key key);
    // Legacy
    bool GetKeyUp(Key key);
    KeyState GetKeyState(Key key);

    std::string GetTitle();
    void SetTitle(const std::string& title);

    float GetTime();
    void SetTime(double time);

    void SetClipboardText(const std::string& text);
    std::string GetClipboardText();
    void SetMouseCursor(const std::string& cursorName);

    ImageSize GetLogicalSize();
    ImageSize GetPhysicalSize();
    glm::vec2 GetDPI();

    // Returns the native window handle (SDL_Window* on SDL backend).
    void* GetNativeHandle() const { return _internal; }

private:
    void* _internal = nullptr;
    bool _isRunning = true;
    bool _isFullscreen = false;
    bool _webGPUCanvas = false; // true if canvas was left free for WebGPU (no GL context)
    int _windowedX = 0;
    int _windowedY = 0;
    int _windowedWidth = 0;
    int _windowedHeight = 0;
    float _scaleX = 1.0f;
    float _scaleY = 1.0f;
    WindowEventCallbackID _nextCallbackID = 0;
    std::unordered_map<WindowEventCallbackID, MouseMoveCallback> _mouseMoveCallbacks;
    std::unordered_map<WindowEventCallbackID, MouseEnterCallback> _mouseEnterCallbacks;
    std::unordered_map<WindowEventCallbackID, MouseLeaveCallback> _mouseLeaveCallbacks;
    std::unordered_map<WindowEventCallbackID, KeyPressCallback> _keyPressCallbacks;
    std::unordered_map<WindowEventCallbackID, KeyReleaseCallback> _keyReleaseCallbacks;
    std::unordered_map<WindowEventCallbackID, ViewportResizeCallback> _viewportResizeCallbacks;
    std::unordered_map<WindowEventCallbackID, FramebufferResizeCallback> _framebufferResizeCallbacks;
};