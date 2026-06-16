#include "window.hpp"
#include <SDL2/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "console.hpp"

static int convertToSDLKey(Key key) {
    switch (key) {
    case Key::UP: return SDL_SCANCODE_UP;
    case Key::RIGHT: return SDL_SCANCODE_RIGHT;
    case Key::LEFT: return SDL_SCANCODE_LEFT;
    case Key::DOWN: return SDL_SCANCODE_DOWN;
    case Key::Num1: return SDL_SCANCODE_1;
    case Key::Num2: return SDL_SCANCODE_2;
    case Key::Num3: return SDL_SCANCODE_3;
    case Key::Num4: return SDL_SCANCODE_4;
    case Key::Num5: return SDL_SCANCODE_5;
    case Key::Num6: return SDL_SCANCODE_6;
    case Key::Num7: return SDL_SCANCODE_7;
    case Key::Num8: return SDL_SCANCODE_8;
    case Key::Num9: return SDL_SCANCODE_9;
    case Key::Num0: return SDL_SCANCODE_0;
    case Key::Q: return SDL_SCANCODE_Q;
    case Key::W: return SDL_SCANCODE_W;
    case Key::E: return SDL_SCANCODE_E;
    case Key::R: return SDL_SCANCODE_R;
    case Key::T: return SDL_SCANCODE_T;
    case Key::Y: return SDL_SCANCODE_Y;
    case Key::U: return SDL_SCANCODE_U;
    case Key::I: return SDL_SCANCODE_I;
    case Key::O: return SDL_SCANCODE_O;
    case Key::P: return SDL_SCANCODE_P;
    case Key::A: return SDL_SCANCODE_A;
    case Key::S: return SDL_SCANCODE_S;
    case Key::D: return SDL_SCANCODE_D;
    case Key::F: return SDL_SCANCODE_F;
    case Key::G: return SDL_SCANCODE_G;
    case Key::H: return SDL_SCANCODE_H;
    case Key::J: return SDL_SCANCODE_J;
    case Key::K: return SDL_SCANCODE_K;
    case Key::L: return SDL_SCANCODE_L;
    case Key::Z: return SDL_SCANCODE_Z;
    case Key::X: return SDL_SCANCODE_X;
    case Key::C: return SDL_SCANCODE_C;
    case Key::V: return SDL_SCANCODE_V;
    case Key::B: return SDL_SCANCODE_B;
    case Key::N: return SDL_SCANCODE_N;
    case Key::M: return SDL_SCANCODE_M;
    case Key::ESCAPE: return SDL_SCANCODE_ESCAPE;
    case Key::ENTER: return SDL_SCANCODE_RETURN;
    case Key::SPACE: return SDL_SCANCODE_SPACE;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

static Key convertFromSDLKey(int key) {
    switch (key) {
    case SDL_SCANCODE_UP: return Key::UP;
    case SDL_SCANCODE_RIGHT: return Key::RIGHT;
    case SDL_SCANCODE_LEFT: return Key::LEFT;
    case SDL_SCANCODE_DOWN: return Key::DOWN;
    case SDL_SCANCODE_1: return Key::Num1;
    case SDL_SCANCODE_2: return Key::Num2;
    case SDL_SCANCODE_3: return Key::Num3;
    case SDL_SCANCODE_4: return Key::Num4;
    case SDL_SCANCODE_5: return Key::Num5;
    case SDL_SCANCODE_6: return Key::Num6;
    case SDL_SCANCODE_7: return Key::Num7;
    case SDL_SCANCODE_8: return Key::Num8;
    case SDL_SCANCODE_9: return Key::Num9;
    case SDL_SCANCODE_0: return Key::Num0;
    case SDL_SCANCODE_Q: return Key::Q;
    case SDL_SCANCODE_W: return Key::W;
    case SDL_SCANCODE_E: return Key::E;
    case SDL_SCANCODE_R: return Key::R;
    case SDL_SCANCODE_T: return Key::T;
    case SDL_SCANCODE_Y: return Key::Y;
    case SDL_SCANCODE_U: return Key::U;
    case SDL_SCANCODE_I: return Key::I;
    case SDL_SCANCODE_O: return Key::O;
    case SDL_SCANCODE_P: return Key::P;
    case SDL_SCANCODE_A: return Key::A;
    case SDL_SCANCODE_S: return Key::S;
    case SDL_SCANCODE_D: return Key::D;
    case SDL_SCANCODE_F: return Key::F;
    case SDL_SCANCODE_G: return Key::G;
    case SDL_SCANCODE_H: return Key::H;
    case SDL_SCANCODE_J: return Key::J;
    case SDL_SCANCODE_K: return Key::K;
    case SDL_SCANCODE_L: return Key::L;
    case SDL_SCANCODE_Z: return Key::Z;
    case SDL_SCANCODE_X: return Key::X;
    case SDL_SCANCODE_C: return Key::C;
    case SDL_SCANCODE_V: return Key::V;
    case SDL_SCANCODE_B: return Key::B;
    case SDL_SCANCODE_N: return Key::N;
    case SDL_SCANCODE_M: return Key::M;
    case SDL_SCANCODE_ESCAPE: return Key::ESCAPE;
    case SDL_SCANCODE_RETURN: return Key::ENTER;
    case SDL_SCANCODE_SPACE: return Key::SPACE;
    default: return Key::UNKNOWN;
    }
}

Window* Window::_instance = nullptr;

Window::Window(WindowProps props) {
    if (_instance != nullptr) {
        throw std::runtime_error("Window is already initialized!");
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("SDL could not initialize! Error: %s\n", SDL_GetError());
    }
// #ifdef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
// #else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
// #endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG | SDL_GL_CONTEXT_DEBUG_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    _internal = SDL_CreateWindow(
        props.title.c_str(),
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        props.width, props.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!_internal) {
        SDL_Log("SDL could not create window! Error: %s\n", SDL_GetError());
    }

    auto window = static_cast<SDL_Window*>(_internal);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(props.vsync ? 1 : 0);

    _instance = this;
}

Window::~Window() {
    SDL_GL_DeleteContext(SDL_GL_GetCurrentContext());
    SDL_DestroyWindow(static_cast<SDL_Window*>(_internal));
    SDL_Quit();
}

void Window::Init() {
}

void* Window::GetProcAddress() {
    return (void*)SDL_GL_GetProcAddress;
}

void Window::InitImGui() {
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(_internal), SDL_GL_GetCurrentContext());
#ifdef __EMSCRIPTEN__
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    ImGui_ImplOpenGL3_Init("#version 410");
#endif

    ImGui::StyleColorsDark();
}

void Window::BeginImGuiFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void Window::EndImGuiFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Window::DeinitImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void Window::MainLoop(std::function<void(float, float)> callback) {
    struct LoopContext {
        std::function<void(float, float)> callback;
        float lastTime;
        float deltaTime;
        Window* window;
    };

    auto loop = [](LoopContext& ctx) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            switch (event.type) {
            case SDL_QUIT:
                ctx.window->Close();
                break;
            case SDL_WINDOWEVENT:
                switch (event.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                    for (auto [id, callback] : ctx.window->_framebufferResizeCallbacks) {
                        callback(event.window.data1, event.window.data2);
                    }
                    break;
                case SDL_WINDOWEVENT_ENTER:
                    for (auto [id, callback] : ctx.window->_mouseEnterCallbacks) {
                        callback();
                    }
                    break;
                case SDL_WINDOWEVENT_LEAVE:
                    for (auto [id, callback] : ctx.window->_mouseLeaveCallbacks) {
                        callback();
                    }
                    break;
                }
                break;
            case SDL_MOUSEMOTION:
                for (auto [id, callback] : ctx.window->_mouseMoveCallbacks) {
                    callback(event.motion.x, event.motion.y);
                }
                break;
            case SDL_KEYDOWN:
                for (auto [id, callback] : ctx.window->_keyPressCallbacks) {
                    callback(convertFromSDLKey(event.key.keysym.scancode), event.key.keysym.mod);
                }
                break;
            case SDL_KEYUP:
                for (auto [id, callback] : ctx.window->_keyReleaseCallbacks) {
                    callback(convertFromSDLKey(event.key.keysym.scancode), event.key.keysym.mod);
                }
                break;
            }
        }

        float currTime = ctx.window->GetTime();
        ctx.deltaTime = currTime - ctx.lastTime;
        ctx.lastTime = currTime;

        ctx.callback(currTime, ctx.deltaTime);

        SDL_GL_SwapWindow(static_cast<SDL_Window*>(ctx.window->_internal));
    };

    LoopContext ctx = {
        callback,
        GetTime(),
        0,
        this
    };
#ifdef __EMSCRIPTEN__
    static LoopContext* ctxPtr = &ctx;
    auto em_callback = [](void* arg) {
        auto ctx = *static_cast<LoopContext*>(arg);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // TODO: implement event handling
            ImGui_ImplSDL2_ProcessEvent(&event);
        }

        float currTime = ctx.window->GetTime();
        ctx.deltaTime = currTime - ctx.lastTime;
        ctx.lastTime = currTime;

        ctx.callback(currTime, ctx.deltaTime);

        SDL_GL_SwapWindow(static_cast<SDL_Window*>(ctx.window->_internal));
    };
    emscripten_set_main_loop_arg(em_callback, ctxPtr, 0, true);
#else
    while (_isRunning) {
        loop(ctx);
    }
#endif
}

void Window::ToggleFullscreen() {

}

void Window::Close() {
    _isRunning = false;
}

std::string Window::GetTitle() {
#ifdef __EMSCRIPTEN__
    return std::string("Atmospheric Engine");
#else
    return std::string(SDL_GetWindowTitle(static_cast<SDL_Window*>(_internal)));
#endif
}

void Window::SetTitle(const std::string& title) {
#ifdef __EMSCRIPTEN__
    return;
#else
    SDL_SetWindowTitle(static_cast<SDL_Window*>(_internal), title.c_str());
#endif
}

float Window::GetTime() {
    return (float)SDL_GetTicks() * 0.001f; // Note that glfwGetTime() only starts to calculate time after the window is created;
}

void Window::SetTime(double time) {
    ENGINE_LOG("SetTime is not implemented");
}

ImageSize Window::GetSize() {
    int width, height;
    SDL_GetWindowSize(static_cast<SDL_Window*>(_internal), &width, &height);
    return ImageSize(width, height);
}

ImageSize Window::GetFramebufferSize() {
    int width, height;
    SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(_internal), &width, &height);
    return ImageSize(width, height);
}

glm::vec2 Window::GetDPI() {
    auto window = static_cast<SDL_Window*>(_internal);
    int fw, fh;
    SDL_GL_GetDrawableSize(window, &fw, &fh);
    return glm::vec2(fw / GetSize().width, fh / GetSize().height);
}

glm::vec2 Window::GetMousePosition() {
    int x, y;
    SDL_GetMouseState(&x, &y);
    return glm::vec2(x, y);
}

// TODO: implement mouse button state
bool Window::GetMouseButtonState() {
    return SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK;
}

bool Window::GetKeyDown(Key key) {
    int sdlKey = convertToSDLKey(key);
    if (sdlKey == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    return SDL_GetKeyboardState(nullptr)[sdlKey];
}

bool Window::GetKeyUp(Key key) {
    int sdlKey = convertToSDLKey(key);
    if (sdlKey == SDL_SCANCODE_UNKNOWN) {
        return false;
    }
    return !SDL_GetKeyboardState(nullptr)[sdlKey];
}

KeyState Window::GetKeyState(Key key) {
    int sdlKey = convertToSDLKey(key);
    if (sdlKey == SDL_SCANCODE_UNKNOWN) {
        return KeyState::UNKNOWN;
    }
    bool pressed = SDL_GetKeyboardState(nullptr)[sdlKey];
    return pressed ? KeyState::PRESSED : KeyState::RELEASED;
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