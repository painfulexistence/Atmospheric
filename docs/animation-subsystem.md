# Animation Subsystem — Design

Status: **proposal v2** (design only, no code yet)

This document proposes a unified `AnimationSubsystem` that consolidates every
piece of animation code currently in the engine — property tweens
(`ActionManager` / `Action`), frame-based sprite animation (`Animator2D`, the
`Animate` action, and the RPG example's ad-hoc `SpriteAnimator`), and GPU
vertex animation (`VATClip` / `VATComponent`) — behind one subsystem, one
shared clip library, and a small family of components with a common playback
interface.

**v2 note:** backward compatibility is explicitly *not* a requirement. This
version deletes the Cocos-style `Action` hierarchy instead of wrapping it,
which makes the whole system evaluate-by-time (scrubbable, reversible,
deterministic) with no imperative escape hatch.

---

## 1. Current state (what we are replacing)

| Code | Kind | Fate |
|------|------|------|
| `Action` / `ActionInterval` / `ActionManager` (`action.hpp`, `action_manager.cpp`) | Cocos-style property tweens (MoveTo, ScaleTo, Sequence, RepeatForever…); stateful, delta-stepped | **Deleted.** Replaced by keyframe `ActionTimeline` + a builder API. Easing functions survive (moved to `easing.hpp`) |
| `Animator2D` (`animator_2d.hpp`) | Frame-based (UV) sprite animation | **Deleted.** Replaced by `FlipbookComponent` |
| `Animate` action (`action.hpp`) | Frame-based sprite animation as an Action | **Deleted** with the Action hierarchy |
| `SpriteAnimator` (Examples/RPG/`rpg_entity.hpp`) | Third frame-animation implementation, engine-free | **Ported** to `FlipbookComponent` (its idempotent `play()` and col/row grid addressing are absorbed into the engine API) |
| `VATClip` / `VATComponent` (`vat.hpp`, `vat_component.cpp`) | GPU vertex animation | **Kept**, refactored: clip ownership moves to the library, playhead logic moves to the shared base |

Why delete `Action` rather than wrap it: actions are single-use mutable
objects driven by `Step(dt)` — they cannot be seeked, scrubbed, played
backwards, or re-triggered without re-instantiation, and `MoveBy`-style
deltas accumulate float error over loops. Every one of those problems
disappears if the tween representation is keyframes evaluated as a pure
function of time. CSB scene data is *already* keyframes; the current loader
converts keyframes → actions, losing the natural representation on the way.

Cross-cutting gaps being fixed:

- **No central tick** — no global/group time scale, no "what is animating
  right now" view.
- **No shared clip storage** — three ad-hoc clip types with three ownership
  stories; VAT bakes one texture set *per component instance*.
- **No events** — nothing fires "finished", "looped", or per-frame events.

---

## 2. Goals / non-goals

Goals

1. One subsystem (`AnimationSubsystem`) that ticks all animation, owns global
   and per-group time scales, and exposes a debug view of active players.
2. One clip library: named, shared animation assets (`FlipbookClip`,
   `ActionTimeline`, `VATClip`).
3. Three focused components on a common playback base: `ActionTimelineComponent`,
   `FlipbookComponent`, `VATComponent`.
4. **Everything is evaluate-by-time.** Every player samples state as a pure
   function of the playhead. Seek, scrub, reverse, ping-pong, fixed-tick
   determinism (headless/netcode) work uniformly, by construction.
5. Unified playback semantics — play/pause/stop/speed/wrap and events behave
   identically across all three.

Non-goals (for this iteration)

- Skeletal animation / blend trees (the design leaves an obvious slot).
- VAT crossfading (needs a second texture binding + blend weight in the
  shader). Clip *switching* is in scope.
- A timeline editor. Editor integration = the existing `DrawImGui` pattern.

---

## 3. Architecture overview

```
                       AnimationSubsystem  (engine service, static Get())
                       ┌──────────────────────────────────────────────┐
                       │ • Process(dt): ticks every registered player │
                       │ • time scales: global + named groups         │
                       │ • AnimationLibrary: named shared clips       │
                       │ • DrawImGui(): live list of active players   │
                       └──────┬───────────────┬───────────────┬───────┘
                    register/unregister (OnAttach/OnDetach)
                              │               │               │
              ┌───────────────┴───┐   ┌───────┴────────┐   ┌──┴────────────┐
              │ ActionTimeline-   │   │ Flipbook-      │   │ VATComponent  │
              │ Component         │   │ Component      │   │ (GPU vertex   │
              │ (keyframe tracks  │   │ (frame-based,  │   │  animation)   │
              │  on transform /   │   │  drives Sprite │   │               │
              │  color / custom)  │   │  UVs)          │   │               │
              └───────────────────┘   └────────────────┘   └───────────────┘
                        │                     │                    │
                  ActionTimeline         FlipbookClip            VATClip
                  (shared asset)        (shared asset)        (shared asset)
                              all stored in AnimationLibrary
```

All three components derive from `AnimationComponent`, which owns the playback
state machine. The subsystem iterates registered components once per frame;
the components' own `OnTick` is disabled (`CanTick() == false`), mirroring how
`CanvasDrawable`s are drawn by the batch renderer rather than ticking
themselves.

### What is shared vs. what stays separate

Unification here does **not** mean 2D and 3D animation evaluate through one
code path. The three players share only the parts that are genuinely
dimension-agnostic — timekeeping and asset storage. Sampling and the write
target stay per-kind:

| Layer | Shared? | Detail |
|-------|---------|--------|
| Playhead state machine (play/pause/speed/seek/WrapMode, finished/looped events) | ✅ one implementation | `AnimationComponent` base; pure time arithmetic, knows nothing about 2D/3D |
| Ticking + time scales | ✅ one implementation | `AnimationSubsystem::Process`; global + group scales |
| Clip storage | ✅ one registry | `AnimationLibrary` (named handles); clip *types* stay distinct |
| Sampling (`Evaluate(t)`) | ❌ per component | keyframe interpolation vs. frame lookup vs. normalized-time passthrough |
| Write target | ❌ per component | transform/color (2D & 3D) vs. sprite UVs (2D) vs. `VATMaterial` playhead (3D GPU) |

So there are exactly **three concrete components** (plus the abstract base,
which is not attachable by itself):

1. `ActionTimelineComponent` — property tweens/timelines; works on any
   GameObject, 2D or 3D, because transform and color are shared concepts.
2. `FlipbookComponent` — the 2D (and billboard-sprite) frame-animation path.
   This *is* `Animator2D`, renamed and rebuilt on the base — 2D animation is
   not being removed, its component is being replaced 1:1 (the rename exists
   because "flipbook" says what it does, and because the engine currently has
   three copies of this logic to collapse into one).
3. `VATComponent` — the 3D GPU vertex-animation path.

### Naming

This design keeps the engine's established "Action" vocabulary for the tween
system: the asset is `ActionTimeline`, its tracks/keys are `ActionTrack` /
`ActionKey`, and the component is `ActionTimelineComponent`. One deliberate
exception: the bare name `Action` is **retired**, not reused. In the old
system an `Action` was an executable behavior object (`Step(dt)`, lifecycle);
a keyframe is a passive data point. Reusing the old name for a semantically
different thing would mislead anyone (or any old sample code) carrying the
Cocos-era meaning, so keys are `ActionKey` — same family prefix, no false
continuity.

### File layout

```
Engine/include/Atmospheric/
    easing.hpp                NEW  EasingType + ApplyEasing (moved from action.hpp)
    animation_clip.hpp        NEW  FlipbookClip, ActionTimeline (+ tracks/keys)
    animation_component.hpp   NEW  AnimationComponent base + PlaybackState
    animation_subsystem.hpp   NEW  AnimationSubsystem + AnimationLibrary
    action_timeline_component.hpp       NEW  ActionTimelineComponent + Tween builder
    flipbook_component.hpp    NEW
    vat.hpp                   KEEP (VATClip unchanged; stored in library)
    vat_component.hpp         CHANGED derives AnimationComponent

    action.hpp                DELETED (with action.cpp)
    action_manager.hpp        DELETED (with action_manager.cpp)
    animator_2d.hpp           DELETED (with animator_2d.cpp)
```

---

## 4. Core types

### 4.1 Playback state (shared semantics)

Every player reduces to the same state machine, written once in the base:

```cpp
// animation_component.hpp
enum class WrapMode {
    Once,       // play to the end, stop, fire OnFinished
    Loop,       // wrap, fire OnLooped each wrap
    PingPong,   // reverse direction at each end
    ClampHold,  // like Once but keeps evaluating the last frame (VAT "hold")
};

struct PlaybackState {
    float time = 0.0f;       // seconds into the clip (not normalized)
    float speed = 1.0f;      // negative plays backwards
    WrapMode wrap = WrapMode::Loop;
    bool playing = false;
    std::string group;       // time-scale group, "" = default
};

class AnimationComponent : public Component {
public:
    explicit AnimationComponent(GameObject* owner);
    ~AnimationComponent() override;          // unregisters from the subsystem

    void OnAttach() override;                // registers with AnimationSubsystem
    void OnDetach() override;                // unregisters
    bool CanTick() const override { return false; }  // subsystem drives us

    // ── uniform playback API ─────────────────────────────────────────
    void Play();                             // resume from current time
    void Pause();
    void Stop();                             // pause + rewind to 0
    void Seek(float seconds);
    void SetSpeed(float speed);
    void SetWrapMode(WrapMode m);
    void SetGroup(const std::string& group);
    bool IsPlaying() const;
    float GetTime() const;
    float GetNormalizedTime() const;         // time / Duration(), 0 if len==0
    virtual float GetDuration() const = 0;   // seconds; 0 = untimed/endless

    // ── events ───────────────────────────────────────────────────────
    void SetOnFinished(std::function<void()> cb);   // Once/ClampHold reached end
    void SetOnLooped(std::function<void()> cb);     // Loop/PingPong wrapped

    // Editor inspector: transport bar (play/pause, speed, seek slider)
    // + whatever Evaluate-side widgets the subclass adds.
    void DrawImGui() override;

protected:
    // Called by AnimationSubsystem::Process with group-scaled dt already
    // applied and `time` already advanced/wrapped. Subclasses only *sample*:
    // Evaluate MUST be a pure function of `time`. This invariant is what
    // makes seek/scrub, reverse, ping-pong, and deterministic replay free.
    virtual void Evaluate(float time) = 0;

    PlaybackState _state;
    friend class AnimationSubsystem;
};
```

### 4.2 Clips (shared assets) and the library

#### FlipbookClip

```cpp
// animation_clip.hpp
struct FlipbookFrame {
    glm::vec2 uvMin, uvMax;
    float duration;              // seconds
    int eventId = -1;            // ≥0 → fire frame event on entering this frame
};

struct FlipbookClip {
    std::string name;
    std::vector<FlipbookFrame> frames;
    float GetDuration() const;   // sum of frame durations (cached prefix sums)

    // Builders (absorb Animator2D::CreateAnimationFromTileset and the RPG
    // example's col/row pattern):
    static FlipbookClip FromTileset(std::string name, glm::vec2 tilesetSize,
                                    const std::vector<int>& tileIndices,
                                    float frameDuration);
    static FlipbookClip FromGrid(std::string name, int cols, int rows,
                                 const std::vector<std::pair<int,int>>& cells,
                                 float frameDuration);
};
```

#### ActionTimeline (replaces the Action hierarchy)

A timeline is a set of typed keyframe tracks over a fixed duration. Sampling
is a pure function of `t`; there is no per-playback mutable state to
instantiate, so one timeline asset serves any number of simultaneous players.

```cpp
enum class ActionProperty {
    Position,      // vec3, absolute (world of the CSB node / local transform)
    Rotation,      // vec3 radians
    Scale,         // vec3
    Color,         // vec4 → SpriteComponent / material tint
    Alpha,         // float → color.a only
    Custom,        // float, delivered via callback (drive anything)
};

struct ActionKey {
    float time;                  // seconds from timeline start
    glm::vec4 value;             // interpreted per property (float in .x)
    EasingType easing = EasingType::Linear;  // ease-in *to this key*
};

struct ActionTrack {
    ActionProperty property;
    int customId = 0;            // routes Custom tracks
    std::vector<ActionKey> keys;  // sorted by time
    glm::vec4 Sample(float t) const;  // binary search + eased lerp; clamps at ends
};

struct TimelineEvent { float time; int eventId; };

struct ActionTimeline {
    std::string name;
    float duration = 0.0f;       // max key time (cached)
    std::vector<ActionTrack> tracks;
    std::vector<TimelineEvent> events;  // fired when playhead crosses them
};
```

Notes on what the Action zoo maps to:

- `MoveTo/RotateTo/ScaleTo/ColorTo/FadeTo` → one track with two keys.
- `MoveBy` and friends → resolved to absolute keys at *build* time by the
  `Tween` builder (§4.4), which reads the current value. No delta stepping
  at runtime → no accumulated drift across loops.
- `Sequence` → keys laid out one after another on the same track;
  cross-property sequences are just multiple tracks with staggered keys.
- `Spawn` (parallel) → multiple tracks, same time span.
- `RepeatForever` → `WrapMode::Loop` on the *player*, not in the asset.
- `CallFunc` → a `TimelineEvent`; the component dispatches by `eventId`.
- `DelayTime` → a gap between keys.

Event semantics under scrubbing: events fire on *forward* crossings during
normal playback. `Seek` does not fire skipped events (documented; editor
scrubbing should not trigger gameplay callbacks).

#### AnimationLibrary

Scene-scoped (cleared by `UnloadCurrentScene`, like scene-tier assets in
`AssetManager`):

```cpp
class AnimationLibrary {
public:
    FlipbookClipHandle AddFlipbook(FlipbookClip clip);
    TimelineHandle     AddTimeline(ActionTimeline tl);
    VATClipHandle      AddVATClip(std::string name, std::unique_ptr<VATClip> clip);

    const FlipbookClip*  GetFlipbook(FlipbookClipHandle) const;  // + by-name
    const ActionTimeline* GetTimeline(TimelineHandle) const;
    VATClip*             GetVATClip(VATClipHandle) const;

    void Clear();                // scene unload
};
```

Moving `VATClip` ownership here fixes the "one baked texture set per
component" cost: ten VAT enemies of the same type share one clip. (The
`VATMaterial` per instance stays — it carries the per-instance playhead.)

### 4.3 AnimationSubsystem

Follows the established service pattern (`AudioSubsystem`-style static
locator; owned by `Application`, constructed after graphics, before
scripting):

```cpp
class AnimationSubsystem : public Subsystem {
public:
    static AnimationSubsystem* Get();

    void Init(Application* app) override;
    void Process(float dt) override;         // single tick point for ALL animation
    void DrawImGui(float dt) override;       // table of active players, live scrub

    AnimationLibrary& Library();

    // Time scaling. Effective dt for a player =
    //   dt * globalScale * groupScale(player.group)
    void SetTimeScale(float scale);                       // global
    void SetGroupTimeScale(const std::string& group, float scale);
    float GetTimeScale() const;
    float GetGroupTimeScale(const std::string& group) const;

private:
    friend class AnimationComponent;
    void Register(AnimationComponent*);
    void Unregister(AnimationComponent*);    // safe during Process (deferred erase)

    std::vector<AnimationComponent*> _players;
    AnimationLibrary _library;
    float _timeScale = 1.0f;
    std::unordered_map<std::string, float> _groupScales;
};
```

`Process(dt)` per player: skip if `!playing || !gameObject->isActive ||
!enabled`; compute scaled dt; advance `_state.time`; apply wrap mode (firing
`OnLooped`/`OnFinished`); call `Evaluate(time)`. Registration changes during
the walk are deferred to the end of the pass.

Tick order: `Process` runs after entity `Tick` (game logic decides *what* to
play) and before rendering/transform sync (so sampled poses are what gets
drawn).

### 4.4 ActionTimelineComponent

One component, two front doors onto the same evaluation path:

- **Named timelines** from the library — `Play("open_menu")`, re-triggerable,
  what CSB loading produces.
- **The `Tween` builder** — for code-driven one-shots ("knock this enemy back
  0.3s"), building a small anonymous `ActionTimeline` inline and playing it.
  This replaces `RunAction(new Sequence{...})` ergonomics.

```cpp
class ActionTimelineComponent : public AnimationComponent {
public:
    explicit ActionTimelineComponent(GameObject* owner);
    std::string GetName() const override { return "ActionTimeline"; }

    void AddTimeline(TimelineHandle tl);                 // from the library
    void AddTimeline(const std::string& libraryName);
    bool Play(const std::string& name);                  // restarts if playing
    using AnimationComponent::Play;                      // resume current

    // Fire-and-forget overlay tweens: independent playheads layered on top of
    // the main timeline (e.g. a hit-flash while the walk timeline runs).
    // Returns an id usable with CancelOverlay.
    int PlayOverlay(ActionTimeline tl, std::function<void()> onFinished = {});
    void CancelOverlay(int id);
    void CancelAllOverlays();

    void SetOnEvent(std::function<void(int eventId)> cb);   // TimelineEvents
    void SetCustomSink(std::function<void(int customId, float value)> cb);

    float GetDuration() const override;      // active timeline's duration

protected:
    void Evaluate(float time) override;      // sample active timeline tracks
                                             // (+ step overlay playheads)
private:
    std::vector<TimelineHandle> _timelines;
    TimelineHandle _active;
    struct Overlay { int id; ActionTimeline tl; float time; /*…*/ };
    std::vector<Overlay> _overlays;          // advanced with the same scaled dt
};
```

The builder:

```cpp
// Fluent construction of a ActionTimeline. `To` targets resolve any relative
// ("by") values against the GameObject's current state at *build* time.
Tween(go)
    .MoveTo(0.3f, {x, y, 0}, EasingType::QuadOut)
    .Then()                                  // advance the cursor (sequence)
    .ScaleTo(0.15f, {1.2f, 1.2f, 1}, EasingType::BackOut)
    .With()                                  // stay at cursor (parallel)
    .FadeTo(0.15f, 0.0f)
    .Event(kHitFlashDone)
    .Play();          // → ActionTimelineComponent::PlayOverlay on go (auto-adds the
                      //   component if missing)
// .Build() instead of .Play() returns the ActionTimeline for the library.
```

Callbacks: where old code interleaved `CallFunc` inside a `Sequence`, new code
places `.Event(id)` markers and handles them in one `SetOnEvent` callback (or
passes `onFinished` for the common "do X when done" case). This keeps the
asset data-only — serializable, shareable, scrub-safe.

### 4.5 FlipbookComponent

```cpp
class FlipbookComponent : public AnimationComponent {
public:
    explicit FlipbookComponent(GameObject* owner);
    std::string GetName() const override { return "Flipbook"; }

    void AddClip(FlipbookClipHandle clip);
    void AddClip(const std::string& libraryName);
    FlipbookClipHandle AddLocalClip(FlipbookClip clip);  // adds to library
                                                         // + registers here

    bool Play(const std::string& clipName);  // no-op if already playing it
                                             // (SpriteAnimator's idempotent play)
    const std::string& GetCurrentClip() const;
    int GetCurrentFrame() const;

    // Frame events: fired when playback enters a frame with eventId >= 0.
    void SetOnFrameEvent(std::function<void(int eventId)> cb);

    // Per-direction flip without extra clips (walk-left = walk-right + flipX).
    void SetFlipX(bool flip);

    float GetDuration() const override;

protected:
    void OnAttach() override;                // caches SpriteComponent*
    void Evaluate(float time) override;      // binary-search frame, set UVs once

private:
    SpriteComponent* _sprite = nullptr;      // falls back to Sprite3DComponent
    std::vector<FlipbookClipHandle> _clips;
    FlipbookClipHandle _current;
    int _lastFrame = -1;                     // only touch the sprite (and fire
                                             // events) on frame transitions
    std::vector<float> _frameEndTimes;       // prefix sums, O(log n) sampling
    std::function<void(int)> _onFrameEvent;
};
```

`Evaluate(t)` maps `t` to a frame via the prefix sums, writes `SetUVs` only
when the frame index changes, and fires the entered frame's event. Pure
sampling → wrap modes, negative speed, and scrubbing need zero extra code.

### 4.6 VATComponent (refactored onto the base)

```cpp
struct VATProps {
    WrapMode wrap = WrapMode::Loop;
    float speed = 1.0f;
    bool playing = true;
    float startTime = 0.0f;      // seconds (no longer normalized)
};

class VATComponent : public AnimationComponent {
public:
    // Clips are library assets; baking registers them once, instances share.
    VATComponent(GameObject* owner, MeshHandle mesh,
                 VATClipHandle clip, const VATProps& props = {});

    std::string GetName() const override { return "VAT"; }
    float GetDuration() const override;      // clip->GetDuration()

    void SetClip(VATClipHandle clip);        // switch clips (e.g. walk → die)

protected:
    void Evaluate(float time) override;      // material->normalizedTime =
                                             //   time / duration
};
```

The private loop/clamp/fmod logic in today's `VATComponent::OnTick` is
deleted — the base does it. Net effect: VAT gains pause, seek, events ("death
anim finished → despawn", which Deathmatch currently approximates with
timers), group time scales, and clip sharing, with less code than it has now.

---

## 5. Serialization & scene loading (breaking, by design)

- **ComponentFactory** — register `"ActionTimeline"`, `"Flipbook"`; the `"Animator2D"`
  and `"ActionManager"` factory names are removed. Existing JSON scenes that
  used them are migrated (the `Animator2D` `animations` array maps 1:1 onto
  `FlipbookClip`s; `ActionManager` `actions` arrays map onto a `ActionTimeline`
  per entry).
- **CSB scenes** (`scene_loader.cpp::ParseTimelines`) get *simpler*: CSB
  frames are already keyframes, so each (node, property) timeline maps
  directly onto a `ActionTrack` (frameIndex / frameRate / speed → key time,
  easingData → `ActionKey::easing`), grouped into one `ActionTimeline` per node,
  registered in the library, auto-played on a `ActionTimelineComponent` with
  `WrapMode::Loop` when `config.loopAnimations`. The keyframe → action →
  playback double translation disappears, and CSB named animation lists
  (currently ignored) map naturally onto multiple named timelines.
- **Lua bindings** — regenerate for the new surface:
  `entity:timeline():play("intro")`, the `Tween` builder, `entity:flipbook():play("walk")`,
  `animation.set_time_scale(0.2)`. The old `RunAction` bindings are dropped
  in the same commit that ports the scripts using them.

---

## 6. Editor / debugging

- `AnimationSubsystem::DrawImGui` — one table of all registered players:
  owner name, component kind, current clip/timeline, time/duration bar,
  playing state, group; plus global & per-group time-scale sliders.
- `AnimationComponent::DrawImGui` — shared transport UI (play/pause/stop,
  speed drag, seek slider — generalizing what `VATComponent::DrawImGui`
  already hand-rolls) + subclass extras (clip dropdown for flipbook, VAT
  texture stats, track list for tween timelines). Scrubbing is safe because
  `Evaluate` is pure and seek suppresses events.

---

## 7. Implementation plan

No compatibility shims, but still phased so each step compiles and the
examples run (ports land in the same commit as the breaking change they
require):

1. **Foundations** — `easing.hpp`, `animation_clip.hpp`,
   `animation_component.hpp`, `animation_subsystem.hpp/.cpp`; `Application`
   constructs the subsystem and calls `Process` from `Update`.
2. **FlipbookComponent** — implement; delete `animator_2d.*` and the
   `Animate` action; port the Animation example and the RPG example's
   `SpriteAnimator` in the same commit.
3. **ActionTimelineComponent** — implement timeline sampling + the `Tween` builder;
   rewrite `scene_loader.cpp` timeline parsing onto `ActionTrack`s; delete
   `action.hpp/.cpp`, `action_manager.*`; update ComponentFactory / JSON
   deserializers / Lua bindings; port remaining call sites.
4. **VATComponent** — rebase onto `AnimationComponent`, move clip ownership
   into the library, add `SetClip`; port the Deathmatch enemy.
5. **Polish** — subsystem ImGui panel, docs, Lua binding regeneration pass.

---

## 8. Future extensions (out of scope, but shaped for)

- **Skeletal animation**: a `SkeletalComponent : AnimationComponent` sampling
  a `SkeletonClip` slots into the same subsystem, library, and transport UI.
- **VAT crossfade**: `VATMaterial` gains a second clip binding + blend factor;
  `VATComponent` gains `CrossfadeTo(clip, seconds)` driven from `Evaluate`.
- **Blend/state machines**: a `StateMachineComponent` that owns transitions
  and drives a Flipbook/VAT/Skeletal player underneath — the reason `Play` is
  idempotent and `Evaluate` is a pure sample.
- **Determinism**: because all animation advances in one place with explicit
  dt scaling and pure sampling, headless/server mode can run the subsystem at
  a fixed tick (or skip render-only players) for the netcode use cases.
