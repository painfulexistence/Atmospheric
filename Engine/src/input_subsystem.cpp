#include "input_subsystem.hpp"
#include "window.hpp"
#include "application.hpp"

static std::string GetKeyName(Key key) {
    switch (key) {
        case Key::SPACE: return "SPACE";
        case Key::ESCAPE: return "ESC";
        case Key::ENTER: return "ENTER";
        case Key::A: return "A";
        case Key::B: return "B";
        case Key::C: return "C";
        case Key::D: return "D";
        case Key::E: return "E";
        case Key::F: return "F";
        case Key::G: return "G";
        case Key::H: return "H";
        case Key::I: return "I";
        case Key::J: return "J";
        case Key::K: return "K";
        case Key::L: return "L";
        case Key::M: return "M";
        case Key::N: return "N";
        case Key::O: return "O";
        case Key::P: return "P";
        case Key::Q: return "Q";
        case Key::R: return "R";
        case Key::S: return "S";
        case Key::T: return "T";
        case Key::U: return "U";
        case Key::V: return "V";
        case Key::W: return "W";
        case Key::X: return "X";
        case Key::Y: return "Y";
        case Key::Z: return "Z";
        case Key::UP: return "UP";
        case Key::DOWN: return "DOWN";
        case Key::LEFT: return "LEFT";
        case Key::RIGHT: return "RIGHT";
        case Key::Num1: return "NUM1";
        case Key::Num2: return "NUM2";
        case Key::Num3: return "NUM3";
        case Key::Num4: return "NUM4";
        case Key::Num5: return "NUM5";
        case Key::Num6: return "NUM6";
        case Key::Num7: return "NUM7";
        case Key::Num8: return "NUM8";
        case Key::Num9: return "NUM9";
        case Key::Num0: return "NUM0";
        default: return "UNKNOWN";
    }
}

InputSubsystem* InputSubsystem::_instance = nullptr;

InputSubsystem::InputSubsystem()
{
    if (_instance != nullptr)
        throw std::runtime_error("InputSubsystem is already initialized!");

    _instance = this;
}

InputSubsystem::~InputSubsystem()
{

}

void InputSubsystem::Init(Application* app)
{
    Subsystem::Init(app);

    for (int k = static_cast<int>(FIRST_KEY); k <= static_cast<int>(LAST_KEY); ++k) {
        auto key = static_cast<Key>(k);
        _keyStates[key] = KeyState::UNKNOWN;
        _prevKeyStates[key] = KeyState::RELEASED;
    }

    Window::Get()->AddKeyPressCallback([this, app](Key key, int mods) {
        _keyPressHistory.push_back({ key, app->GetClock() });
        if (_keyPressHistory.size() > _keyPressHistorySize) {
            _keyPressHistory.pop_front();
        }
    });
}

void InputSubsystem::Process(float dt)
{
    for (int k = static_cast<int>(FIRST_KEY); k <= static_cast<int>(LAST_KEY); ++k) {
        auto key = static_cast<Key>(k);
        _prevKeyStates[key] = _keyStates[key];
        _keyStates[key] = Window::Get()->GetKeyState(key);
    }
    _prevMouseDown = _mouseDown;
    _mouseDown = Window::Get()->GetMouseButtonState();
    uint64_t currentClock = _app->GetClock();
    while (!_keyPressHistory.empty() && (currentClock - _keyPressHistory.front().time) > KEY_EVENT_LIFETIME) {
        _keyPressHistory.pop_front();
    }
}

void InputSubsystem::DrawImGui(float dt)
{
    if (ImGui::CollapsingHeader("Input", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string recentKeys = "Recent keys:\n";
        for (auto event : _keyPressHistory) {
            recentKeys += fmt::format("-> [{}]", GetKeyName(event.key));
        }
        ImGui::TextWrapped("%s", recentKeys.c_str());
    }
}

bool InputSubsystem::IsKeyDown(Key key)
{
    return _keyStates[key] == KeyState::PRESSED;
}

bool InputSubsystem::IsKeyUp(Key key)
{
    return _keyStates[key] == KeyState::RELEASED;
}

bool InputSubsystem::IsKeyPressed(Key key)
{
    return _keyStates[key] == KeyState::PRESSED && _prevKeyStates[key] == KeyState::RELEASED;
}

bool InputSubsystem::IsKeyReleased(Key key)
{
    return _keyStates[key] == KeyState::RELEASED && _prevKeyStates[key] == KeyState::PRESSED;
}

bool InputSubsystem::IsMouseButtonDown()
{
    return _mouseDown;
}

bool InputSubsystem::IsMouseButtonPressed()
{
    return _mouseDown && !_prevMouseDown;
}

glm::vec2 InputSubsystem::GetMousePosition() // In pixel coordinate
{
    return Window::Get()->GetMousePosition();
};