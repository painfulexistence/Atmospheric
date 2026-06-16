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

    glm::vec2 GetMousePosition();

private:
    std::unordered_map<Key, KeyState> _keyStates;
    std::unordered_map<Key, KeyState> _prevKeyStates;
    std::deque<KeyEvent> _keyPressHistory;
    const size_t _keyPressHistorySize = 16;
    const uint64_t KEY_EVENT_LIFETIME = 400;
};