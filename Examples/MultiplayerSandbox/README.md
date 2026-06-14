# NoitaLike — 2P networked falling-sand arena

A Noita-style proof of concept built on AtmosphericEngine: a fully destructible
falling-sand world, two networked players, and seven spells/weapons.

This is a native re-imagining of the `noesis-game` quicksand sample. That sample
kept the cellular automaton on a Rails ActionCable server and streamed grid
deltas to NoesisGUI clients; here the simulation runs natively in C++ on both
peers and only *inputs* cross the wire (deterministic lockstep), which is both
dramatically cheaper (≈10 bytes/tick) and lower latency. The HUD uses RmlUi —
the engine's built-in replacement for NoesisGUI.

## Running

```sh
./NoitaLikeDemo                    # solo sandbox
./NoitaLikeDemo --host [port]      # host a 2-player game (default port 7777)
./NoitaLikeDemo --join <ip> [port] # join a hosted game
```

Options:

- `--seed <n>` world seed (host decides; the client inherits it)
- `--delay <ticks>` lockstep input delay, default 3 ticks = 50 ms @ 60 Hz.
  Use 1–2 on LAN for near-instant response; raise it if the connection stalls.
- `--autotest <ticks>` headless-ish smoke test: run N ticks, print the world
  checksum, and exit (used to verify cross-peer determinism).

## Controls

| Input            | Action                              |
|------------------|-------------------------------------|
| A / D            | move                                |
| W / Space        | jump; hold in mid-air to levitate   |
| Mouse            | aim                                 |
| Left button      | cast / fire                         |
| 1–7              | select spell                        |
| Esc              | quit                                |

## Spells & weapons

1. **Spark Bolt** — fast light bolt, chips terrain
2. **Fireball** — explodes, ignites oil/wood
3. **Grenade** — bounces, big explosion on a fuse
4. **Water Jet** — sprays water cells (douses fire, pushes players)
5. **Acid Flask** — lobbed vial, splashes terrain-dissolving acid
6. **Dig Blast** — hitscan excavation, tunnel anywhere
7. **Rapid Wand** — low-damage automatic fire with spread

## World materials

Stone (indestructible frame/veins), dirt, sand, wood, water, oil, acid, lava,
fire, smoke and steam — with the interactions you'd expect: fire spreads
through wood and oil, water turns lava to stone and fire to steam, acid eats
everything but stone, sand sinks through liquids, gases rise.

## Architecture

```
main.cpp          Application subclass: fixed 60 Hz stepping, rendering
                  (grid → GL texture → DrawTexturedQuad, players/projectiles
                  via BatchRenderer2D), RmlUi HUD, input sampling
game_sim.{hpp,cpp}    deterministic gameplay: players, spells, projectiles,
                      explosions, damage/respawn
sand_world.{hpp,cpp}  deterministic falling-sand cellular automaton
                      (480×270 cells, xorshift32 RNG)
net_lockstep.{hpp,cpp} 2-peer UDP lockstep: input exchange with redundant
                       resend, RTT measurement, checksum-based desync detection
assets/ui/hud.{rml,rcss}  RmlUi HUD (HP bars, score, spell bar, status)
```

### Netcode model

Deterministic lockstep: both peers run the identical simulation from a shared
seed; each tick consumes one `InputFrame` (4 bytes) per player. Local input is
scheduled `inputDelay` ticks ahead; every UDP packet redundantly carries the
last ≤32 unacked frames and is resent every frame, so isolated packet loss
never stalls the game. Peers exchange a state checksum every 2 s to detect
divergence. Because floats are involved in player/projectile motion, both
peers should run the same build on the same architecture (fine for a PoC).

Web builds compile but run solo only (no UDP sockets in the browser).
