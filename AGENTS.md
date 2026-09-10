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
```

All of them exit non-zero on failure. Two things worth knowing:

- `--simulate --json` prints a `state_hash` that is stable for a given map and
  seed. Compare it before and after a refactor to find out whether gameplay
  actually changed. It is the cheapest possible regression check.
- Benchmark against `maps/de_dust2.txt`, never `maps/arena.txt`. The default
  map has 45 boxes and hides every scaling problem; de_dust2 has ~2000.

## Performance notes

`Map` carries a uniform XZ grid (`MapGrid`) built at load time, and
`map_box_overlap` / `map_ramp_surface` / `ray_map` all go through it. Keep `Map`
a POD - it is `memset` on load and copied by value - so no `std::vector` or
owning pointers in it. If the grid ever fails to build the queries silently fall
back to a full scan, which stays correct and only costs speed.
