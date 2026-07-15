#pragma once
#include "animation_clip.hpp"
#include "animator_component.hpp"
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ActionTimelineComponent — property tweens / timelines
//
// The tween path, replacing the old ActionManager + Action hierarchy. It plays
// ActionTimeline assets (keyframe tracks over transform / color / custom
// properties) sampled as a pure function of the playhead, so seek, reverse,
// ping-pong and scrubbing all work uniformly through the AnimatorComponent base.
//
// Two front doors:
//   • Named library timelines — AddTimeline + Play("name"); what CSB loading
//     produces, re-triggerable.
//   • Overlays — fire-and-forget one-shots layered on top of the main timeline
//     with their own playhead (e.g. a hit-flash while a walk timeline runs).
//     The Tween builder (below) is the ergonomic way to construct these.
// ─────────────────────────────────────────────────────────────────────────────
class ActionTimelineComponent : public AnimatorComponent {
public:
    explicit ActionTimelineComponent(GameObject* owner);

    std::string GetName() const override {
        return "ActionTimeline";
    }

    void DrawImGui() override;

    // ── named timelines ──────────────────────────────────────────────────────
    void AddTimeline(TimelineHandle tl);
    void AddTimeline(const std::string& libraryName);
    bool Play(const std::string& name);// (re)start the named timeline
    using AnimatorComponent::Play;// keep the parameterless resume overload

    const std::string& GetCurrentTimeline() const {
        return _activeName;
    }

    // ── overlays (independent layered playheads) ─────────────────────────────
    // Returns an id usable with CancelOverlay. onFinished fires when the overlay
    // reaches its end (it plays once).
    int PlayOverlay(ActionTimeline tl, std::function<void()> onFinished = {});
    void CancelOverlay(int id);
    void CancelAllOverlays();

    // ── event / custom routing ───────────────────────────────────────────────
    void SetOnEvent(std::function<void(int)> cb) {
        _onEvent = std::move(cb);
    }
    void SetCustomSink(std::function<void(int customId, float value)> cb) {
        _customSink = std::move(cb);
    }

    float GetDuration() const override;

protected:
    void Evaluate(float time) override;

private:
    // Apply every track of `tl` to this component's GameObject at `time`.
    void ApplyTracks(const ActionTimeline& tl, float time);
    // Fire events of `tl` crossed moving forward from `from` to `to`.
    void FireEvents(const ActionTimeline& tl, float from, float to);

    std::vector<TimelineHandle> _timelines;
    TimelineHandle _active;
    const ActionTimeline* _activeClip = nullptr;
    std::string _activeName;
    float _prevTime = 0.0f;// for dt derivation + forward event crossing

    struct Overlay {
        int id = 0;
        ActionTimeline timeline;// owned copy (one-shot, may be anonymous)
        float time = 0.0f;
        std::function<void()> onFinished;
    };
    std::vector<Overlay> _overlays;
    int _nextOverlayId = 1;

    std::function<void(int)> _onEvent;
    std::function<void(int, float)> _customSink;
};

// ─────────────────────────────────────────────────────────────────────────────
// Tween — fluent builder for ActionTimelines
//
//   Tween(go).MoveTo(0.3f, {x,y,0}, EasingType::QuadOut)
//            .Then().ScaleTo(0.15f, {1.2f,1.2f,1}, EasingType::BackOut)
//            .With().FadeTo(0.15f, 0.0f)
//            .Event(kHitFlashDone)
//            .Play();               // → ActionTimelineComponent::PlayOverlay(go)
//
// Relative ("by") helpers resolve against the GameObject's current state at
// build time, so no delta stepping happens at runtime. Segments are sequential
// by default; .With() parallels the next segment with the previous one, .Then()
// is the explicit sequential marker.
// ─────────────────────────────────────────────────────────────────────────────
class Tween {
public:
    explicit Tween(GameObject* go) : _go(go) {
    }

    Tween& MoveTo(float dur, const glm::vec3& pos, EasingType e = EasingType::Linear);
    Tween& MoveBy(float dur, const glm::vec3& delta, EasingType e = EasingType::Linear);
    Tween& RotateTo(float dur, const glm::vec3& rot, EasingType e = EasingType::Linear);// radians
    Tween& ScaleTo(float dur, const glm::vec3& scale, EasingType e = EasingType::Linear);
    Tween& ColorTo(float dur, const glm::vec4& color, EasingType e = EasingType::Linear);
    Tween& FadeTo(float dur, float alpha, EasingType e = EasingType::Linear);
    Tween& CustomTo(float dur, int customId, float value, EasingType e = EasingType::Linear);

    Tween& Then();// next segment starts at the previous segment's end (default)
    Tween& With();// next segment parallels the previous segment
    Tween& Delay(float seconds);// insert a gap before the next segment
    Tween& Event(int eventId);// fire an event at the current cursor
    Tween& Name(const std::string& name);

    ActionTimeline Build();// finalize the timeline asset
    // Register the timeline in the library and play it as an overlay on the
    // GameObject (adds an ActionTimelineComponent if absent). Returns the
    // overlay id, or 0 if it could not be started.
    int Play(std::function<void()> onFinished = {});

private:
    ActionTrack& TrackFor(ActionProperty prop, int customId = 0);
    void AppendSegment(ActionProperty prop, int customId, const glm::vec4& target, float dur, EasingType e);
    glm::vec4 CurrentValue(ActionProperty prop, int customId) const;

    GameObject* _go = nullptr;
    ActionTimeline _timeline;
    float _cursor = 0.0f;// start time for the next appended segment
    float _lastStart = 0.0f;
    float _lastEnd = 0.0f;
};
