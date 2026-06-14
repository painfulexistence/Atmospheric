#include "window.hpp"
#include <GLFW/glfw3.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "console.hpp"

static int convertToGlfwKey(Key key) {
    switch (key) {
    case Key::UP: return GLFW_KEY_UP;
    case Key::RIGHT: return GLFW_KEY_RIGHT;
    case Key::LEFT: return GLFW_KEY_LEFT;
    case Key::DOWN: return GLFW_KEY_DOWN;
    case Key::Num1: return GLFW_KEY_1;
    case Key::Num2: return GLFW_KEY_2;
    case Key::Num3: return GLFW_KEY_3;
    case Key::Num4: return GLFW_KEY_4;
    case Key::Num5: return GLFW_KEY_5;
    case Key::Num6: return GLFW_KEY_6;
    case Key::Num7: return GLFW_KEY_7;
    case Key::Num8: return GLFW_KEY_8;
    case Key::Num9: return GLFW_KEY_9;
    case Key::Num0: return GLFW_KEY_0;
    case Key::Q: return GLFW_KEY_Q;
    case Key::W: return GLFW_KEY_W;
    case Key::E: return GLFW_KEY_E;
    case Key::R: return GLFW_KEY_R;
    case Key::T: return GLFW_KEY_T;
    case Key::Y: return GLFW_KEY_Y;
    case Key::U: return GLFW_KEY_U;
    case Key::I: return GLFW_KEY_I;
    case Key::O: return GLFW_KEY_O;
    case Key::P: return GLFW_KEY_P;
    case Key::A: return GLFW_KEY_A;
    case Key::S: return GLFW_KEY_S;
    case Key::D: return GLFW_KEY_D;
    case Key::F: return GLFW_KEY_F;
    case Key::G: return GLFW_KEY_G;
    case Key::H: return GLFW_KEY_H;
    case Key::J: return GLFW_KEY_J;
    case Key::K: return GLFW_KEY_K;
    case Key::L: return GLFW_KEY_L;
    case Key::Z: return GLFW_KEY_Z;
    case Key::X: return GLFW_KEY_X;
    case Key::C: return GLFW_KEY_C;
    case Key::V: return GLFW_KEY_V;
    case Key::B: return GLFW_KEY_B;
    case Key::N: return GLFW_KEY_N;
    case Key::M: return GLFW_KEY_M;
    case Key::ESCAPE: return GLFW_KEY_ESCAPE;
    case Key::ENTER: return GLFW_KEY_ENTER;
    case Key::SPACE: return GLFW_KEY_SPACE;
    default: return GLFW_KEY_UNKNOWN;
    }
}

static Key convertFromGlfwKey(int key) {
    switch (key) {
    case GLFW_KEY_UP: return Key::UP;
    case GLFW_KEY_RIGHT: return Key::RIGHT;
    case GLFW_KEY_LEFT: return Key::LEFT;
    case GLFW_KEY_DOWN: return Key::DOWN;
    case GLFW_KEY_Q: return Key::Q;
    case GLFW_KEY_W: return Key::W;
    case GLFW_KEY_E: return Key::E;
    case GLFW_KEY_R: return Key::R;
    case GLFW_KEY_T: return Key::T;
    case GLFW_KEY_Y: return Key::Y;
    case GLFW_KEY_U: return Key::U;
    case GLFW_KEY_I: return Key::I;
    case GLFW_KEY_O: return Key::O;
    case GLFW_KEY_P: return Key::P;
    case GLFW_KEY_A: return Key::A;
    case GLFW_KEY_S: return Key::S;
    case GLFW_KEY_D: return Key::D;
    case GLFW_KEY_F: return Key::F;
    case GLFW_KEY_G: return Key::G;
    case GLFW_KEY_H: return Key::H;
    case GLFW_KEY_J: return Key::J;
    case GLFW_KEY_K: return Key::K;
    case GLFW_KEY_L: return Key::L;
    case GLFW_KEY_Z: return Key::Z;
    case GLFW_KEY_X: return Key::X;
    case GLFW_KEY_C: return Key::C;
    case GLFW_KEY_V: return Key::V;
    case GLFW_KEY_B: return Key::B;
    case GLFW_KEY_N: return Key::N;
    case GLFW_KEY_M: return Key::M;
    case GLFW_KEY_ESCAPE: return Key::ESCAPE;
    case GLFW_KEY_ENTER: return Key::ENTER;
    case GLFW_KEY_SPACE: return Key::SPACE;
    case GLFW_KEY_1: return Key::Num1;
    case GLFW_KEY_2: return Key::Num2;
    case GLFW_KEY_3: return Key::Num3;
    case GLFW_KEY_4: return Key::Num4;
    case GLFW_KEY_5: return Key::Num5;
    case GLFW_KEY_6: return Key::Num6;
    case GLFW_KEY_7: return Key::Num7;
    case GLFW_KEY_8: return Key::Num8;
    case GLFW_KEY_9: return Key::Num9;
    case GLFW_KEY_0: return Key::Num0;
    default: return Key::UNKNOWN;
    }
}

Window* Window::_instance = nullptr;

Window::Window(WindowProps props) {
    if (_instance != nullptr) {
        throw std::runtime_error("Window is already initialized!");
    }

    glfwSetErrorCallback([](int code, const char* msg) {
        throw std::runtime_error(fmt::format("Error occurred: {}\n", msg));
    });
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW!");
    }

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_RESIZABLE, props.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, props.floating ? GLFW_TRUE : GLFW_FALSE);

    if (props.fullscreen) {
        this->_internal = glfwCreateWindow(props.width, props.height, props.title.c_str(), glfwGetPrimaryMonitor(), NULL);
        _isFullscreen = true;
    } else {
        this->_internal = glfwCreateWindow(props.width, props.height, props.title.c_str(), NULL, NULL);
        _isFullscreen = false;
    }
    if (this->_internal == nullptr)
        throw std::runtime_error("Failed to create window!");

    GLFWwindow* window = static_cast<GLFWwindow*>(this->_internal);
    glfwSetWindowUserPointer(window, this);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(props.vsync ? 1 : 0);

    _instance = this;
}

Window::~Window() {
    glfwDestroyWindow(static_cast<GLFWwindow*>(_internal));
    glfwTerminate();
}

void Window::Init() {
    GLFWwindow* window = static_cast<GLFWwindow*>(_internal);
    glfwGetWindowSize(window, &_windowedWidth, &_windowedHeight);
    glfwGetWindowPos(window, &_windowedX, &_windowedY);

    // Setup input management
    // glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double x, double y) {
        auto self = static_cast<Window*>(glfwGetWindowUserPointer(win));
        for (auto [id, callback] : self->_mouseMoveCallbacks) {
            callback((float)x, (float)y);
        }
    });
    glfwSetCursorEnterCallback(window, [](GLFWwindow* win, int entered) {
        auto self = static_cast<Window*>(glfwGetWindowUserPointer(win));
        if (entered) {
            for (auto [id, callback] : self->_mouseEnterCallbacks) {
                callback();
            }
        } else {
            for (auto [id, callback] : self->_mouseLeaveCallbacks) {
                callback();
            }
        }
    });
    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        auto self = static_cast<Window*>(glfwGetWindowUserPointer(win));
        // TODO: implement key mods and scancode
        if (action == GLFW_PRESS) {
            for (auto [id, callback] : self->_keyPressCallbacks) {
                callback(convertFromGlfwKey(key), mods);
            }
        } else {
            for (auto [id, callback] : self->_keyReleaseCallbacks) {
                callback(convertFromGlfwKey(key), mods);
            }
        }
    });
    glfwSetWindowSizeCallback(window, [](GLFWwindow* win, int width, int height) {
        auto self = static_cast<Window*>(glfwGetWindowUserPointer(win));
        for (auto [id, callback] : self->_viewportResizeCallbacks) {
            callback(width, height);
        }
    });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int width, int height) {
        auto self = static_cast<Window*>(glfwGetWindowUserPointer(win));
        for (auto [id, callback] : self->_framebufferResizeCallbacks) {
            callback(width, height);
        }
    });

#ifdef __EMSCRIPTEN__
    emscripten_set_fullscreenchange_callback(
        EMSCRIPTEN_EVENT_TARGET_DOCUMENT,
        this,
        EM_FALSE,
        [](int eventType, const EmscriptenFullscreenChangeEvent* event, void* userData) -> EM_BOOL {
            auto self = static_cast<Window*>(userData);
            if (self) {
                self->_isFullscreen = event->isFullscreen;
            }
            return EM_TRUE;
        }
    );
#endif

    // Default event listeners
    AddMouseMoveCallback([](float x, float y) {
        // ENGINE_LOG("-- Mouse moved to ({},{})\n", x, y);
    });
    AddViewportResizeCallback([](int width, int height) {
        ENGINE_LOG("Viewport resized to {}X{}\n", width, height);
    });
    AddFramebufferResizeCallback([](int width, int height) {
        ENGINE_LOG("Framebuffer resized to {}X{}\n", width, height);
    });
}

void* Window::GetProcAddress() {
    return (void*)glfwGetProcAddress;
}

bool Window::IsWebGPUAvailable() {
#ifdef __EMSCRIPTEN__
    // Cache the result — navigator.gpu is immutable after page load.
    static int cached = -1;
    if (cached == -1) {
        cached = EM_ASM_INT({
            return (typeof navigator !== 'undefined' &&
                    typeof navigator.gpu !== 'undefined' &&
                    navigator.gpu !== null) ? 1 : 0;
        });
        if (cached) {
            EM_ASM({ console.log("[AtmosphericEngine] WebGPU detected: navigator.gpu is available."); });
        } else {
            EM_ASM({ console.log("[AtmosphericEngine] WebGPU not available — falling back to WebGL 2."); });
        }
    }
    return cached != 0;
#else
    return false;
#endif
}

GfxBackend Window::GetActiveBackend() {
    // WebGPU is the target web backend.
    // Requires AE_USE_WEBGPU=ON in CMake + a browser that supports WebGPU.
    return GfxBackend::WebGPU;
}

void Window::InitImGui() {
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(_internal), true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 410");
#endif

    ImGui::StyleColorsDark();
}

void Window::BeginImGuiFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Window::EndImGuiFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Window::DeinitImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Window::MainLoop(std::function<void(float, float)> callback)
{
    struct LoopContext {
        std::function<void(float, float)> callback;
        float lastTime;
        float deltaTime;
        Window* window;
    };

    auto loop = [](LoopContext& ctx) {
        glfwPollEvents();

        float currTime = ctx.window->GetTime();
        ctx.deltaTime = currTime - ctx.lastTime;
        ctx.lastTime = currTime;

        ctx.window->BeginImGuiFrame();
        ctx.callback(currTime, ctx.deltaTime);
        ctx.window->EndImGuiFrame();

        glfwSwapBuffers(static_cast<GLFWwindow*>(ctx.window->_internal));
    };

    static LoopContext ctx = {
        callback,
        GetTime(),
        0,
        this
    };
#ifdef __EMSCRIPTEN__
    static LoopContext* ctxPtr = &ctx;
    auto em_callback = [](void* arg) {
        auto& ctx = *static_cast<LoopContext*>(arg);
        glfwPollEvents();

        float currTime = ctx.window->GetTime();
        ctx.deltaTime = currTime - ctx.lastTime;
        ctx.lastTime = currTime;

        ctx.window->BeginImGuiFrame();
        ctx.callback(currTime, ctx.deltaTime);
        ctx.window->EndImGuiFrame();

        glfwSwapBuffers(static_cast<GLFWwindow*>(ctx.window->_internal));
    };
    emscripten_set_main_loop_arg(em_callback, ctxPtr, 0, true);
#else
    while (_isRunning) {
        loop(ctx);
    }
#endif
}

void Window::ToggleFullscreen() {
#ifdef __EMSCRIPTEN__
    if (!_isFullscreen) {
        EmscriptenFullscreenStrategy strategy;
        strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_ASPECT;
        strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_STDDEF;
        strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
        strategy.canvasResizedCallback = nullptr;
        strategy.canvasResizedCallbackUserData = nullptr;

        // Request fullscreen on the main canvas element (ID "canvas" is standard in Emscripten templates)
        // Set deferUntilInEventHandler to true so the request succeeds if called asynchronously.
        emscripten_request_fullscreen_strategy("#canvas", EM_TRUE, &strategy);
    } else {
        emscripten_exit_fullscreen();
    }
#else
    GLFWwindow* window = static_cast<GLFWwindow*>(_internal);
    if (!_isFullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        // FIXME: not working on macOS
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        _isFullscreen = true;
    } else {
        glfwSetWindowMonitor(window, nullptr, _windowedX, _windowedY, _windowedWidth, _windowedHeight, 0);
        _isFullscreen = false;
    }
#endif
}

void Window::Close() {
    _isRunning = false;
}

std::string Window::GetTitle() {
#ifdef __EMSCRIPTEN__
    return std::string("Atmospheric Engine");
#else
    return std::string(glfwGetWindowTitle(static_cast<GLFWwindow*>(_internal)));
#endif
}

void Window::SetTitle(const std::string& title) {
#ifdef __EMSCRIPTEN__
    return;
#else
    glfwSetWindowTitle(static_cast<GLFWwindow*>(_internal), title.c_str());
#endif
}

float Window::GetTime() {
    return (float)glfwGetTime(); // Note that glfwGetTime() only starts to calculate time after the window is created;
}

void Window::SetTime(double time) {
    glfwSetTime(time);
}

ImageSize Window::GetSize() {
    int width, height;
    glfwGetWindowSize(static_cast<GLFWwindow*>(_internal), &width, &height);
    return ImageSize(width, height);
}

ImageSize Window::GetFramebufferSize() {
    int width, height;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(_internal), &width, &height);
    return ImageSize(width, height);
}

glm::vec2 Window::GetDPI() {
    float scaleX, scaleY;
    glfwGetWindowContentScale(static_cast<GLFWwindow*>(_internal), &scaleX, &scaleY);
    return glm::vec2(scaleX, scaleY);
}

glm::vec2 Window::GetMousePosition()
{
    double x, y;
    glfwGetCursorPos(static_cast<GLFWwindow*>(_internal), &x, &y);
    return glm::vec2((float)x, (float)y);
}

// TODO: implement mouse button state
bool Window::GetMouseButtonState() {
    return glfwGetMouseButton(static_cast<GLFWwindow*>(_internal), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}

bool Window::GetKeyDown(Key key) {
    int glfwKey = convertToGlfwKey(key);
    if (glfwKey == GLFW_KEY_UNKNOWN) {
        return false;
    }

    return glfwGetKey(static_cast<GLFWwindow*>(_internal), glfwKey) == GLFW_PRESS;
}

bool Window::GetKeyUp(Key key) {
    int glfwKey = convertToGlfwKey(key);
    if (glfwKey == GLFW_KEY_UNKNOWN) {
        return false;
    }

    return glfwGetKey(static_cast<GLFWwindow*>(_internal), glfwKey) == GLFW_RELEASE;
}

KeyState Window::GetKeyState(Key key) {
    int glfwKey = convertToGlfwKey(key);
    if (glfwKey == GLFW_KEY_UNKNOWN) {
        return KeyState::UNKNOWN;
    }

    int state = glfwGetKey(static_cast<GLFWwindow*>(_internal), glfwKey);
    switch (state) {
    case GLFW_RELEASE:
        return KeyState::RELEASED;
    case GLFW_PRESS:
        return KeyState::PRESSED;
    case GLFW_REPEAT:
        return KeyState::HELD;
    default:
        return KeyState::UNKNOWN;
    }
}

WindowEventCallbackID Window::AddMouseMoveCallback(MouseMoveCallback callback) {
    _mouseMoveCallbacks[_nextCallbackID] = callback;
    return _nextCallbackID++;
}

WindowEventCallbackID Window::AddMouseEnterCallback(MouseEnterCallback callback) {
    _mouseEnterCallbacks[_nextCallbackID] = callback;
    return _nextCallbackID++;
}

WindowEventCallbackID Window::AddMouseLeaveCallback(MouseLeaveCallback callback) {
    _mouseLeaveCallbacks[_nextCallbackID] = callback;
    return _nextCallbackID++;
}

WindowEventCallbackID Window::AddKeyPressCallback(KeyPressCallback callback) {
    _keyPressCallbacks[_nextCallbackID] = callback;
    return _nextCallbackID++;
}

WindowEventCallbackID Window::AddKeyReleaseCallback(KeyReleaseCallback callback) {
    _keyReleaseCallbacks[_nextCallbackID] = callback;
    return _nextCallbackID++;
}

WindowEventCallbackID Window::AddViewportResizeCallback(ViewportResizeCallback callback) {
    _viewportResizeCallbacks[_nextCallbackID] = callback;
    return _nextCallbackID++;
}

WindowEventCallbackID Window::AddFramebufferResizeCallback(FramebufferResizeCallback callback) {
    _framebufferResizeCallbacks[_nextCallbackID] = callback;
    return _nextCallbackID++;
}

void Window::RemoveMouseMoveCallback(WindowEventCallbackID id) {
    _mouseMoveCallbacks.erase(id);
}

void Window::RemoveMouseEnterCallback(WindowEventCallbackID id) {
    _mouseEnterCallbacks.erase(id);
}

void Window::RemoveMouseLeaveCallback(WindowEventCallbackID id) {
    _mouseLeaveCallbacks.erase(id);
}

void Window::RemoveKeyPressCallback(WindowEventCallbackID id) {
    _keyPressCallbacks.erase(id);
}

void Window::RemoveKeyReleaseCallback(WindowEventCallbackID id) {
    _keyReleaseCallbacks.erase(id);
}

void Window::RemoveViewportResizeCallback(WindowEventCallbackID id) {
    _viewportResizeCallbacks.erase(id);
}

void Window::RemoveFramebufferResizeCallback(WindowEventCallbackID id) {
    _framebufferResizeCallbacks.erase(id);
}

void Window::RemoveAllEventCallbacks() {
    _mouseMoveCallbacks.clear();
    _mouseEnterCallbacks.clear();
    _mouseLeaveCallbacks.clear();
    _keyPressCallbacks.clear();
    _keyReleaseCallbacks.clear();
    _viewportResizeCallbacks.clear();
    _framebufferResizeCallbacks.clear();
}
void Window::SetClipboardText(const std::string& text) {
    glfwSetClipboardString(static_cast<GLFWwindow*>(_internal), text.c_str());
}

std::string Window::GetClipboardText() {
    const char* text = glfwGetClipboardString(static_cast<GLFWwindow*>(_internal));
    return text ? text : "";
}

void Window::SetMouseCursor(const std::string& cursorName) {
    int shape = GLFW_ARROW_CURSOR;
    if (cursorName == "text")    shape = GLFW_IBEAM_CURSOR;
    else if (cursorName == "cross")   shape = GLFW_CROSSHAIR_CURSOR;
    else if (cursorName == "pointer") shape = GLFW_HAND_CURSOR;
    else if (cursorName == "resize")  shape = GLFW_HRESIZE_CURSOR;
    else if (cursorName == "move")    shape = GLFW_CROSSHAIR_CURSOR;

    // Cache cursors to avoid re-creating them every frame
    static std::unordered_map<int, GLFWcursor*> cursorCache;
    auto it = cursorCache.find(shape);
    if (it == cursorCache.end()) {
        GLFWcursor* cursor = glfwCreateStandardCursor(shape);
        cursorCache[shape] = cursor;
        it = cursorCache.find(shape);
    }
    if (it != cursorCache.end() && it->second) {
        glfwSetCursor(static_cast<GLFWwindow*>(_internal), it->second);
    }
}
