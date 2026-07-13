#pragma once
#include "animation_clip.hpp"
#include "animator_component.hpp"
#include <functional>
#include <string>
#include <vector>

class SpriteComponent;
class Sprite3DComponent;

// ─────────────────────────────────────────────────────────────────────────────
// FlipbookComponent — frame-based sprite animation
//
// The 2D (and billboard-sprite) frame-animation path. Replaces the old
// Animator2D, the Animate action, and the RPG example's hand-rolled
// SpriteAnimator with one component on the shared AnimatorComponent base.
// Drives a SpriteComponent's (or Sprite3DComponent's) UVs from a FlipbookClip;
// clips live in the AnimationLibrary and are shared by handle.
//
// Sampling is a pure function of the playhead: Evaluate() maps time to a frame
// via cached prefix sums, writes the sprite UVs only when the frame changes,
// and fires a frame event on entering a frame whose eventId >= 0. Because it is
// pure, wrap modes / reverse / scrubbing all work with no extra code.
// ─────────────────────────────────────────────────────────────────────────────
class FlipbookComponent : public AnimatorComponent {
public:
    explicit FlipbookComponent(GameObject* owner);

    std::string GetName() const override {
        return "Flipbook";
    }

    void OnAttach() override;// caches the sprite target, then registers
    void DrawImGui() override;

    // Register a shared clip from the library, or add a local one (which is
    // pushed into the library and registered here in one call).
    void AddClip(FlipbookClipHandle clip);
    void AddClip(const std::string& libraryName);
    FlipbookClipHandle AddLocalClip(FlipbookClip clip);

    // Switch to a registered clip by name. No-op if that clip is already the
    // current one and playing (idempotent, like SpriteAnimator::play). Returns
    // false if the name isn't among this component's clips.
    bool Play(const std::string& clipName);
    using AnimatorComponent::Play;// keep the parameterless resume overload

    const std::string& GetCurrentClip() const {
        return _currentName;
    }
    int GetCurrentFrame() const {
        return _lastFrame;
    }

    void SetOnFrameEvent(std::function<void(int)> cb) {
        _onFrameEvent = std::move(cb);
    }

    // Mirror horizontally without a separate clip (walk-left = walk-right +
    // flipX), halving authored clip counts.
    void SetFlipX(bool flip);
    bool GetFlipX() const {
        return _flipX;
    }

    float GetDuration() const override {
        return _duration;
    }

protected:
    void Evaluate(float time) override;

private:
    // Resolve the current clip's frames + prefix sums for O(log n) sampling.
    void BindClip(FlipbookClipHandle handle);

    SpriteComponent* _sprite = nullptr;// primary target
    Sprite3DComponent* _sprite3D = nullptr;// fallback target (billboards)

    std::vector<FlipbookClipHandle> _clips;
    FlipbookClipHandle _current;
    std::string _currentName;
    const FlipbookClip* _clip = nullptr;// resolved view of _current
    std::vector<float> _frameEndTimes;// prefix sums; last == _duration
    float _duration = 0.0f;
    int _lastFrame = -1;// change detection for UV writes + events
    bool _flipX = false;

    std::function<void(int)> _onFrameEvent;
};
