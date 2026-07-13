#pragma once
#include "animation_clip.hpp"
#include "subsystem.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class AnimatorComponent;
class VATClip;

// ─────────────────────────────────────────────────────────────────────────────
// AnimationLibrary — the shared clip store
//
// Owns every animation asset (flipbook strips, action timelines, baked VAT
// clips) so instances can share one copy. Scene-scoped: Clear() is called on
// scene unload, like other scene-tier assets. Handles are stable for the
// lifetime of the library; id 0 is always invalid.
// ─────────────────────────────────────────────────────────────────────────────
class AnimationLibrary {
public:
    FlipbookClipHandle AddFlipbook(FlipbookClip clip);
    TimelineHandle AddTimeline(ActionTimeline timeline);
    VATClipHandle AddVATClip(std::string name, std::unique_ptr<VATClip> clip);

    const FlipbookClip* GetFlipbook(FlipbookClipHandle h) const;
    const ActionTimeline* GetTimeline(TimelineHandle h) const;
    VATClip* GetVATClip(VATClipHandle h) const;

    // By-name lookups (return an invalid handle if absent).
    FlipbookClipHandle FindFlipbook(const std::string& name) const;
    TimelineHandle FindTimeline(const std::string& name) const;
    VATClipHandle FindVATClip(const std::string& name) const;

    void Clear();

private:
    std::vector<FlipbookClip> _flipbooks;// index = handle.id - 1
    std::vector<ActionTimeline> _timelines;
    std::vector<std::unique_ptr<VATClip>> _vatClips;
    std::vector<std::string> _vatNames;// parallel to _vatClips

    std::unordered_map<std::string, uint32_t> _flipbookByName;
    std::unordered_map<std::string, uint32_t> _timelineByName;
    std::unordered_map<std::string, uint32_t> _vatByName;
};

// ─────────────────────────────────────────────────────────────────────────────
// AnimationSubsystem — the single tick point for all animation
//
// Advances every registered AnimatorComponent once per frame with a
// group-scaled dt, and owns the AnimationLibrary. Follows the engine's static
// locator convention (AudioSubsystem-style Get()); owned by Application.
// ─────────────────────────────────────────────────────────────────────────────
class AnimationSubsystem : public Subsystem {
public:
    AnimationSubsystem();
    ~AnimationSubsystem() override;

    // Non-owning locator into the Application-owned instance (see sibling
    // subsystems). Set in the constructor, cleared in the destructor.
    static AnimationSubsystem* Get() {
        return _instance;
    }

    void Init(Application* app) override;
    void Process(float dt) override;// ticks ALL animation
    void DrawImGui(float dt) override;// live table of active players + scales

    AnimationLibrary& Library() {
        return _library;
    }

    // Effective dt for a player = dt * globalScale * groupScale(player.group).
    void SetTimeScale(float scale) {
        _timeScale = scale;
    }
    float GetTimeScale() const {
        return _timeScale;
    }
    void SetGroupTimeScale(const std::string& group, float scale) {
        _groupScales[group] = scale;
    }
    float GetGroupTimeScale(const std::string& group) const;

private:
    friend class AnimatorComponent;
    void Register(AnimatorComponent* player);
    void Unregister(AnimatorComponent* player);// safe during Process

    static AnimationSubsystem* _instance;

    std::vector<AnimatorComponent*> _players;
    AnimationLibrary _library;
    float _timeScale = 1.0f;
    std::unordered_map<std::string, float> _groupScales;

    bool _iterating = false;
    std::vector<AnimatorComponent*> _pendingAdd;
    std::vector<AnimatorComponent*> _pendingRemove;
};
