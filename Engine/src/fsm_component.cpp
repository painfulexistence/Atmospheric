#include "fsm_component.hpp"
#include "imgui.h"

FSMComponent::FSMComponent(GameObject* owner, std::shared_ptr<FSMDefinition> definition)
    : _definition(std::move(definition)) {
    gameObject = owner;
    if (_definition) _state.currentState = _definition->initialState;
}

void FSMComponent::OnAttach() {
    if (_definition) {
        _state.currentState = _definition->initialState;
        _state.stateTime = 0.0f;
        _state.totalTime = 0.0f;
        // Initial "enter" notification, mirroring Vapor's FSMInitSystem which
        // emits a from==to change event for the initial state.
        if (_onStateChange) _onStateChange({ _definition->initialState, _definition->initialState, 0.0f });
    }
}

void FSMComponent::OnTick(float dt) {
    if (!_definition) return;
    if (auto change = FSMStep(*_definition, _state, _pendingEvents, dt)) {
        if (_onStateChange) _onStateChange(*change);
    }
}

void FSMComponent::SetState(uint32_t state) {
    const uint32_t from = _state.currentState;
    const float prevTime = _state.stateTime;
    _state.currentState = state;
    _state.stateTime = 0.0f;
    _pendingEvents.clear();
    if (_onStateChange) _onStateChange({ from, state, prevTime });
}

void FSMComponent::SetState(const std::string& stateName) {
    if (_definition) SetState(_definition->GetStateIndex(stateName));
}

void FSMComponent::DrawImGui() {
    ImGui::Text("State: %s (%.2fs)", GetStateName().c_str(), _state.stateTime);
    if (_definition) {
        ImGui::Text(
            "%zu states, %zu event rules, %zu timed",
            _definition->stateNames.size(),
            _definition->eventTransitions.size(),
            _definition->timedTransitions.size()
        );
    }
    if (!_pendingEvents.empty()) ImGui::Text("Pending events: %zu", _pendingEvents.size());
}
