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
