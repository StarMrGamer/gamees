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
