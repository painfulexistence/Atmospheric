# Deathmatch

A 2-player, server-authoritative, Quake-style **3D** arena shooter — the
Quake→Source FPS netcode lineage: client-side prediction, exact rewind-replay
reconciliation, entity interpolation, **view-angle replication + quantization**,
server-side **lag compensation**, and the **favor-the-shooter vs
favor-the-target** tradeoff.

Third real-time architecture in the repo, contrasted with the other two:

| Example | Model | Latency strategy |
|---|---|---|
| [MultiplayerSandbox](../MultiplayerSandbox) | P2P deterministic lockstep | delay-based |
| [HideAndSeek](../HideAndSeek) | server-authoritative state sync | predict + error-smoothing reconcile |
| **Deathmatch** | server-authoritative state sync | predict + exact rewind-replay + lag comp (3D) |

## Running

```sh
./DeathmatchServer [--port <n>] [--favor-target]      # default port 9200
./DeathmatchClient --connect <ip> [port]              # run two of these
```

`--favor-target` flips rocket lag comp from favor-the-shooter (default) to
favor-the-target so the difference is observable.

Controls: WASD move · mouse look · LMB railgun · F rocket · SPACE jump (hold
in air to levitate) · Q dash · C shield (hold) · ESC quit.

## What the 3rd dimension adds, netcode-wise

Going 3D isn't cosmetic here — it forces a class of classic UDP-FPS problems a
2D top-down version can't show:

- **View-angle replication + quantization.** Every player's yaw/pitch is
  replicated each tick. A float pair per player per tick is wasteful, so angles
  are quantized — Quake used a single byte per angle (~1.4°), Source ~16 bits;
  this uses 16-bit yaw/pitch (see `protocol.hpp` `QuantizeYaw`/`QuantizePitch`).
  The enemy's angles are interpolated (yaw by shortest-arc to handle the 2π
  wraparound a naive lerp gets wrong) so its model turns smoothly.
- **Yaw-relative movement in the reconciliation loop.** Movement is in the
  player's own view frame, so the input carries the view yaw and the server
  reproduces movement with the *exact* yaw the client predicted with — a
  subtlety a world-axis 2D mover doesn't have, but reconciliation stays exact.
- **3D capsule hit detection under lag comp.** Hitscan is a ray vs a vertical
  capsule; the server rewinds the target capsule to the shooter's `renderTick`.
  The capsule is yaw-invariant, which is why only *position* history is kept
  for rewind, not orientation.

## The two weapons demonstrate different lag-comp answers

- **Railgun (hitscan)** — point-in-time rewind. The server rewinds the target
  capsule to the tick the shooter was looking at and tests the ray there:
  favor-the-shooter. (`authority.cpp` `FireRail`.)
- **Rocket (projectile)** — rolling rewind + the favor-shooter/target toggle.
  favor-shooter judges the rocket against the target as it was `lagTicks` ago at
  every step of flight ("I died behind cover"); favor-target judges the present
  so the victim can dodge the replicated rocket. The central pillar is the cover
  that makes the difference visible. (`authority.cpp` rocket loop in `Tick`.)

## Character abilities — each a distinct netcode lesson

A small Control-style kit, chosen so every ability also demonstrates a
networking facet (all in `sim_common.hpp` `StepPlayer` + the reconciled
`Motion` state, so client and server run identical ability logic):

- **Jump / Levitate** (tap/hold SPACE) — vertical prediction. The reconciled
  state gains a vertical velocity `vy`; jump/levitate integrate it, and exact
  reconciliation now reproduces the *airborne* position with zero residual, not
  just the ground position.
- **Dash** (Q) — a predicted ability movement on a cooldown. The dash burst and
  its cooldown live in the reconciled `Motion`, so the client predicts the dash
  immediately and the server confirms it.
- **Shield** (hold C) — a replicated buff state that changes hit resolution. The
  server checks the shield at the target's **present** when a lag-compensated
  hit resolves — so a shield raised *after* the shooter fired still blocks. That
  favor-the-shooter-vs-shield collision is a real fairness wrinkle, deliberately
  left visible (`authority.cpp` `ApplyDamage`).

## Client prediction, three faces

`client_net.hpp`/`.cpp`: own movement predicted + **exact rewind-replay**
reconciled (zero residual on a clean link — the textbook version HideAndSeek's
error-smoothing deliberately approximates); the enemy interpolated in position
*and* orientation; own rockets cosmetically predicted.

## Why server-authoritative, not lockstep or rollback

Hitscan hit detection against another player's capsule is exactly what a single
authoritative simulator makes cheat-resistant and desync-free. The server hand-
rolls its kinematics (no Bullet) so it stays a lean, dependency-free class and
shares the *same* movement code with the client.

## What is verified, and what isn't

The netcode core (`authority.cpp` + `client_net.cpp` + `udp_socket.cpp`) is
covered by a headless integration test: exact reconciliation of yaw-relative
movement **including vertical jump/levitate** converges with zero residual, the
pillar occludes a railshot, favor-the-shooter hitscan hits a rewound capsule
the present-time shot misses, favor-shooter vs favor-target rockets diverge,
and the shield blocks a shot the same aim lands without it. The windowed
`DeathmatchClient` (3D camera, meshes, mouse-look — the local player is a
`DeathmatchController` Component) needs the full engine and is not built in the
netcode CI lane; its netcode is the same verified `ClientNet`.

## Roadmap / not yet built

- **Visual polish**: low-poly "Control"-style material palette, particle juice
  (muzzle/impact/explosion via the engine's particle subsystem), enemy-shield
  visual, dash trail, optional third-person camera. Real levels will come from
  Quake-map loading on a separate branch — the single greybox pillar here is
  only the placeholder the lag-comp "behind cover" demo needs.

## Known limitations (v1)

- 2 players; a slot can be claimed by any `ClientHello` (no auth); no
  disconnect timeout.
- Dash inputs aren't redundantly resent (a lost dash-tick packet drops the
  dash); shield/levitate have no energy cap.
- Rocket splash is direct-hit only; single capsule hitbox (no headshots).
- The engine has no relative-mouse/cursor-lock, so mouse-look uses absolute
  cursor delta (cursor can escape at window edges).
- Native only; no Emscripten/web build.
