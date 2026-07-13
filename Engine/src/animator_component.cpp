#include "animator_component.hpp"
#include "animation_subsystem.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

AnimatorComponent::AnimatorComponent(GameObject* owner) {
    gameObject = owner;
}

AnimatorComponent::~AnimatorComponent() {
    // OnDetach is the normal unregister path, but guard here too in case a
    // component is destroyed without a matching detach.
    if (_registered) {
        if (auto* sub = AnimationSubsystem::Get()) sub->Unregister(this);
        _registered = false;
    }
}

void AnimatorComponent::OnAttach() {
    if (!_registered) {
        if (auto* sub = AnimationSubsystem::Get()) {
            sub->Register(this);
            _registered = true;
        }
    }
}

void AnimatorComponent::OnDetach() {
    if (_registered) {
        if (auto* sub = AnimationSubsystem::Get()) sub->Unregister(this);
        _registered = false;
    }
}

void AnimatorComponent::Play() {
    _state.playing = true;
}

void AnimatorComponent::Pause() {
    _state.playing = false;
}

void AnimatorComponent::Stop() {
    _state.playing = false;
    _state.time = 0.0f;
    _pingForward = true;
    Evaluate(_state.time);
}

void AnimatorComponent::Seek(float seconds) {
    const float d = GetDuration();
    _state.time = (d > 0.0f) ? std::clamp(seconds, 0.0f, d) : seconds;
    Evaluate(_state.time);
}

void AnimatorComponent::SetSpeed(float speed) {
    _state.speed = speed;
}

void AnimatorComponent::SetWrapMode(WrapMode mode) {
    _state.wrap = mode;
}

void AnimatorComponent::SetGroup(const std::string& group) {
    _state.group = group;
}

void AnimatorComponent::Advance(float scaledDt) {
    if (!_state.playing) return;

    const float dur = GetDuration();
    // Untimed / endless clip: just accumulate and sample, no wrapping.
    if (dur <= 0.0f) {
        _state.time += scaledDt * _state.speed;
        Evaluate(_state.time);
        return;
    }

    float step = scaledDt * _state.speed;
    if (_state.wrap == WrapMode::PingPong && !_pingForward) step = -step;
    _state.time += step;

    switch (_state.wrap) {
    case WrapMode::Once:
        if (_state.time >= dur) {
            _state.time = dur;
            _state.playing = false;
            Evaluate(_state.time);
            if (_onFinished) _onFinished();
            return;
        }
        if (_state.time < 0.0f) {
            _state.time = 0.0f;
            _state.playing = false;
            Evaluate(_state.time);
            if (_onFinished) _onFinished();
            return;
        }
        break;

    case WrapMode::ClampHold:
        if (_state.time >= dur) {
            _state.time = dur;
            const bool was = _state.playing;
            _state.playing = false;
            Evaluate(_state.time);
            if (was && _onFinished) _onFinished();
            return;
        }
        if (_state.time < 0.0f) {
            _state.time = 0.0f;
            const bool was = _state.playing;
            _state.playing = false;
            Evaluate(_state.time);
            if (was && _onFinished) _onFinished();
            return;
        }
        break;

    case WrapMode::Loop:
        if (_state.time >= dur) {
            _state.time = std::fmod(_state.time, dur);
            if (_onLooped) _onLooped();
        } else if (_state.time < 0.0f) {
            _state.time = std::fmod(_state.time, dur) + dur;
            if (_state.time >= dur) _state.time = 0.0f;
            if (_onLooped) _onLooped();
        }
        break;

    case WrapMode::PingPong:
        if (_state.time >= dur) {
            _state.time = dur - (_state.time - dur);// reflect past the end
            _state.time = std::clamp(_state.time, 0.0f, dur);
            _pingForward = false;
            if (_onLooped) _onLooped();
        } else if (_state.time < 0.0f) {
            _state.time = -_state.time;// reflect past the start
            _state.time = std::clamp(_state.time, 0.0f, dur);
            _pingForward = true;
            if (_onLooped) _onLooped();
        }
        break;
    }

    Evaluate(_state.time);
}

void AnimatorComponent::DrawImGui() {
    // Shared transport bar. Subclasses call this then add their own widgets.
    const float dur = GetDuration();
    if (_state.playing) {
        if (ImGui::Button("Pause")) Pause();
    } else {
        if (ImGui::Button("Play")) Play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) Stop();
    ImGui::SameLine();
    ImGui::Text("%.2f / %.2f s", _state.time, dur);

    ImGui::DragFloat("Speed", &_state.speed, 0.01f, -8.0f, 8.0f);

    const char* wraps[] = { "Once", "Loop", "PingPong", "ClampHold" };
    int wrap = static_cast<int>(_state.wrap);
    if (ImGui::Combo("Wrap", &wrap, wraps, IM_ARRAYSIZE(wraps))) {
        _state.wrap = static_cast<WrapMode>(wrap);
    }

    if (dur > 0.0f) {
        float t = _state.time;
        if (ImGui::SliderFloat("Seek", &t, 0.0f, dur)) Seek(t);
    }
}
