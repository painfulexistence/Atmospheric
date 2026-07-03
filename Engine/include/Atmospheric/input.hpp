#pragma once
#include "globals.hpp"
#include "server.hpp"
#include "window.hpp"
#include <deque>

class Input : public Server
{
private:
    static Input* _instance;

public:
    static Input* Get()
    {
        return _instance;
    }

    Input();
    ~Input();

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;

    bool IsKeyDown(Key key);
    bool IsKeyUp(Key key);
    bool IsKeyPressed(Key key);
    bool IsKeyReleased(Key key);

    bool IsMouseButtonDown();
    bool IsMouseButtonPressed();

    // True when the cursor is over an interactive RmlUi element this frame.
    // Games should gate world-space mouse actions on !IsMouseOverUI() so a
    // click on the HUD doesn't also hit the scene behind it.
    bool IsMouseOverUI() const { return _mouseOverUi; }

    glm::vec2 GetMousePosition();

private:
    std::unordered_map<Key, KeyState> _keyStates;
    std::unordered_map<Key, KeyState> _prevKeyStates;
    bool _mouseDown = false;
    bool _prevMouseDown = false;
    bool _mouseOverUi = false;
    std::deque<KeyEvent> _keyPressHistory;
    const size_t _keyPressHistorySize = 16;
    const uint64_t KEY_EVENT_LIFETIME = 400;
};