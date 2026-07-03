#pragma once
#include "globals.hpp"
#include "subsystem.hpp"
#include "window.hpp"
#include <deque>

class InputSubsystem : public Subsystem
{
private:
    static InputSubsystem* _instance;

public:
    static InputSubsystem* Get()
    {
        return _instance;
    }

    InputSubsystem();
    ~InputSubsystem();

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    bool IsKeyDown(Key key);
    bool IsKeyUp(Key key);
    bool IsKeyPressed(Key key);
    bool IsKeyReleased(Key key);

    bool IsMouseButtonDown();
    bool IsMouseButtonPressed();

    glm::vec2 GetMousePosition();

private:
    std::unordered_map<Key, KeyState> _keyStates;
    std::unordered_map<Key, KeyState> _prevKeyStates;
    bool _mouseDown = false;
    bool _prevMouseDown = false;
    std::deque<KeyEvent> _keyPressHistory;
    const size_t _keyPressHistorySize = 16;
    const uint64_t KEY_EVENT_LIFETIME = 400;
};