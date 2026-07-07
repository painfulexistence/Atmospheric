# Deathmatch

A 2-player, server-authoritative, Quake-style arena shooter — the netcode of
the Quake→Source FPS lineage: client-side prediction, exact rewind-replay
reconciliation, entity interpolation, and the two things HideAndSeek's
proximity-tag mechanic never forced anyone to build — **server-side lag
compensation** and the **favor-the-shooter vs favor-the-target** tradeoff.

This is the third real-time architecture in the repo, contrasted with the
other two:

| Example | Model | Latency strategy |
|---|---|---|
| [MultiplayerSandbox](../MultiplayerSandbox) | P2P deterministic lockstep | delay-based (wait for inputs) |
| [HideAndSeek](../HideAndSeek) | server-authoritative state sync | predict + **error-smoothing** reconcile |
| **Deathmatch** | server-authoritative state sync | predict + **exact rewind-replay** reconcile + **lag comp** |

## Running

```sh
./DeathmatchServer [--port <n>] [--favor-target]      # default port 9200
./DeathmatchClient --connect <ip> [port]              # run two of these
```

`--favor-target` flips rocket lag compensation from favor-the-shooter (the
default) to favor-the-target, so you can watch the difference live.

Controls: WASD / arrows move · mouse aim · **LMB railgun** · **SPACE rocket** · ESC quit.

## The two weapons demonstrate *different* lag-comp answers

Instantaneous and travelling shots don't want the same treatment, and putting
both in one game is the whole point.

### Railgun — hitscan, point-in-time rewind (favor-the-shooter)

When you fire, the client tags the shot with **renderTick**: the server tick
you were actually *looking at* (your latest received snapshot tick minus the
interpolation delay the enemy is drawn at). The server rewinds the target to
renderTick and tests the ray against where the target was **on your screen**,
not where it has since moved to (`authority.cpp`, `FireRail`). This is favor
the shooter: if it looked like a hit to the player who fired, it lands. One
rewind, to one instant. Hitscan is essentially always favor-the-shooter —
nobody ships favor-the-target hitscan, it feels terrible to shoot.

### Rocket — projectile, rolling rewind, and the tradeoff that actually bites

A projectile travels, so its whole flight has to be judged, and *whose* time
frame it flies in is a real decision (`authority.cpp`, rocket loop in `Tick`):

- **favor-the-shooter** (`--favor-target` off, default): the rocket is checked
  against the target as it was `lagTicks` in the past at *every* step of its
  flight — a rolling rewind. A victim who has ducked behind the wall on their
  own screen can still be killed by the them-of-a-moment-ago out in the open.
  This is the infamous **"I died behind cover"**, and it's strictly worse than
  the hitscan version because the rewind offset compounds with travel time.
- **favor-the-target** (`--favor-target` on): the rocket is judged against the
  target's *current* position — the same authoritative rocket everyone sees
  replicated and can dodge — at the cost of the shooter having to lead more
  than their screen suggests.

The occluder wall in the middle of the arena exists precisely so this is
observable: duck behind it right after someone fires a favor-shooter rocket
down your lane and you still die; flip to `--favor-target` and the same dodge
works. Quake shipped world-time (favor-target) rockets; Overwatch favors the
shooter for projectiles too — the "right" answer is a genuine design choice,
which is why this is the deepest lag-comp interview question.

## Client-side prediction has three faces here

All in `client_net.hpp`/`.cpp`, deliberately different because each has a
different amount of information to work with:

1. **Own movement — predicted, then reconciled with exact rewind-replay.**
   The client keeps every un-acked input; each snapshot it resets to the
   server's authoritative position as of `ackedInputTick` and re-applies the
   inputs the server hasn't seen yet, via the *same* `sim::StepPlayer` the
   server runs. Because the server consumes each input exactly once in order
   (`authority.cpp`), the replay reproduces the prediction with **zero
   residual** on a clean link — this is the textbook version HideAndSeek's
   README calls "the path not taken", where HideAndSeek instead blends a
   fraction toward the server each snapshot.
2. **The enemy — not predicted, interpolated.** Rendered between the last two
   snapshots with a fixed ~100 ms delay, since there's no local input to
   predict it from. That render delay is also what *defines* renderTick, the
   value the server rewinds to — the lag comp and the interpolation are two
   sides of the same "the enemy you see is in the past" coin.
3. **Own rockets — cosmetic prediction.** A local rocket is shown leaving the
   barrel immediately so firing feels responsive, while the server's
   authoritative rocket (replicated in snapshots) is the one that deals damage.

## Fire events are reliable-over-UDP; movement isn't

Movement can go stale for a tick (the next snapshot corrects it), but a lost
"I shot" packet is a shot that never happened. Each fire carries a per-weapon
sequence number (`railSeq` / `rocketSeq`) that the client repeats in every
input packet until the next fire supersedes it; the server acts on a sequence
only when it advances and dedups the repeats (`protocol.hpp`, `authority.cpp`).
A minimal reliable-*events* layer, distinct from MultiplayerSandbox's
reliable-*stream* redundancy for lockstep inputs.

## Why server-authoritative, not lockstep or rollback

Same reasons as HideAndSeek plus one: hitscan hit detection against another
player's position is exactly what a single authoritative simulator makes
cheat-resistant and desync-free. Rollback (the fighting-game model) would be
peer-to-peer deterministic with no server to do favor-the-shooter hit
detection at all — a different architecture for a different genre.

## What is verified, and what isn't

The netcode core (`authority.cpp` + `client_net.cpp` + `udp_socket.cpp`) is
covered by a headless integration test that drives the authority in-process
with synthetic clients and asserts: exact reconciliation converges with zero
residual, wall occlusion blocks a railshot, favor-the-shooter hitscan hits a
rewound position the present-time shot misses, and favor-shooter vs
favor-target rockets diverge (kill-behind-cover vs successful dodge). The
windowed `DeathmatchClient` (rendering/input) needs the full engine and is not
built in every CI lane; its netcode is the same verified `ClientNet`.

## Known limitations (v1)

- 2 players, one weapon of each kind, no weapon switching or ammo.
- A slot can be claimed by anyone who sends a `ClientHello` (no auth); no
  disconnect/timeout handling beyond a fresh hello taking the slot.
- Rocket splash is direct-hit only (no area damage).
- No client-side anti-cheat needed — the server is authoritative — but there's
  no rate-limiting on fires or inputs (see `UdpRelay` for the pattern a
  public-facing version would want).
- Native only; no Emscripten/web build (raw UDP sockets).
