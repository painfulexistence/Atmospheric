#pragma once
#include "component.hpp"
#include "fsm.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// FSMComponent — a state machine on a GameObject
//
// The GameObject/Component adapter over the pure FSM logic in fsm.hpp. Owns
// per-instance state (current state, timers, pending events) and steps once
// per OnTick; the definition is shared, so many components can run the same
// machine. Wire OnStateChange to perform actions on transitions — combined
// with ActionTimelineComponent events (which can PushEvent here via
// SetOnEvent) and UINavigator, this is the cinematic orchestration pattern
// ported from Vapor's subtitle/letterbox systems.
//
//   auto def = std::make_shared<FSMDefinition>(FSMDefinitionBuilder()
//       .Transition("Hidden",  "FadingIn",  "Show")
//       .TimedTransition("FadingIn", "Visible", 0.3f)
//       .Transition("Visible", "FadingOut", "Hide")
//       .TimedTransition("FadingOut", "Hidden", 0.3f)
//       .InitialState("Hidden").Build());
//   auto* fsm = go->AddComponent<FSMComponent>(def);
//   fsm->SetOnStateChange([=](const FSMStateChange& c) { ... });
//   fsm->PushEvent("Show");
// ─────────────────────────────────────────────────────────────────────────────
class FSMComponent : public Component {
public:
    FSMComponent(GameObject* owner, std::shared_ptr<FSMDefinition> definition);

    std::string GetName() const override {
        return "FSM";
    }

    void OnAttach() override;
    void OnTick(float dt) override;
    void DrawImGui() override;

    // Queue an event for the next tick (multiple events keep their order; the
    // first matching one wins, exactly like Vapor's FSMSystem).
    void PushEvent(const std::string& event) {
        _pendingEvents.push_back(event);
    }

    // Force a state (clears timers; fires OnStateChange). For restarts.
    void SetState(uint32_t state);
    void SetState(const std::string& stateName);

    uint32_t GetState() const {
        return _state.currentState;
    }
    const std::string& GetStateName() const {
        static const std::string empty;
        return _definition ? _definition->GetStateName(_state.currentState) : empty;
    }
    float GetStateTime() const {
        return _state.stateTime;
    }
    bool IsInState(const std::string& stateName) const {
        return _definition && _state.currentState == _definition->GetStateIndex(stateName);
    }

    const std::shared_ptr<FSMDefinition>& GetDefinition() const {
        return _definition;
    }

    void SetOnStateChange(std::function<void(const FSMStateChange&)> cb) {
        _onStateChange = std::move(cb);
    }

private:
    std::shared_ptr<FSMDefinition> _definition;
    FSMState _state;
    std::vector<std::string> _pendingEvents;
    std::function<void(const FSMStateChange&)> _onStateChange;
};
