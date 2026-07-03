# Vision

Atmospheric is a labor of love, but not an aimless one. This document is the
yardstick I measure every design decision against. When a change makes the
engine bigger, slower to understand, or harder to build, it has to justify
itself against these principles.

## Coder-friendly DX

The engine is built for people who like writing code. Clean, discoverable
APIs; fast incremental builds; readable errors; one-command builds via CMake
presets. Anything a contributor has to learn twice is a bug in the developer
experience.

## Cross-platform, for real

Windows, macOS, Linux, iOS, Android, and the Web are all first-class targets —
every one of them is built by CI and verified on real hardware or simulators.
"Cross-platform" doesn't mean "compiles everywhere"; it means the same game
runs everywhere.

## Multi-threaded by default

Parallelism is the baseline, not an optimization pass. Update and render run
concurrently, asset loading goes through the job system, and long-running work
(video decode/encode, prefetching) lives on worker threads. Single-threaded
mode exists as a fallback (e.g. non-pthread web builds), not as the default.

## Small and simple — but 2D + 3D out of the box

Atmospheric stays a codebase one person can hold in their head. No plugin
labyrinth, no feature flags for features nobody uses. Within that budget, both
2D and 3D are supported natively — sprites, 2D physics, and canvas layering
next to PBR, shadows, and terrain — without bolting one world onto the other.

## Component-based gameplay architecture

Gameplay code follows the model Unity proved out: GameObjects composed of
components, with clear ownership and lifecycle (attach, tick, detach).
Familiarity is a feature — if you've shipped anything in a component-based
engine, you already know how to structure a game here.

## A rich set of examples, all open-source

The `Examples/` directory is the living documentation: from HelloWorld to a
voxel world, a card battler, 2D physics, procedural terrain, multiplayer, and
video playback. Every example builds on every platform, is captured by
automated smoke tests, and is free to copy from. If a feature has no example,
it isn't finished.

## Easy-to-use Lua APIs

Scripting should feel like Löve2D: a small, friendly Lua surface where a
handful of lines gets something moving on screen. The Lua frontend is a
first-class way to build a game — not a config language bolted on top — while
the C++ core stays available when you need to drop down.
