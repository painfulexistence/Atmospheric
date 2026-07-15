#include "action_timeline_component.hpp"
#include "animation_subsystem.hpp"
#include "game_object.hpp"
#include "imgui.h"
#include "sprite_3d_component.hpp"
#include "sprite_component.hpp"
#include "text_2d_component.hpp"
#include "text_3d_component.hpp"
#include <algorithm>

// ── colour get/set across the drawable component types ───────────────────────
namespace {
    bool GetColor(GameObject* go, glm::vec4& out) {
        if (auto* s = go->GetComponent<SpriteComponent>()) {
            out = s->GetColor();
            return true;
        }
        if (auto* t = go->GetComponent<Text2DComponent>()) {
            out = t->GetColor();
            return true;
        }
        if (auto* t = go->GetComponent<Text3DComponent>()) {
            out = t->GetColor();
            return true;
        }
        if (auto* s = go->GetComponent<Sprite3DComponent>()) {
            out = s->GetColor();
            return true;
        }
        return false;
    }
    void SetColor(GameObject* go, const glm::vec4& c) {
        if (auto* s = go->GetComponent<SpriteComponent>()) {
            s->SetColor(c);
            return;
        }
        if (auto* t = go->GetComponent<Text2DComponent>()) {
            t->SetColor(c);
            return;
        }
        if (auto* t = go->GetComponent<Text3DComponent>()) {
            t->SetColor(c);
            return;
        }
        if (auto* s = go->GetComponent<Sprite3DComponent>()) {
            s->SetColor(c);
            return;
        }
    }
}// namespace

// ── ActionTimelineComponent ──────────────────────────────────────────────────

ActionTimelineComponent::ActionTimelineComponent(GameObject* owner) : AnimatorComponent(owner) {
    _state.wrap = WrapMode::Once;
}

static AnimationLibrary* Lib() {
    auto* sub = AnimationSubsystem::Get();
    return sub ? &sub->Library() : nullptr;
}

void ActionTimelineComponent::AddTimeline(TimelineHandle tl) {
    if (tl.IsValid() && std::find(_timelines.begin(), _timelines.end(), tl) == _timelines.end())
        _timelines.push_back(tl);
}

void ActionTimelineComponent::AddTimeline(const std::string& libraryName) {
    if (auto* lib = Lib()) AddTimeline(lib->FindTimeline(libraryName));
}

bool ActionTimelineComponent::Play(const std::string& name) {
    auto* lib = Lib();
    if (!lib) return false;

    // Resolve among THIS component's own timelines. Timeline names collide
    // across entities (every CSB node's timeline may share a name — the default
    // GameObject name is even the same " "), so a global by-name lookup would
    // return another entity's handle and this component would fail to play.
    for (TimelineHandle h : _timelines) {
        const ActionTimeline* tl = lib->GetTimeline(h);
        if (tl && tl->name == name) {
            _active = h;
            _activeClip = tl;
            _activeName = tl->name;
            _state.time = 0.0f;
            _prevTime = 0.0f;
            _state.playing = true;
            Evaluate(0.0f);
            return true;
        }
    }
    return false;
}

int ActionTimelineComponent::PlayOverlay(ActionTimeline tl, std::function<void()> onFinished) {
    tl.Recompute();
    Overlay ov;
    ov.id = _nextOverlayId++;
    ov.timeline = std::move(tl);
    ov.time = 0.0f;
    ov.onFinished = std::move(onFinished);
    _overlays.push_back(std::move(ov));
    _state.playing = true;// ensure the subsystem ticks us so overlays advance
    return _overlays.back().id;
}

void ActionTimelineComponent::CancelOverlay(int id) {
    _overlays.erase(
        std::remove_if(_overlays.begin(), _overlays.end(), [id](const Overlay& o) { return o.id == id; }),
        _overlays.end()
    );
}

void ActionTimelineComponent::CancelAllOverlays() {
    _overlays.clear();
}

float ActionTimelineComponent::GetDuration() const {
    return _activeClip ? _activeClip->duration : 0.0f;
}

void ActionTimelineComponent::ApplyTracks(const ActionTimeline& tl, float time) {
    if (!gameObject) return;
    for (const auto& track : tl.tracks) {
        const glm::vec4 v = track.Sample(time);
        switch (track.property) {
        case ActionProperty::Position:
            gameObject->SetPosition(glm::vec3(v.x, v.y, v.z));
            break;
        case ActionProperty::Rotation:
            gameObject->SetRotation(glm::vec3(v.x, v.y, v.z));
            break;
        case ActionProperty::Scale:
            gameObject->SetScale(glm::vec3(v.x, v.y, v.z));
            break;
        case ActionProperty::Color:
            SetColor(gameObject, v);
            break;
        case ActionProperty::Alpha: {
            glm::vec4 c(1.0f);
            GetColor(gameObject, c);
            c.a = v.x;
            SetColor(gameObject, c);
            break;
        }
        case ActionProperty::Custom:
            if (_customSink) _customSink(track.customId, v.x);
            break;
        }
    }
}

void ActionTimelineComponent::FireEvents(const ActionTimeline& tl, float from, float to) {
    if (!_onEvent || tl.events.empty()) return;
    auto fireRange = [&](float lo, float hi) {
        for (const auto& e : tl.events)
            if (e.time > lo && e.time <= hi) _onEvent(e.eventId);
    };
    if (to >= from) {
        fireRange(from, to);
    } else {
        // wrapped: [from, duration] then (−, to]
        fireRange(from, tl.duration);
        fireRange(-1.0f, to);
    }
}

void ActionTimelineComponent::Evaluate(float time) {
    // Main timeline.
    if (_activeClip) {
        ApplyTracks(*_activeClip, time);
        FireEvents(*_activeClip, _prevTime, time);
    }

    // Overlays: advance by the main playhead's forward delta, layer on top.
    float dt = time - _prevTime;
    if (dt < 0.0f) dt = 0.0f;// main timeline wrapped this frame
    if (!_overlays.empty()) {
        for (auto& ov : _overlays) {
            const float prev = ov.time;
            ov.time += dt;
            ApplyTracks(ov.timeline, ov.time);
            FireEvents(ov.timeline, prev, ov.time);
        }
        // Retire finished overlays (they play once).
        for (auto it = _overlays.begin(); it != _overlays.end();) {
            if (it->time >= it->timeline.duration) {
                auto cb = std::move(it->onFinished);
                it = _overlays.erase(it);
                if (cb) cb();
            } else {
                ++it;
            }
        }
    }

    _prevTime = time;

    // Nothing left to drive → let the subsystem stop ticking us.
    if (!_activeClip && _overlays.empty()) _state.playing = false;
}

void ActionTimelineComponent::DrawImGui() {
    // The editor already wraps each component in a CollapsingHeader(GetName())
    // with a PushID — so draw widgets directly, no extra header here.
    ImGui::Text("Timeline: %s", _activeName.empty() ? "(none)" : _activeName.c_str());
    ImGui::Text("Overlays: %zu", _overlays.size());
    AnimatorComponent::DrawImGui();
}

// ── Tween builder ────────────────────────────────────────────────────────────

glm::vec4 Tween::CurrentValue(ActionProperty prop, int) const {
    if (!_go) return glm::vec4(0.0f);
    switch (prop) {
    case ActionProperty::Position: {
        glm::vec3 p = _go->GetPosition();
        return glm::vec4(p.x, p.y, p.z, 0.0f);
    }
    case ActionProperty::Rotation: {
        glm::vec3 r = _go->GetRotation();
        return glm::vec4(r.x, r.y, r.z, 0.0f);
    }
    case ActionProperty::Scale: {
        glm::vec3 s = _go->GetScale();
        return glm::vec4(s.x, s.y, s.z, 0.0f);
    }
    case ActionProperty::Color: {
        glm::vec4 c(1.0f);
        GetColor(_go, c);
        return c;
    }
    case ActionProperty::Alpha: {
        glm::vec4 c(1.0f);
        GetColor(_go, c);
        return glm::vec4(c.a, 0.0f, 0.0f, 0.0f);
    }
    case ActionProperty::Custom:
    default:
        return glm::vec4(0.0f);
    }
}

ActionTrack& Tween::TrackFor(ActionProperty prop, int customId) {
    for (auto& t : _timeline.tracks)
        if (t.property == prop && t.customId == customId) return t;
    _timeline.tracks.push_back(ActionTrack{ prop, customId, {} });
    return _timeline.tracks.back();
}

void Tween::AppendSegment(ActionProperty prop, int customId, const glm::vec4& target, float dur, EasingType e) {
    ActionTrack& track = TrackFor(prop, customId);
    const float start = _cursor;
    const float end = _cursor + dur;
    // Seed a start key at build-time value the first time this track is touched.
    if (track.keys.empty()) track.keys.push_back(ActionKey{ start, CurrentValue(prop, customId), EasingType::Linear });
    track.keys.push_back(ActionKey{ end, target, e });
    _lastStart = start;
    _lastEnd = end;
    _cursor = end;// sequential by default
}

Tween& Tween::MoveTo(float dur, const glm::vec3& pos, EasingType e) {
    AppendSegment(ActionProperty::Position, 0, glm::vec4(pos.x, pos.y, pos.z, 0.0f), dur, e);
    return *this;
}
Tween& Tween::MoveBy(float dur, const glm::vec3& delta, EasingType e) {
    glm::vec3 cur = _go ? _go->GetPosition() : glm::vec3(0.0f);
    return MoveTo(dur, cur + delta, e);
}
Tween& Tween::RotateTo(float dur, const glm::vec3& rot, EasingType e) {
    AppendSegment(ActionProperty::Rotation, 0, glm::vec4(rot.x, rot.y, rot.z, 0.0f), dur, e);
    return *this;
}
Tween& Tween::ScaleTo(float dur, const glm::vec3& scale, EasingType e) {
    AppendSegment(ActionProperty::Scale, 0, glm::vec4(scale.x, scale.y, scale.z, 0.0f), dur, e);
    return *this;
}
Tween& Tween::ColorTo(float dur, const glm::vec4& color, EasingType e) {
    AppendSegment(ActionProperty::Color, 0, color, dur, e);
    return *this;
}
Tween& Tween::FadeTo(float dur, float alpha, EasingType e) {
    AppendSegment(ActionProperty::Alpha, 0, glm::vec4(alpha, 0.0f, 0.0f, 0.0f), dur, e);
    return *this;
}
Tween& Tween::CustomTo(float dur, int customId, float value, EasingType e) {
    AppendSegment(ActionProperty::Custom, customId, glm::vec4(value, 0.0f, 0.0f, 0.0f), dur, e);
    return *this;
}

Tween& Tween::Then() {
    _cursor = _lastEnd;
    return *this;
}
Tween& Tween::With() {
    _cursor = _lastStart;
    return *this;
}
Tween& Tween::Delay(float seconds) {
    _cursor += seconds;
    return *this;
}
Tween& Tween::Event(int eventId) {
    _timeline.events.push_back(TimelineEvent{ _cursor, eventId });
    return *this;
}
Tween& Tween::Name(const std::string& name) {
    _timeline.name = name;
    return *this;
}

ActionTimeline Tween::Build() {
    std::sort(_timeline.events.begin(), _timeline.events.end(), [](const TimelineEvent& a, const TimelineEvent& b) {
        return a.time < b.time;
    });
    _timeline.Recompute();
    return _timeline;
}

int Tween::Play(std::function<void()> onFinished) {
    if (!_go) return 0;
    auto* atc = _go->GetComponent<ActionTimelineComponent>();
    if (!atc) atc = static_cast<ActionTimelineComponent*>(_go->AddComponent<ActionTimelineComponent>());
    return atc->PlayOverlay(Build(), std::move(onFinished));
}
