#include "flipbook_component.hpp"
#include "animation_subsystem.hpp"
#include "game_object.hpp"
#include "imgui.h"
#include "sprite_3d_component.hpp"
#include "sprite_component.hpp"
#include <algorithm>
#include <utility>

FlipbookComponent::FlipbookComponent(GameObject* owner) : AnimatorComponent(owner) {
    _state.wrap = WrapMode::Loop;
}

void FlipbookComponent::OnAttach() {
    // Resolve the render target once. Prefer a 2D sprite; fall back to a 3D
    // (billboard) sprite, which exposes the same SetUVs interface.
    if (gameObject) {
        _sprite = gameObject->GetComponent<SpriteComponent>();
        if (!_sprite) _sprite3D = gameObject->GetComponent<Sprite3DComponent>();
    }
    AnimatorComponent::OnAttach();
}

static AnimationLibrary* Lib() {
    auto* sub = AnimationSubsystem::Get();
    return sub ? &sub->Library() : nullptr;
}

void FlipbookComponent::AddClip(FlipbookClipHandle clip) {
    if (!clip.IsValid()) return;
    if (std::find(_clips.begin(), _clips.end(), clip) == _clips.end()) _clips.push_back(clip);
    // Bind the first clip added so there is something to show before Play().
    if (!_current.IsValid()) BindClip(clip);
}

void FlipbookComponent::AddClip(const std::string& libraryName) {
    if (auto* lib = Lib()) AddClip(lib->FindFlipbook(libraryName));
}

FlipbookClipHandle FlipbookComponent::AddLocalClip(FlipbookClip clip) {
    auto* lib = Lib();
    if (!lib) return {};
    FlipbookClipHandle h = lib->AddFlipbook(std::move(clip));
    AddClip(h);
    return h;
}

void FlipbookComponent::BindClip(FlipbookClipHandle handle) {
    auto* lib = Lib();
    _current = handle;
    _clip = lib ? lib->GetFlipbook(handle) : nullptr;
    _frameEndTimes.clear();
    _duration = 0.0f;
    _lastFrame = -1;
    _currentName.clear();
    if (!_clip) return;

    _currentName = _clip->name;
    _frameEndTimes.reserve(_clip->frames.size());
    float acc = 0.0f;
    for (const auto& f : _clip->frames) {
        acc += f.duration;
        _frameEndTimes.push_back(acc);
    }
    _duration = acc;
}

bool FlipbookComponent::Play(const std::string& clipName) {
    auto* lib = Lib();
    if (!lib) return false;

    // Already playing this exact clip → idempotent no-op (SpriteAnimator semantics).
    if (_currentName == clipName && _state.playing) return true;

    FlipbookClipHandle target = lib->FindFlipbook(clipName);
    if (!target.IsValid()) return false;
    // Only accept clips this component was given.
    if (std::find(_clips.begin(), _clips.end(), target) == _clips.end()) return false;

    BindClip(target);
    _state.time = 0.0f;
    _state.playing = true;
    Evaluate(_state.time);
    return true;
}

void FlipbookComponent::SetFlipX(bool flip) {
    if (_flipX == flip) return;
    _flipX = flip;
    _lastFrame = -1;// force a UV rewrite on the next evaluate
    Evaluate(_state.time);
}

void FlipbookComponent::Evaluate(float time) {
    if (!_clip || _frameEndTimes.empty()) return;
    if (!_sprite && !_sprite3D) return;

    // First frame whose accumulated end time is strictly greater than `time`.
    auto it = std::upper_bound(_frameEndTimes.begin(), _frameEndTimes.end(), time);
    int frame = static_cast<int>(it - _frameEndTimes.begin());
    frame = std::clamp(frame, 0, static_cast<int>(_clip->frames.size()) - 1);

    if (frame == _lastFrame) return;// no visible change
    _lastFrame = frame;

    const FlipbookFrame& f = _clip->frames[frame];
    glm::vec2 mn = f.uvMin, mx = f.uvMax;
    if (_flipX) std::swap(mn.x, mx.x);
    if (_sprite) _sprite->SetUVs(mn, mx);
    if (_sprite3D) _sprite3D->SetUVs(mn, mx);

    if (f.eventId >= 0 && _onFrameEvent) _onFrameEvent(f.eventId);
}

void FlipbookComponent::DrawImGui() {
    if (!ImGui::CollapsingHeader("Flipbook")) return;
    ImGui::Text("Clip: %s", _currentName.empty() ? "(none)" : _currentName.c_str());
    ImGui::Text("Frame: %d / %zu", _lastFrame, _clip ? _clip->frames.size() : 0);
    bool flip = _flipX;
    if (ImGui::Checkbox("Flip X", &flip)) {
        _flipX = !flip;// undo the checkbox's write so SetFlipX sees a real change
        SetFlipX(flip);
    }
    AnimatorComponent::DrawImGui();
}
