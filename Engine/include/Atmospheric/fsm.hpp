#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// FSM — shared state-machine definition + pure step logic
//
// Ported from Vapor's fsm.hpp/fsm_system.hpp (the layer its cinematic flows —
// subtitle queues, letterbox sequencing — are orchestrated with), translated
// out of entt: the definition and step function below are plain data / pure
// logic with no engine dependencies, and FSMComponent (fsm_component.hpp)
// adapts them to the GameObject/Component lifecycle.
//
// Semantics (identical to Vapor's FSMSystem::update, per step):
//   1. process queued events in order — the first rule matching
//      (currentState, event, stateTime >= minStateTime) transitions; at most
//      one transition per step; the queue is consumed either way
//   2. if no event fired, check timed transitions
//   3. report the transition (if any), then advance the timers by dt
// ─────────────────────────────────────────────────────────────────────────────

// Transition rule — event triggered.
struct FSMTransitionRule {
    uint32_t fromState = 0;
    uint32_t toState = 0;
    std::string triggerEvent;
    float minStateTime = 0.0f;

    FSMTransitionRule() = default;
    FSMTransitionRule(uint32_t from, uint32_t to, std::string event, float minTime = 0.0f)
        : fromState(from), toState(to), triggerEvent(std::move(event)), minStateTime(minTime) {
    }
};

// Timed transition — auto-transition after a duration in the state.
struct FSMTimedTransition {
    uint32_t fromState = 0;
    uint32_t toState = 0;
    float duration = 0.0f;

    FSMTimedTransition() = default;
    FSMTimedTransition(uint32_t from, uint32_t to, float dur) : fromState(from), toState(to), duration(dur) {
    }
};

// FSM definition — states and transitions. Shareable: any number of
// FSMComponents (or hand-rolled steppers) can run the same definition.
struct FSMDefinition {
    std::vector<std::string> stateNames;
    std::vector<FSMTransitionRule> eventTransitions;
    std::vector<FSMTimedTransition> timedTransitions;
    uint32_t initialState = 0;

    uint32_t GetStateIndex(const std::string& name) const {
        for (uint32_t i = 0; i < stateNames.size(); ++i) {
            if (stateNames[i] == name) return i;
        }
        return 0;
    }

    const std::string& GetStateName(uint32_t index) const {
        static const std::string empty;
        return index < stateNames.size() ? stateNames[index] : empty;
    }
};

// Per-instance state a definition runs against.
struct FSMState {
    uint32_t currentState = 0;
    float stateTime = 0.0f;
    float totalTime = 0.0f;
};

// A transition that fired during FSMStep.
struct FSMStateChange {
    uint32_t fromState = 0;
    uint32_t toState = 0;
    float previousStateTime = 0.0f;
};

// One frame of FSM logic (pure; see the semantics block above). `events` is
// consumed. Returns the transition if one fired.
inline std::optional<FSMStateChange>
    FSMStep(const FSMDefinition& def, FSMState& state, std::vector<std::string>& events, float dt) {
    const uint32_t previousState = state.currentState;
    const float previousStateTime = state.stateTime;
    bool transitioned = false;

    // 1. Process events
    for (const auto& event : events) {
        if (transitioned) break;
        for (const auto& rule : def.eventTransitions) {
            if (rule.fromState == state.currentState && rule.triggerEvent == event
                && state.stateTime >= rule.minStateTime) {
                state.currentState = rule.toState;
                state.stateTime = 0.0f;
                transitioned = true;
                break;
            }
        }
    }
    events.clear();

    // 2. Timed transitions
    if (!transitioned) {
        for (const auto& timed : def.timedTransitions) {
            if (timed.fromState == state.currentState && state.stateTime >= timed.duration) {
                state.currentState = timed.toState;
                state.stateTime = 0.0f;
                transitioned = true;
                break;
            }
        }
    }

    // 3. Advance timers
    state.stateTime += dt;
    state.totalTime += dt;

    if (transitioned) return FSMStateChange{ previousState, state.currentState, previousStateTime };
    return std::nullopt;
}

// Fluent builder for FSMDefinition.
//
//   auto def = FSMDefinitionBuilder()
//       .State("Idle")
//       .State("Walk")
//       .Transition("Idle", "Walk", "StartWalk")
//       .Transition("Walk", "Idle", "Stop")
//       .TimedTransition("Walk", "Idle", 5.0f)
//       .InitialState("Idle")
//       .Build();
class FSMDefinitionBuilder {
public:
    FSMDefinitionBuilder& State(const std::string& name) {
        GetOrCreateState(name);
        return *this;
    }

    FSMDefinitionBuilder&
        Transition(const std::string& from, const std::string& to, const std::string& event, float minTime = 0.0f) {
        const uint32_t fromIdx = GetOrCreateState(from);
        const uint32_t toIdx = GetOrCreateState(to);
        _definition.eventTransitions.emplace_back(fromIdx, toIdx, event, minTime);
        return *this;
    }

    FSMDefinitionBuilder& TimedTransition(const std::string& from, const std::string& to, float duration) {
        const uint32_t fromIdx = GetOrCreateState(from);
        const uint32_t toIdx = GetOrCreateState(to);
        _definition.timedTransitions.emplace_back(fromIdx, toIdx, duration);
        return *this;
    }

    FSMDefinitionBuilder& InitialState(const std::string& name) {
        _definition.initialState = GetOrCreateState(name);
        return *this;
    }

    FSMDefinition Build() {
        return std::move(_definition);
    }

private:
    FSMDefinition _definition;

    uint32_t GetOrCreateState(const std::string& name) {
        for (uint32_t i = 0; i < _definition.stateNames.size(); ++i) {
            if (_definition.stateNames[i] == name) return i;
        }
        _definition.stateNames.push_back(name);
        return static_cast<uint32_t>(_definition.stateNames.size() - 1);
    }
};
