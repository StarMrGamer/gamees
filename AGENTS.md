# Agent Instructions

This repository is the `arena` C++20 LAN arena FPS prototype. Treat the files in
`docs/superpowers/` as the source of truth for product goals, architecture, and
game-feel expectations:

- `docs/superpowers/specs/2026-07-02-arena-fps-design.md`
- `docs/superpowers/plans/2026-07-02-arena-fps-contract.md`

When implementing gameplay, prefer the approved goals over generic FPS behavior.
Movement should aim for the design spec: Quake-style strafe jumping, bunny hop
momentum, air control with the wish-speed cap, rocket jumping, dash, crouch-slide,
jump buffering, and grounded step-up collision.

Keep changes consistent with the existing architecture:

- Shared simulation lives in `src/game/` and must stay free of SDL, rendering,
  audio, and platform dependencies.
- Networking is authoritative-server-first unless the user explicitly asks to
  change that design.
- Gameplay constants belong in `src/game/tuning.h`.
- Tests use the hand-rolled harness in `tests/`; add focused coverage for physics,
  collision, weapons, snapshots, and protocol changes.

Build and verify with:

```sh
cmake --build build
./build/arena_tests
```

`./build/arena_tests <substring>` runs a single test; `--list` prints the names.

## Verifying gameplay without a screen

This is a game, but none of it needs a window to be checked. `docs/AGENT_TOOLING.md`
is the full reference; the short version, after any change to `src/game/`,
`src/client/client.cpp`, or a map:

```sh
cmake --build build && ./build/arena_tests
./build/arena --simulate --map maps/de_dust2.txt   # gameplay: NaN + wedged-player check
./build/arena --netcheck                           # protocol round trip over loopback
./build/arena --bench --map maps/de_dust2.txt      # hot path timings
./build/arena --check-map maps/de_dust2.txt        # can a player fall out of the level?
```

All of them exit non-zero on failure. Two things worth knowing:

- `--simulate --json` prints a `state_hash` that is stable for a given map and
  seed. Compare it before and after a refactor to find out whether gameplay
  actually changed. It is the cheapest possible regression check.
- Benchmark against `maps/de_dust2.txt`, never `maps/arena.txt`. The default
  map has 45 boxes and hides every scaling problem; de_dust2 has ~2000.
- `--check-map` walks the level with the real physics and reports any spot a
  player can reach and fall out of the world from. Run it after editing a map
  or touching movement/collision - a change to step height or hull size can
  open leaks in a map that was previously sealed.
- Do not "fix" walk leaks by dropping barrier boxes at the leak columns. It
  drives the count to zero while walling off doorways and ledges players are
  meant to use, and the reachable-position metric is too coarse to notice.
  `docs/AGENT_TOOLING.md` has the full account.

## Bots

`src/ai/agent.h` turns a `GameState` into a `PlayerInput` and nothing else - no
sockets, no rendering, no wall clock. That is deliberate: the same code drives
the networked `--bot`, the headless `--eval` harness, and (later) the
demonstrations a learned policy trains against. Keep it that way, or those three
stop agreeing about what the bot does.

The navmesh in `src/game/nav.h` is **generated from the reachability walk, not
from the geometry**. `map_walk_reachable()` steps a real player hull around the
level and resolves each step with the real drop physics, so every node is a
position a player can stand in and every edge is a step the movement code
actually accepted. A mesh derived from brushes instead would confidently route
a bot through a gap narrower than its own shoulders. Anything that needs to
know where a player can go must go through that one search.

It is built by `map_build_nav()` on request, never by `map_load()`: the walk
costs ~120 ms on de_dust2 and only bots need the result. Queries fail cleanly
when it is absent and callers fall back to steering straight at the target.

Two properties that are load-bearing and easy to break:

- **Nodes are quantised in Y as well as XZ.** A tunnel and the bridge over it
  are the same place in plan view; merging them puts a waypoint six metres
  above the bot's head and it believes it has arrived.
- **Edges are directed.** The walk only discovers a step in the direction it
  travelled, and a drop off a ledge is not reversible. The return leg is only
  added when the climb back is within `REACH_CLIMB_HEIGHT`, or bots grind
  against cliffs forever.

Measured: de_dust2 is 1712 nodes / 6770 links, built in 121 ms, and A* over it
costs ~10 us. Turning it on took the bot from 6.2 to 11.2 frags per match there.

`AGENT_SIMPLE` is the original hold-forward-and-spray bot. Do not delete it - it
is the floor every later claim is measured against.

The bot is **omniscient for navigation and blind for aiming**: with nobody in
sight it walks toward the nearest living enemy, but firing still requires a real
line-of-sight raycast. The split is deliberate and worth preserving. Without the
navigation half, two bots on de_dust2 never meet and 30 of 30 matches time out;
without the aiming half, it is a wallhack and useless as a training opponent.

### Difficulty

Four tiers, picked by `agent_config(AgentSkill)` (names from
`agent_skill_name`, parsed by `agent_skill_parse`): `easy`, `normal`, `hard`,
`demon`. `--bot-skill` and `--eval-a`/`--eval-b` take the names; the live bot
defaults to `normal`. `demon` is the original bot - 9.0 rad/s, 0.10 s reaction,
no FOV - preserved as the top of the ladder so every earlier measurement stays
comparable. It is not meant to be fair.

Three knobs do the work, and each is load-bearing:

- **`fov`** is the half-angle the bot may *acquire* targets in. `demon` uses
  `PI` and can never be flanked; the others cannot see behind themselves, which
  is most of what makes the lower tiers beatable. Two rules follow. The dot
  product is checked before the raycast because it is cheap and rejects most
  candidates. And a recent **hit** bypasses the cone (`provoked_by`, 1.5 s):
  being shot from behind tells a human roughly where it came from, so a narrow
  FOV must not turn the bot into something that can be shot in the back for free.
- **`aim_error`** is the *size* of the persistent aim wander, not per-tick noise.
  Pure jitter averages out over a burst and barely costs a shot; an offset that
  persists for a few tenths of a second is what actually misses. `aim_drift_rate`
  is how fast it wanders and `agent_think` scales the kick by `aim_error * rate`
  - scaling by the rate alone made the tiers backwards (more error the faster it
  drifted) and flatlined easy/normal/hard at the same damage per shot.
- **`reaction`** is seconds of unbroken sight before the trigger unlocks.

`agent_config_demon_handicapped(0.0f)` still equals `agent_config(SKILL_DEMON)`
exactly, so a mirror match is a real mirror. Handicap scales within a tier and
is applied on top of it in `--eval`, so a ladder can be coarse or continuous.
Tests to keep honest: `skill_tiers_are_ordered_by_strength` (every knob moves
monotonically), `skill_names_round_trip_and_reject_junk`,
`fov_blinds_the_lower_tiers_from_behind`, and
`being_shot_pulls_the_bot_onto_an_out_of_view_enemy`.

### Difficulty, and what made it unbeatable

Played against a human, the first version was unbeatable, and the reason was
structural rather than a number being too high: **target acquisition raycast
from the eye in every direction**, so the bot could not be flanked. It also ran
a 0.10 s reaction (a human's is ~0.25 s before the aim has even moved) and
9.0 rad/s of sustained aim sweep, which is 515 deg/s.

`AgentConfig::fov` fixes the structural half. `EV_HIT` carries the attacker, so
a bot that is shot from outside its cone still turns round - being hit is
information a human gets, and without modelling it a narrow FOV makes the bot
free to flank rather than merely beatable.

The tiers are easy / normal / hard / demon. **Demon is the original bot, kept
deliberately as a fixed yardstick so earlier measurements stay comparable; it
is not meant to be fair.** The live `--bot` defaults to `normal`.

Two measurement traps here, both paid for:

- **Aim error has to be correlated over time.** Independent per-tick jitter
  averages out across a burst, so a bot with "error" still lands nearly every
  shot. The error is a slow drift instead.
- **Scale the drift by its amplitude, not its rate.** Keying the kick to
  `aim_drift_rate` alone made the wander depend on how fast it moved rather
  than how large it was, which put the tiers backwards - `hard` had more aim
  error than `easy` - and made easy, normal and hard measure identical at
  0.25-0.28 damage per shot. Against a fixed `simple` opponent the tiers should
  read 0.20 / 0.26 / 0.47 / 0.78 damage per shot.

Bot-vs-bot is a weak proxy for human difficulty: two bots approach each other
head-on, so field of view and turn rate barely matter between them. Check a
tier against the fixed `simple` bot for aim quality, and against a human for
whether it is fair.

### Movement tech, and the one that does not work

The bot uses dashes, slides, slide-jumps, wall jumps and double jumps. These
are *impulses* (+12, +2, +3 m/s) and they are what makes it fast: average speed
on arena went 7.7 -> 13.2 m/s, and de_dust2 matches reach the frag limit in
396 s instead of 514.

Jump timing matters: jump, dash and air jump are **edge-triggered** in
movement.cpp (`jump_down && !p.jump_held`). Holding the bit does nothing after
the first tick, so the bot has to release for a tick between presses.

**Air strafing is deliberately absent, and that is measured, not an omission.**
This engine caps air acceleration on the wish direction's *projection* onto
velocity (`AIR_WISH_CAP`, 1 m/s), so the only gain available is perpendicular -
it curves you rather than speeding you along. Over 20 s on flat ground:

| strategy                | avg speed | net travel |
|-------------------------|----------:|-----------:|
| walk forward            | 8.00 m/s  | **159.9 m** |
| hop, forward held       | 8.00 m/s  | 159.9 m |
| wish perpendicular      | 11.12 m/s | 42.4 m (a circle) |
| half-beat, 20deg swing  | 5.65 m/s  | 99.3 m |
| half-beat, 45deg swing  | 2.28 m/s  | 4.1 m |

There is no compounding here the way there is in the games the technique comes
from, so a bot that air strafes arrives *later* than one that holds forward.
Making it pay would mean retuning the movement itself, which changes the game
for human players too - a design decision, not a bot change.

A related trap already paid for: the first version of bunny hopping jumped
whenever the bot was grounded and moving. That left it airborne 90.6% of the
time averaging 7.7 m/s - slower than the bot that merely walked - because a
player in the air can barely accelerate. Airborne time is only worth having if
something is being gained during it.

Two things learned the hard way while tuning the steering, both measured:

- **Walls and pits are not the same constraint.** Running into a wall is
  harmless - the collision code slides you along it - so openness is only a
  preference. Walking off the map is not recoverable, so footing is the only
  hard veto. Treating both as hard vetoes made every direction illegal in
  de_dust2's corridors and cost 78% of the bot's frags.
- **Look-ahead has to scale with speed.** The bot bunny hops; a fixed 1.6 m
  probe is a tenth of a second of warning at 12 m/s. Scaling it took falls out
  of the world from 16 per 30 matches to 0.

## Weapon feel

`src/client/gunfeel.h` owns everything about firing that must not wait for the
server. The original client drove **every** weapon effect from the server's
event stream, so pressing fire produced nothing - no flash, no tracer, not even
a sound - until a snapshot came back: a tick of server latency plus the round
trip. That is what "the gun feels unresponsive" was, and no amount of animation
fixes it, because the animation was waiting on the same packet.

Three rules hold this together:

- **The local fire rate must match the server exactly.** `gunfeel_weapon_interval`
  and `gunfeel_weapon_sound` mirror `weapons.cpp`; if they drift, the player is
  watching a weapon that is not the one they are shooting.
  `gunfeel_fire_rate_matches_the_server` drives both and compares.
- **Suppress the server's echo of your own shot.** `handle_events` skips sound
  events whose shooter is the local player, or every shot plays twice - once
  immediately and once a fraction of a second later, which sounds worse than
  the latency did.
- **View punch goes on the camera, never on the input.** Recoil that moved the
  aim would change the balance and would have to live in the shared simulation
  for prediction to match. As a camera-only effect it costs nothing and risks
  nothing.

Tracers travel. The original spawned the whole line at once - up to 36 dots
hanging in the air for 60 ms - which reads as a laser rather than a round in
flight, and was ruinous for the 2048-particle pool: a shotgun blast traced seven
pellets at 36 dots each, **252 particles from one trigger pull**, starving
explosions and sparks of slots. A streak is launched with real velocity instead,
each dot given exactly enough life to cover its own head start so the whole
thing arrives together and vanishes into the surface. A blast is now under 80.

`Particle::delay` exists so a wall impact lands as the streak arrives rather
than a few frames early. A delayed particle must not age, move *or* draw -
getting any one of those wrong makes the debris drift before it appears.

Geometry impacts are predicted locally; hits on a **player** are not. The map is
the same on every machine, so a wall puff is safe; a puff on a shot that turns
out to have missed is worse than one that arrives late.

The punch is a spring, and a stiff one: it needs sub-stepping at 1/240 s, since
a single 0.1 s step diverges. Clamping the frame time is not enough - a hitch
still explodes it. Measured peaks under sustained fire are 2.6 degrees (rifle)
to 4.1 (rocket), bounded, settling to exactly zero.

## Performance notes

`Map` carries a uniform XZ grid (`MapGrid`) built at load time, and
`map_box_overlap` / `map_ramp_surface` / `ray_map` all go through it. Keep `Map`
a POD - it is `memset` on load and copied by value - so no `std::vector` or
owning pointers in it. If the grid ever fails to build the queries silently fall
back to a full scan, which stays correct and only costs speed.

The index is 2D, so a cell in a tall map holds its whole column. Each bucket
entry therefore packs the primitive's vertical extent, quantised to
`MAP_GRID_Y_BANDS`, next to its index: a candidate whose bands miss the query's
is dropped from the entry word itself, without the random read into the
primitive array that is the expensive part. On de_dust2 that is 53% of them.
Two rules follow. A query that genuinely wants the whole column
(`map_ramp_surface`) must use the unfiltered walk rather than pass infinite
bounds - it would otherwise pay for a filter that can never reject. And the
filter is only consulted when `MapGrid::use_bands` is set, because below
`MAP_GRID_BAND_MIN_PRIMS` the geometry is cache-resident and the read it avoids
was free; on the 45-box default map, filtering cost about 5% and saved nothing.
Both paths are held to the same answers by `map_grid_band_filter_matches_unfiltered`.

Memory is the binding constraint on these hot paths, not arithmetic. Two
measurements worth keeping in mind before optimising here: hoisting the three
reciprocals out of `ray_aabb` into a per-ray struct was worth 2.3x on `ray_map`
(one de_dust2 ray tests ~50 primitives and was paying ~150 divisions for a
direction that never changes), while a Z-order sort of the primitive arrays -
which looks like it should help and did help before the band tags existed -
measured slower afterwards and was removed. Benchmark, do not reason.

`Map` is over a megabyte. Heap-allocate it rather than putting one on the stack
alongside another (`game_client.cpp` holds a `Map` and a `Client`, which carries
its own); the Windows link reserves an 8 MB stack for the same reason.

There are three solid primitives: `MapBox`, `MapRamp` (bounds plus an explicit
surface plane - it is the only walkable slope, so imported terrain becomes
ramps too), and `MapBrush` (a convex solid stored as half-spaces, used for
angled geometry that a box cannot express). Anything that walks the world must handle all three - adding a
primitive to `map_box_overlap` and `ray_map` but forgetting `map_check.cpp`
makes the leak checker treat that geometry as empty space. Brushes cache their
bounds specifically so both collision paths can reject on six compares before
touching the plane loop; keep that early-out.
