# Arena FPS — Design Spec

**Date:** 2026-07-02
**Status:** Approved
**Working title:** `arena` (binary name, trivial to rename later)

## Summary

A fast, flat-shaded multiplayer arena FPS in C++ with a custom engine, running on
Linux and Windows. 2–8 players, free-for-all deathmatch in a single hand-authored
map, first to 20 frags. Quake-style movement physics extended with dash and
crouch-slide. Authoritative server without client-side prediction (LAN / low-ping
oriented). One binary that can play, host, or run as a headless dedicated server.

## Goals & Non-Goals

**Goals (v1):**
- Playable, fun deathmatch for 2–8 players over LAN or low-ping internet.
- Custom engine: hand-written renderer, game loop, physics, netcode, audio mixer.
- Movement depth: strafe jump, bunny hop, air control, rocket jump, dash, crouch-slide.
- Builds and runs on Linux (GCC/Clang) and Windows (MSVC; MinGW cross-compile for
  verification from Linux).

**Non-goals (v1, explicit stretch goals):**
- Client-side prediction / lag compensation (netcode is server-authoritative,
  render-what-the-server-says).
- Server-side bots with real AI (a dumb `--bot` scripted client exists for testing).
- Multiple maps, more than two weapons, matchmaking, in-game map editor, CI.

## Dependencies

Philosophy: thin platform libs only; everything that matters is hand-written.

| Dependency | Used for | Notes |
|---|---|---|
| SDL3 | window, GL context, input, audio device, timing | `find_package` first, `FetchContent` fallback so Windows builds are turnkey |
| OpenGL 3.3 core | rendering | function pointers loaded by hand via `SDL_GL_GetProcAddress` (~50 functions, no glad/glew) |
| stb_easy_font | HUD text | single header, vendored |
| OS sockets | networking | BSD sockets / Winsock2 behind one thin wrapper |

Hand-written: math library (vec2/3/4, mat4), renderer, collision & movement
physics, network protocol, audio mixer + procedural sound synthesis, test harness.

Toolchain: C++20, CMake ≥ 3.24, Ninja on Linux, MSVC or MinGW on Windows.
Host machine needs `cmake` installed (and `mingw-w64-gcc` for Windows
cross-verification).

## Architecture

Single binary, three run modes:

- **(no args)** — minimal keyboard menu: Host / Join by IP / Quit.
- **`--connect <ip[:port]>`** — join a server.
- **`--host [--port N]`** — server on a background thread + local client connected
  over loopback UDP (identical code path to remote play; no special local-player logic).
- **`--dedicated [--port N] [--fraglimit N]`** — headless server loop on the main
  thread; no window, no audio, clean shutdown on SIGINT/Ctrl-C.

### Module layout

```
gamees/
  CMakeLists.txt
  src/
    main.cpp        — arg parsing, mode dispatch
    core/           — math, time, log, rng, small utilities
    platform/       — sockets wrapper (BSD/Winsock2); everything else via SDL3
    render/         — GL loader, shader, meshes, camera, particles, HUD text
    audio/          — mixer over SDL3 audio stream, procedural sfx synthesis
    net/            — protocol: bounds-checked (de)serialization, connection, snapshots
    game/           — SHARED SIMULATION: entities, movement, weapons, collision, map, rules
    client/         — input sampling, snapshot interpolation, render/audio glue, HUD, menu
    server/         — tick loop, client sessions, snapshot broadcast, --bot support
  maps/arena.txt    — the shipped map
  tests/            — hand-rolled test binary
```

Boundary rule: `game/` (the simulation) has no dependency on `render/`, `audio/`,
or SDL. `server/` depends only on `game/`, `net/`, `platform/`, `core/`.

### Entities

Plain structs in fixed-size arrays (max 8 players, small caps for rockets/pickups/
events). No ECS framework. Entity kinds: **Player**, **Rocket**, **Pickup**
(health pack), plus transient **Events** (kill, sound cue, join/leave).

## Simulation

Fixed 60Hz tick on the server. All rules live in `game/` and are deterministic
given (state, inputs) — not bit-perfect cross-platform (not needed; only the
server simulates).

### Map format

Plain text, one directive per line:

- `box x y z w h d r g b` — solid colored AABB (world geometry and collision are
  the same data; stairs approximate ramps)
- `spawn x y z yaw` — player spawn point
- `health x y z` — health pack location
- `light dx dy dz` / `fog r g b density` — visual tuning

One arena ships in v1, tuned for the movement set: bhop lines, rocket-jump
towers, a central pit.

### Player movement

Hull: AABB 0.6 × 0.6 × 1.8 m (crouched: 1.2 m tall). Move-and-slide against world
AABBs, up to 3 slide iterations, step-up ≤ 0.4 m, jump buffering (~0.1 s).

Starting constants (all tunable in one header):

| Constant | Value |
|---|---|
| gravity | 20 m/s² |
| ground max speed | 8 m/s |
| ground accel / friction | 60 m/s² / 6 s⁻¹ |
| air accel | 25 m/s² with 1.0 m/s wish-speed cap (Quake air control) |
| jump velocity | 7 m/s |
| dash | 12 m/s impulse in wish dir, 2 s cooldown, usable airborne |
| crouch-slide | trigger: crouch while grounded & speed > 9 m/s; +2 m/s initial boost; friction 0.5 s⁻¹ while sliding, decays to normal over 1 s |

Strafe jumping / bhop / air control emerge from the air-accel math. Rocket
knockback applies to the shooter (rocket jumps).

### Player classes

Players can switch between three lightweight classes. **Ranger** is the baseline
arena character and uses the rifle as primary. **Scout** has lower max health,
higher movement speed, stronger/faster dash recovery, and uses a close-range
shotgun as primary. **Tank** has higher max health, slightly higher outgoing
damage, lower incoming damage, slower movement/dash recovery, and uses a fast
low-damage LMG as primary. Class choice is sent as input and applied by the
authoritative server; snapshots replicate class id for HUD/scoreboard/render
feedback.

### Combat

| | Rifle | Shotgun | LMG | Rocket launcher |
|---|---|---|---|---|
| type | hitscan ray | 7-pellet hitscan spread | fast hitscan ray | projectile, 25 m/s |
| damage | 9 per hit | 8 per pellet | 2.5 per hit | 80 direct; splash ≤ 80 in 3.5 m radius, linear falloff |
| fire interval | 0.12 s | 0.65 s | 0.055 s | 0.8 s |
| ammo | infinite | infinite | infinite | infinite |
| knockback | small | small per pellet | tiny | strong (incl. self) |

Ranger has 100 HP; Scout/Tank adjust max health by class. Health packs +25
(capped by class max), 15 s respawn timer. Death → respawn after 2 s at the
spawn point farthest from living enemies. Score: +1 frag per kill, −1 for
suicide. First to frag limit (default 20) wins → 10 s scoreboard → map restarts.

## Netcode

Authoritative server, **no client prediction**: clients send inputs, server
simulates, clients render server state. Hand-rolled protocol over UDP.

- **Packets:** little-endian, magic + protocol version header. Every read goes
  through a bounds-checked reader; malformed packets are dropped silently.
- **Connection:** hello → accept (player id, map name, tick rate) or reject
  (version mismatch / server full, human-readable reason). Heartbeats; 5 s timeout;
  explicit disconnect message on quit.
- **Client → server @ 60Hz:** input packet = sequence number + buttons bitmask +
  wish direction + yaw/pitch, carrying the last 3 inputs redundantly so one lost
  packet costs nothing. Server consumes at most one new input per tick, reuses
  the last input if starved.
- **Server → client @ 60Hz:** full-state snapshot — tick, all players (pos,
  view angles, health, weapon, crouch/dash flags, frags), all rockets, pickup
  availability, and the recent-events window. < 1 KB at 8 players; no delta
  compression needed.
- **Events without a reliable channel:** one-shot events (kills, global sound
  cues, join/leave) get sequence ids and ride in snapshots for ~15 ticks; the
  client dedupes by id.
- **Client rendering time:** remote entities interpolate between the two newest
  snapshots (~one snapshot interval behind); the local player renders from the
  newest snapshot directly for responsiveness.

## Renderer

OpenGL 3.3 core. One main pipeline: vertices = position + normal + color;
uniforms = MVP, directional light dir/color, ambient, fog color/density. That
shader *is* the art style: flat-shaded faces + lambert + distance fog.

- Static arena mesh built once from map boxes (24 verts per box, face normals).
- Players: two colored boxes (body + head), per-player color, simple speed-based
  bob; first-person viewmodel from boxes in the corner.
- Rockets: small emissive elongated boxes.
- Particles: CPU pool, camera-facing quads — explosions, muzzle flash, hit sparks,
  rocket smoke trail.
- HUD via stb_easy_font: health, weapon name, frag counter, kill feed, crosshair,
  hitmarker flash, Tab scoreboard, connection-state messages.

## Audio

Hand-rolled mixer feeding one SDL3 audio stream (float32 stereo, 48 kHz). All
sfx **procedurally synthesized at startup** (noise bursts, sine/saw sweeps,
envelopes) — zero asset files. Sounds: rifle shot, rocket launch, explosion,
jump, dash, slide, pickup, hurt, death, respawn. 3D = distance attenuation +
stereo pan from listener yaw. Mutex-guarded voice list (audio thread + game
thread).

## Error handling

- SDL / GL context / shader compile failures → fatal with clear message.
- Net: bounds-checked deserialization everywhere; unknown/oversized packets
  dropped; connect rejections carry reasons; timeouts detected both sides.
- Dedicated server: SIGINT/SIGTERM (and console Ctrl-C on Windows) → notify
  clients, clean exit.

## Testing

- `tests/` binary with a tiny hand-rolled assert harness (no framework):
  - math (vec/mat operations, ray-AABB intersection),
  - packet serialize → deserialize round-trips incl. truncation/fuzz cases,
  - movement/collision: fixed input sequences → expected trajectories (epsilon),
    slide/step-up cases, splash damage falloff.
- Netcode soak: `--bot` flag connects a headless scripted client; run a dedicated
  server + several bots. Bots double as target practice.
- Manual: `--host` + a second `--connect localhost` client on one machine.
- Windows: MinGW cross-compile must succeed; MSVC build documented in README.

## Milestones

1. **Skeleton** — CMake + SDL3 window + GL context + game loop + math + input.
2. **World** — map load, arena mesh, fly camera, renderer with light/fog.
3. **Movement** — collision, Quake physics, jump/dash/slide, feel-tuning pass.
4. **Netcode** — sockets, protocol, connection, input/snapshot loop, interpolation
   (host + join on localhost).
5. **Combat** — weapons, damage, health, rockets + splash, respawns, pickups.
6. **Game feel** — audio synth + mixer, particles, HUD, scoreboard, kill feed, menu.
7. **Ship** — dedicated mode polish, bots, tests green, Windows cross-build verified,
   README.

## Amendments

**2026-07-03** — Client-side movement prediction, originally a v1 non-goal,
is now implemented. Snapshots replicate all movement-internal player state,
the server consumes one queued input per tick per client (matching the
client's paced 60 Hz input sends), and the client replays unacknowledged
inputs through the shared `player_move` — reconciling against every
snapshot. Remote-player interpolation renders one snapshot interval behind
as originally specified. Protocol is v4 with a 4 KB packet cap. The event
ring grew to 64 entries and snapshots carry the 16 newest events. Movement
sounds (jump, dash, slide, and a new landing thud) are reported by the
simulation itself via `Player::move_sound`.
