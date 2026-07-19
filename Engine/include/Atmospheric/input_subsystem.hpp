#pragma once
#include "globals.hpp"
#include "subsystem.hpp"
#include "window.hpp"
#include <cstdint>
#include <deque>
#include <vector>

// One live touch contact, in physical/framebuffer pixels (the same space as
// GetMousePosition()). startPosition is where the finger first landed — a
// floating virtual joystick anchors its center there.
struct TouchPoint {
    int64_t id;
    glm::vec2 position;
    glm::vec2 startPosition;
};

class InputSubsystem : public Subsystem {
private:
    static InputSubsystem* _instance;

public:
    static InputSubsystem* Get() {
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

    // True when the cursor is over an interactive RmlUi element this frame.
    // Games should gate world-space mouse actions on !IsMouseOverUI() so a
    // click on the HUD doesn't also hit the scene behind it.
    bool IsMouseOverUI() const {
        return _mouseOverUi;
    }

    glm::vec2 GetMousePosition();

    // Active touch contacts in arrival order, updated from Window finger
    // events. Empty on platforms without a touchscreen. Unlike the mouse
    // (which SDL synthesizes from the first finger only), every finger is
    // listed — poll this for multi-touch controls such as virtual joysticks.
    const std::vector<TouchPoint>& GetTouches() const {
        return _touches;
    }

private:
    std::unordered_map<Key, KeyState> _keyStates;
    std::unordered_map<Key, KeyState> _prevKeyStates;
    bool _mouseDown = false;
    bool _prevMouseDown = false;
    bool _mouseOverUi = false;
    std::vector<TouchPoint> _touches;
    std::deque<KeyEvent> _keyPressHistory;
    const size_t _keyPressHistorySize = 16;
    const uint64_t KEY_EVENT_LIFETIME = 400;
};