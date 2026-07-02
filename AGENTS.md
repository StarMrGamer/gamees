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
