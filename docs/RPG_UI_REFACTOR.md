# RPG example — UI refactor & analysis

This note documents the refactor of `Examples/RPG` away from immediate-mode UI,
and answers three questions: how the battle screen is put on screen, whether it
used the engine's existing UI system, and what is still missing to build a
text-heavy / narrative ("React-style") game on this engine.

## 1. How the battle screen appears

The battle is a mode of a small state machine, driven by components — not a
scene of its own:

1. `EnemyAIComponent::OnTick` (components.hpp) detects a player↔enemy rect
   overlap and calls back into `RPGGame::StartBattle`.
2. `BattleSystemComponent::StartBattle` flips `GameMode` to
   `BattleTransitionIn`, snapshots the enemy's stats into `BattleState`
   (`enemyStats/enemyNames/enemyIndices`), and `RPGGame::OnUpdate` fades to
   black over `_transition`.
3. At full fade, mode becomes `Battle`. `RPGGame::OnUpdate`'s `Battle` case does
   **no rendering** — the battle view is driven entirely by
   `BattleSystemComponent::OnTick`.
4. `OnTick` runs `UpdateBattle` (input + the `BattlePhase` turn state machine:
   `PlayerMenu → PlayerSkillMenu/ItemMenu → EnemyAction → Victory/Defeat/Flee`)
   and then reconciles the current `BattleState` onto the view.

Cursor state (`menuSel/skillSel/itemSel/targetIdx`) lives in C++; the view only
reflects it.

## 2. Did it use the existing UI system? (before → after)

**Before: no.** The engine ships two retained UI paths —

- **CanvasDrawable components** (`SpriteComponent` / `Text2DComponent` /
  `ShapeRendererComponent`): layer/z-sorted, batched by the `CanvasPass`.
- **RmlUi via `UIPageComponent`**: an HTML/CSS document (`.rml` + `.rcss`) with
  flow layout, word-wrap, data-driven updates and listener wiring, composited by
  the UI pass (layers ≥ `LAYER_UI_BACK`). This is the engine's "React-like"
  declarative UI, exemplified by Terrain / MultiplayerSandbox / TerrainStreaming.

The old battle screen (and explore HUD, and dialogue) used **neither**. Every
panel, bar, cursor row and log line was recomputed from magic 800×600 pixel
constants and re-issued each frame with `gfx->DrawQuad/DrawRect/DrawText`. The
`DrawPanel/DrawHPBar/DrawMPBar` helpers were even **duplicated** between
`main.cpp` (explore HUD) and `components.hpp` (battle).

**After: yes — a hybrid, split by the rendering pass each part belongs in.**

| Part | System | Why |
| --- | --- | --- |
| Battle backdrop, scanlines, combatant sprites | **Canvas** (`ui_kit.hpp`: `Quad`, `Sprite`) | Share the game's pixel/render space; sprites need per-frame UVs from a `FlipbookComponent`, tint, and shake. |
| Battle menus / skill·item lists / log / player stats / enemy read-outs | **RmlUi** (`BattleUIPage : UIPageComponent`, `assets/ui/battle.rml`) | Structured text; laid out and styled by RCSS; state pushed in from C++. |
| Explore HUD (LV/EXP, HP/MP, gold, hint) | **RmlUi** (`HudUIPage`, `hud.rml`) | Kills the immediate-mode copy shared with battle. |
| Dialogue box | **RmlUi** (`DialogueUIPage`, `dialogue.rml`) | Word-wrap + typewriter; seeds the narrative path. |

### Why hybrid, and a rendering-pass constraint worth knowing

Immediate `gfx->Draw*` calls are appended to the canvas queue **after** the
sorted `CanvasDrawable`s, with no interleave — so immediate draws always
composite *on top of* Canvas components. You therefore cannot half-migrate a
single pass (e.g. make the HUD Canvas components while the tilemap stays
immediate — the tilemap would paint over it). RmlUi sidesteps this because it is
a **separate pass on top**, so RmlUi HUD/menus correctly overlay the immediate
world. That is exactly why menus/log/dialogue went to RmlUi while the
world-coupled sprites went to Canvas.

Keyboard menus need **no** new engine plumbing: only mouse is bridged into the
RmlUi context today, so selection stays in C++ (`BattleState`) and is reflected
into the document by toggling a `.sel` class — the same pattern MultiplayerSandbox
uses for spell slots.

### What is intentionally still immediate

The explore **tilemap** (`Tilemap2D::Draw`), **2D lighting**
(`LightingSystem2D::Apply`) and the full-screen **fade** are still immediate:
they are engine systems with no component form yet, and they sit *below* the
RmlUi HUD so there is no ordering conflict. Converting them cleanly needs an
engine-level `TilemapComponent` / 2D-camera story (see §3).

## 3. What's missing for a text-heavy / narrative game

The `DialogueBox` here is the seed; a real narrative game still needs:

**Systems**
- **DialogueSystem / narrative runtime** — a script/graph format (lines,
  speakers, choices, branches, conditions, variables/flags) and a runner. Today
  "dialogue" is a `std::string` + timer and NPCs hold a flat `vector<string>`
  cycled by index — no branching, choices or state.
- **Rich text / advanced layout** — inline color/bold spans, inline icons and
  portraits, auto-height. (RmlUi already gives word-wrap and flow, which the
  `DialogueBox` uses; rich spans are the next step.)
- **Localization / string tables** — externalized strings, not literals.
- **Save/load of narrative state** — flags, visited nodes, variables.
- **RmlUi keyboard-focus navigation** — forward `ProcessKeyDown` and drive
  element focus, if you want native RmlUi menu navigation instead of the
  C++-owned-cursor approach used here.
- **Audio hooks** — per-char blip, voice lines.

**Reusable components / widgets**
- `DialogueBoxComponent` (done, minimal), `ChoiceMenuComponent`,
  `PortraitComponent`, a generic `MenuListComponent` (to remove the duplicated
  battle/skill/item selection logic), and a `Backlog/History` view.
- Engine-side, to finish removing immediate mode from the world: a
  `TilemapComponent` and a 2D lighting component, plus a 2D camera the CanvasPass
  already supports, so explore rendering can leave immediate mode too.

## 4. Files

- `ui_kit.hpp` — Canvas `Quad` / `Sprite` handles.
- `hud_ui_page.hpp`, `battle_ui_page.hpp`, `dialogue_ui_page.hpp` — `UIPageComponent` subclasses.
- `assets/ui/{hud,battle,dialogue}.{rml,rcss}` — the documents.
- `components.hpp` — `BattleSystemComponent` now drives a Canvas `BattleScene` + the RmlUi `BattleUIPage`; all `DrawBattle*` removed.
- `main.cpp` — HUD/dialogue via `UIPageComponent`; explore world/fade still immediate.
- `CMakeLists.txt` — stages the game's `assets/ui` into the Emscripten preload.

## 5. Build/verification status

This refactor was written against the engine's APIs (verified against the
migrated Terrain / MultiplayerSandbox usages) but **was not compiled or run** in
the environment it was authored in (no bootstrapped vcpkg / RmlUi toolchain).
The RML layout/positions are best-effort and may need visual tuning; the C++/RmlUi
API usage mirrors working examples. Please build and eyeball before relying on it.
