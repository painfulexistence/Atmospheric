#pragma once
#include "component.hpp"
#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// AnimatorComponent — shared playback driver (the "operator")
//
// The common base for every animation component: ActionTimelineComponent,
// FlipbookComponent, and VATComponent. It owns the playhead state machine
// (play/pause/stop/seek/speed/wrap + finished/looped events) and nothing else —
// it knows nothing about 2D vs. 3D, sprites vs. meshes. Subclasses implement
// Evaluate(time) to *sample* their clip at the (already advanced and wrapped)
// playhead; they never advance time themselves.
//
// Ticking is centralized: an AnimatorComponent does NOT tick itself
// (CanTick() == false). AnimationSubsystem::Process advances every registered
// player once per frame with a group-scaled dt, mirroring how CanvasDrawables
// are driven by the batch renderer rather than by their own OnTick.
// ─────────────────────────────────────────────────────────────────────────────

enum class WrapMode {
    Once,// play to the end, stop, fire OnFinished
    Loop,// wrap to the start each cycle, fire OnLooped
    PingPong,// reverse direction at each end, fire OnLooped at each turn
    ClampHold,// like Once, but keeps evaluating the final frame (VAT "hold")
};

struct PlaybackState {
    float time = 0.0f;// seconds into the clip (already wrapped into [0, duration])
    float speed = 1.0f;// negative plays backwards
    WrapMode wrap = WrapMode::Loop;
    bool playing = false;
    std::string group;// time-scale group ("" = default group)
};

class AnimationSubsystem;

class AnimatorComponent : public Component {
public:
    explicit AnimatorComponent(GameObject* owner);
    ~AnimatorComponent() override;

    void OnAttach() override;// registers with AnimationSubsystem
    void OnDetach() override;// unregisters
    bool CanTick() const override {
        return false;// the subsystem drives us, not the entity tick
    }

    // ── uniform playback API ─────────────────────────────────────────────────
    void Play();// resume from the current playhead
    void Pause();
    void Stop();// pause and rewind to 0 (samples frame 0)
    void Seek(float seconds);// set the playhead and re-sample immediately
    void SetSpeed(float speed);
    void SetWrapMode(WrapMode mode);
    void SetGroup(const std::string& group);

    bool IsPlaying() const {
        return _state.playing;
    }
    float GetSpeed() const {
        return _state.speed;
    }
    WrapMode GetWrapMode() const {
        return _state.wrap;
    }
    const std::string& GetGroup() const {
        return _state.group;
    }
    float GetTime() const {
        return _state.time;
    }
    float GetNormalizedTime() const {
        const float d = GetDuration();
        return d > 0.0f ? _state.time / d : 0.0f;
    }

    // Length of the current clip in seconds; 0 means untimed/endless (the base
    // then samples the raw playhead without wrapping).
    virtual float GetDuration() const = 0;

    // ── events ───────────────────────────────────────────────────────────────
    void SetOnFinished(std::function<void()> cb) {
        _onFinished = std::move(cb);
    }
    void SetOnLooped(std::function<void()> cb) {
        _onLooped = std::move(cb);
    }

    void DrawImGui() override;// shared transport bar; subclasses extend

protected:
    // Sample the clip at `time` (seconds, already wrapped by the base). MUST be
    // a pure function of `time` wherever possible — that is what makes seek,
    // reverse and scrubbing work uniformly. Must tolerate being called before
    // the subclass has resolved its target (guard on null).
    virtual void Evaluate(float time) = 0;

    PlaybackState _state;

private:
    friend class AnimationSubsystem;
    // Advance the playhead by an already-scaled dt, apply the wrap mode (firing
    // events), and Evaluate. Called only by AnimationSubsystem::Process.
    void Advance(float scaledDt);

    std::function<void()> _onFinished;
    std::function<void()> _onLooped;
    bool _pingForward = true;// PingPong direction
    bool _registered = false;
};
