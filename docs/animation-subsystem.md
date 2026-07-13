# Animation Subsystem — Design

Status: **proposal** (design only, no code yet)

This document proposes a unified `AnimationSubsystem` that consolidates every
piece of animation code currently in the engine — property tweens
(`ActionManager` / `Action`), frame-based sprite animation (`Animator2D`, the
`Animate` action, and the RPG example's ad-hoc `SpriteAnimator`), and GPU
vertex animation (`VATClip` / `VATComponent`) — behind one subsystem, one
shared clip library, and a small family of components with a common playback
interface.

---

## 1. Current state (what we are unifying)

| Code | Kind | Problems |
|------|------|----------|
| `Action` / `ActionInterval` / `ActionManager` (`action.hpp`, `action_manager.cpp`) | Property tweens (MoveTo, ScaleTo, ColorTo, Sequence, RepeatForever…), Cocos-style | Component ticks itself; raw-`new` ownership API; no pause/speed/timescale; no way to name, reuse, or re-trigger a timeline — `scene_loader.cpp` compiles CSB timelines straight into one fire-and-forget action per node |
| `Animator2D` (`animator_2d.hpp`) | Frame-based (UV) sprite animation | Clips are stored per component (no sharing); no events; no speed control |
| `Animate` action (`action.hpp`) | Frame-based sprite animation *again*, as an Action | Duplicates `Animator2D`; owns a private copy of the clip; header carries a long TODO about where `AnimationClip` should live |
| `SpriteAnimator` (Examples/RPG/`rpg_entity.hpp`) | Frame-based sprite animation *a third time*, engine-free struct | Exists because the engine component wasn't convenient enough — a signal the engine API needs to absorb this use case |
| `VATClip` / `VATComponent` (`vat.hpp`, `vat_component.cpp`) | GPU vertex animation | Playhead logic (loop/clamp/speed) re-implemented privately; clip is owned by the component so it can't be shared between instances of the same enemy mesh |

Cross-cutting gaps:

- **No central tick.** Each component advances time in its own `OnTick`, so
  there is no global animation time scale (slow-mo, pause-world-but-not-UI),
  and no single place to inspect "everything currently animating".
- **No shared clip storage.** Every system invents its own clip type and its
  own ownership story (owned copy, per-component map, unique_ptr).
- **No events.** Nothing exposes "clip finished", "looped", or per-frame
  events (footstep on frame 3), which every game ends up hand-rolling.

---

## 2. Goals / non-goals

Goals

1. One subsystem (`AnimationSubsystem`) that ticks all animation, owns global
   and per-group time scales, and exposes a debug view of active players.
2. One clip library: named, shared, ref-counted animation assets
   (`FlipbookClip`, `ActionTimeline`, `VATClip`).
3. Three focused components sharing a common playback base:
   `ActionTimelineComponent` (tweens/timelines), `FlipbookComponent`
   (frame-based, drives `SpriteComponent`), `VATComponent` (GPU vertex
   animation, refactored onto the base).
4. Unified playback semantics — play/pause/stop/speed/loop and completion
   events behave identically across all three.
5. Backward compatibility during migration: `ActionManager::RunAction` and the
   CSB loader keep working; `Animator2D` becomes a deprecated shim.

Non-goals (for this iteration)

- Skeletal animation / animation blending trees. The design leaves room for a
  future `SkeletalComponent` as a fourth player kind, but nothing more.
- VAT crossfading (needs shader work — two clip bindings + blend weight).
  Clip *switching* is in scope.
- A timeline *editor*. The editor integration is limited to the existing
  `DrawImGui` inspector pattern.

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
              │ Component         │   │ Component      │   │               │
              │ (tweens, CSB      │   │ (frame-based,  │   │ (GPU vertex   │
              │  timelines,       │   │  drives Sprite │   │  animation)   │
              │  RunAction API)   │   │  UVs)          │   │               │
              └───────────────────┘   └────────────────┘   └───────────────┘
                        │                     │                    │
                  ActionTimeline        FlipbookClip            VATClip
                  (shared asset)        (shared asset)        (shared asset)
                              all stored in AnimationLibrary
```

All three components derive from a common `AnimationComponent` base that owns
the playback state machine. The subsystem iterates registered components once
per frame; the components' own `OnTick` is disabled (`CanTick() == false`),
mirroring how `CanvasDrawable`s are drawn by the batch renderer rather than
ticking themselves.

### File layout

```
Engine/include/Atmospheric/
    animation_clip.hpp        NEW  FlipbookFrame, FlipbookClip, ActionTimeline
    animation_component.hpp   NEW  AnimationComponent base + PlaybackState
    animation_subsystem.hpp   NEW  AnimationSubsystem + AnimationLibrary
    action.hpp                KEEP Action hierarchy (unchanged public API);
                                   `Animate` reimplemented on FlipbookClip
    action_manager.hpp        KEEP thin deprecated alias (see §7)
    action_timeline_component.hpp NEW
    flipbook_component.hpp    NEW
    animator_2d.hpp           DEPRECATED shim → FlipbookComponent
    vat.hpp                   KEEP  (VATClip unchanged; gains library storage)
    vat_component.hpp         CHANGED derives AnimationComponent
```

---

## 4. Core types

### 4.1 Playback state (shared semantics)

Every player — tween timeline, flipbook, VAT — reduces to the same state
machine, which today is re-implemented three times with three sets of bugs
(`Animator2D` has no speed, `VATComponent` has no events, `ActionManager` has
no pause). It is written once in the base:

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
    // applied and `time` already advanced/wrapped. Subclasses only *sample*.
    virtual void Evaluate(float time) = 0;

    PlaybackState _state;
    friend class AnimationSubsystem;
};
```

Key decision: **the base advances and wraps time; subclasses only sample.**
`Evaluate(t)` must be a pure function of `t` wherever possible — this is what
makes seek/scrub in the editor, negative speed, and deterministic replay
(relevant to the netcode work) fall out for free. The one exception is the
imperative `RunAction` path, see §4.4.

### 4.2 Clips (shared assets) and the library

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
    float GetDuration() const;   // sum of frame durations (cached)

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

`ActionTimeline` is the *declarative* counterpart of an action sequence: a
named, reusable recipe that can be instantiated many times (unlike an
`Action`, which is single-use mutable state):

```cpp
struct ActionTimeline {
    std::string name;
    // Factory producing a fresh action tree per playback. Keeps the whole
    // existing Action zoo (Sequence, RepeatForever, easing…) working as the
    // evaluation engine without copying every Action subclass into a new
    // keyframe format. CSB loading and Lua build these.
    std::function<std::unique_ptr<Action>()> instantiate;
    float duration = 0.0f;       // 0 = unbounded (e.g. RepeatForever)
};
```

The library lives inside the subsystem (scene-scoped, cleared by
`UnloadCurrentScene` like other scene-tier assets in `AssetManager`):

```cpp
class AnimationLibrary {
public:
    // Register returns a stable handle; names are unique per kind.
    FlipbookClipHandle   AddFlipbook(FlipbookClip clip);
    TimelineHandle       AddTimeline(ActionTimeline tl);
    VATClipHandle        AddVATClip(std::string name, std::unique_ptr<VATClip> clip);

    const FlipbookClip*  GetFlipbook(FlipbookClipHandle) const;   // + by-name lookups
    const ActionTimeline* GetTimeline(TimelineHandle) const;
    VATClip*             GetVATClip(VATClipHandle) const;

    void Clear();                // scene unload
};
```

Moving `VATClip` ownership here fixes the current "one baked texture set per
component" cost: ten VAT enemies of the same type share one clip. (The
`VATMaterial` per instance stays — it carries the per-instance playhead.)

`AnimationFrame`/`AnimationClip` from `animator_2d.hpp` are replaced by
`FlipbookFrame`/`FlipbookClip` in `animation_clip.hpp`, which also resolves
the long-standing TODO comment block in `action.hpp` about where those structs
should live.

### 4.3 AnimationSubsystem

Follows the established service pattern (`AudioSubsystem`-style static
locator; owned by `Application` in construction order after graphics, before
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
`OnLooped`/`OnFinished`); call `Evaluate(time)`. Registration/unregistration
during the walk is deferred to the end of the pass — same discipline
`ActionManager::OnTick` already implements for its action list, now written
once instead of per component.

Tick order: `Process` runs in the frame after entity `Tick` (game logic
decides *what* to play) and before rendering/transform sync (so sampled poses
are what gets drawn). This matches where `GameLayer` currently ticks the
components anyway, so behavior is unchanged — it just becomes explicit.

### 4.4 ActionTimelineComponent (absorbs ActionManager)

Two modes, both first-class:

- **Imperative** — the existing fire-and-forget `RunAction` API, unchanged in
  spirit. This is inherently stateful (actions mutate targets with deltas), so
  these actions are *stepped*, not evaluated; seek/scrub does not apply to
  them.
- **Declarative** — named `ActionTimeline`s from the library, playable and
  re-triggerable by name: `Play("open_menu")`. Each `Play` instantiates a
  fresh action tree via `ActionTimeline::instantiate` and steps it; `Stop`
  discards it. This is what the CSB loader and gameplay code should migrate
  to.

```cpp
class ActionTimelineComponent : public AnimationComponent {
public:
    explicit ActionTimelineComponent(GameObject* owner);
    std::string GetName() const override { return "ActionTimeline"; }

    // ── imperative (ActionManager-compatible) ────────────────────────
    void RunAction(std::unique_ptr<Action> action);
    void RunAction(Action* action);          // takes ownership (Lua/legacy)
    void StopAllActions();

    // ── declarative (new) ────────────────────────────────────────────
    void AddTimeline(TimelineHandle tl);                 // from the library
    void AddTimeline(const std::string& libraryName);
    bool PlayTimeline(const std::string& name);          // restarts if playing
    void StopTimeline();
    const std::string& GetCurrentTimeline() const;

    float GetDuration() const override;      // current timeline's duration

protected:
    void Evaluate(float time) override;      // steps timeline + ad-hoc actions

private:
    // ad-hoc actions (imperative path) — same deferred add/stop discipline
    // as today's ActionManager
    std::vector<std::unique_ptr<Action>> _actions, _actionsToAdd;
    bool _stopRequested = false;

    // declarative path
    std::vector<TimelineHandle> _timelines;
    std::unique_ptr<Action> _activeInstance;  // current timeline instantiation
    TimelineHandle _active;
    float _lastEvalTime = 0.0f;               // Evaluate() derives dt for Step
};
```

Implementation note: because `Action::Step(dt)` is delta-driven, `Evaluate`
computes `dt = time - _lastEvalTime` and steps. Backward scrub on the
imperative path is explicitly unsupported (documented); the declarative path
supports restart-then-fast-forward as a cheap approximation of seek, which is
sufficient for editor preview.

### 4.5 FlipbookComponent (absorbs Animator2D / Animate / SpriteAnimator)

```cpp
class FlipbookComponent : public AnimationComponent {
public:
    explicit FlipbookComponent(GameObject* owner);
    std::string GetName() const override { return "Flipbook"; }

    // Clips come from the library (shared) or are registered locally.
    void AddClip(FlipbookClipHandle clip);
    void AddClip(const std::string& libraryName);
    FlipbookClipHandle AddLocalClip(FlipbookClip clip);  // convenience: adds to
                                                         // library + registers

    bool Play(const std::string& clipName);  // no-op if already playing it
                                             // (SpriteAnimator's idempotent play)
    const std::string& GetCurrentClip() const;
    int GetCurrentFrame() const;

    // Frame events: fired when playback enters a frame with eventId >= 0.
    void SetOnFrameEvent(std::function<void(int eventId)> cb);

    // Optional per-clip flip override (walk-left = walk-right + flipX,
    // halving the number of authored clips — the RPG pattern).
    void SetFlipX(bool flip);

    float GetDuration() const override;

protected:
    void OnAttach() override;                // caches SpriteComponent*
    void Evaluate(float time) override;      // binary-search frame, set UVs once

private:
    SpriteComponent* _sprite = nullptr;      // also probes Sprite3DComponent
    std::vector<FlipbookClipHandle> _clips;
    FlipbookClipHandle _current;
    int _lastFrame = -1;                     // change detection: only touch the
                                             // sprite (and fire events) on frame
                                             // transitions
    std::vector<float> _frameEndTimes;       // prefix sums for O(log n) sampling
    std::function<void(int)> _onFrameEvent;
};
```

`Evaluate(t)` maps `t` to a frame index via prefix-summed frame end times
(the `_splitTimes` idea from `Animate`, done once per clip instead of per
playback), writes `SetUVs` only when the frame index changes, and fires frame
events for the entered frame. Because it is a pure sample of `t`, wrap modes,
negative speed and scrubbing all work with zero extra code.

Works against `SpriteComponent` first; `OnAttach` falls back to
`Sprite3DComponent` (same UV interface) so billboarded world sprites animate
with the same component.

### 4.6 VATComponent (refactored onto the base)

Public construction stays source-compatible, plus a library-handle overload:

```cpp
class VATComponent : public AnimationComponent {
public:
    // Existing signature (takes ownership → clip is auto-registered into the
    // library under a generated name, so old call sites compile unchanged).
    VATComponent(GameObject* owner, MeshHandle mesh,
                 std::unique_ptr<VATClip> clip, const VATProps& props = {});
    // New: share one baked clip across many instances.
    VATComponent(GameObject* owner, MeshHandle mesh,
                 VATClipHandle clip, const VATProps& props = {});

    std::string GetName() const override { return "VAT"; }
    float GetDuration() const override;      // clip->GetDuration()

    void SetClip(VATClipHandle clip);        // switch clips (e.g. walk→die)

protected:
    void Evaluate(float time) override;      // material->normalizedTime =
                                             //   time / duration
};
```

`VATProps{speed, loop, playing, startTime}` maps onto `PlaybackState`
(`loop → WrapMode::Loop / ClampHold`, `startTime` is normalized → seeded via
`Seek(startTime * duration)`); the struct is kept as a constructor convenience.
The private loop/clamp/fmod logic in `VATComponent::OnTick` is deleted — the
base does it. Net effect: VAT gains pause, events ("death anim finished →
despawn", which Deathmatch currently approximates with timers), group time
scales, and clip sharing, for less code than it has now.

---

## 5. Serialization & scene loading

- **ComponentFactory** — register `"Flipbook"` and `"ActionTimeline"`.
  `"Animator2D"` and `"ActionManager"` registrations stay but construct the
  new components (the factory name is the compatibility surface; `GetName()`
  of the shims returns the old names so editor round-trips are stable).
- **JSON scenes** (`application.cpp` registrations): the `Animator2D`
  deserializer's `animations` array maps 1:1 onto `FlipbookClip`s registered
  in the library (named `"<entity>/<clip>"`) and added to a
  `FlipbookComponent`. The `ActionManager` post-init hook (`ParseAction`)
  keeps producing imperative `RunAction`s.
- **CSB scenes** (`scene_loader.cpp::ParseTimelines`): instead of immediately
  `RunAction`-ing one anonymous looped sequence per node, the parser builds an
  `ActionTimeline` per (node, animation) whose `instantiate` closure rebuilds
  the parsed sequence, registers it in the library, and gives the node an
  `ActionTimelineComponent` that auto-plays it. Same runtime behavior as
  today, but timelines become stoppable, re-triggerable and inspectable, and
  CSB "animation list" names (currently ignored) get a natural home.
- **Lua bindings**: `RunAction` bindings keep working (the raw-pointer
  overload exists for exactly this). New bindings are a thin layer:
  `entity:flipbook():play("walk")`, `animation.set_time_scale(0.2)`,
  `entity:timeline():play("intro")`.

---

## 6. Editor / debugging

- `AnimationSubsystem::DrawImGui` — one table of all registered players:
  owner name, component kind, current clip/timeline, time/duration bar,
  playing state, group; plus global & per-group time-scale sliders. This is
  the "what is animating right now" view the engine currently lacks.
- `AnimationComponent::DrawImGui` — shared transport UI (play/pause/stop,
  speed drag, seek slider — generalizing what `VATComponent::DrawImGui`
  already hand-rolls) + subclass extras (clip dropdown for flipbook, VAT
  texture stats, action count for timelines).

---

## 7. Migration plan

Phased so every step compiles, runs the existing examples, and is
independently committable:

1. **Foundations** — add `animation_clip.hpp`, `animation_component.hpp`,
   `animation_subsystem.hpp/.cpp`; `Application` constructs the subsystem and
   calls `Process` from `Update`. Nothing registers yet; zero behavior change.
2. **FlipbookComponent** — implement on the base; reimplement `Animator2D` as
   a deprecated subclass/shim that forwards `AddAnimation/Play/Stop` and keeps
   its factory entry; reimplement the `Animate` action to sample a
   `FlipbookClip` handle instead of owning a private `AnimationClip` copy.
   Port the Animation example; optionally replace the RPG `SpriteAnimator`
   (or leave it — examples may lag).
3. **ActionTimelineComponent** — implement; `ActionManager` becomes
   `using ActionManager = ActionTimelineComponent` behind a deprecation note
   (its public surface — `RunAction` ×2, `StopAllActions` — is a strict
   subset). Migrate `scene_loader.cpp` to build library timelines.
4. **VATComponent** — rebase onto `AnimationComponent`, move clip ownership
   into the library, add `SetClip`. Port the Deathmatch enemy.
5. **Cleanup** — delete `animator_2d.cpp` internals, drop the TODO block in
   `action.hpp`, document the subsystem in `docs/`, add Lua bindings for the
   new surface.

Compatibility contract during migration: `ActionManager::RunAction(new ...)`,
`Animator2D` factory/JSON scenes, CSB loading, and the `VATComponent`
unique_ptr constructor all keep working unchanged.

---

## 8. Future extensions (explicitly out of scope, but shaped for)

- **Skeletal animation**: a `SkeletalComponent : AnimationComponent` sampling
  a `SkeletonClip` slots into the same subsystem, library, and transport UI.
- **VAT crossfade**: `VATMaterial` gains a second clip binding + blend factor;
  `VATComponent` gains `CrossfadeTo(clip, seconds)` driven from `Evaluate`.
- **Blend/state machines**: a `StateMachineComponent` that owns transitions
  and drives a Flipbook/VAT/Skeletal player underneath — the reason `Play` is
  idempotent and `Evaluate` is a pure sample.
- **Determinism**: because all animation advances in one place with explicit
  dt scaling, headless/server mode can run the subsystem at a fixed tick (or
  skip render-only players) for the netcode use cases.
