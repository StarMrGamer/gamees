# Headless tooling

Everything here runs without a window, a GPU, a second machine, or a human at
the keyboard. That is the point: the interesting parts of this project are a
simulation and a netcode, and neither needs to be rendered to be checked.

Build once, then use the binary:

```sh
cmake --build build
./build/arena --help
```

## The loop to use

For any change to `src/game/`, `src/client/client.cpp`, or a map:

```sh
cmake --build build && ./build/arena_tests        # 1. unit tests
./build/arena --simulate --map maps/de_dust2.txt  # 2. does it still play?
./build/arena --netcheck                          # 3. does the wire still work?
./build/arena --bench --map maps/de_dust2.txt     # 4. did it get slower?
```

All four exit non-zero on failure, so they chain with `&&`.

## `--simulate` - deterministic gameplay run

Drives the authoritative simulation with scripted inputs. No SDL, no sockets.

```sh
./build/arena --simulate --map maps/arena.txt --ticks 1800 --players 8
./build/arena --simulate --ticks 600 --json      # machine-readable
./build/arena --simulate --trace-every 30        # JSON lines, one per sample
```

It fails the run (exit 1) on the two things that are never acceptable:

- **`nan`** - a player position, velocity or health went non-finite.
- **`stuck`** - a player's hull sat *inside* solid geometry for a quarter
  second. Note this is a penetration test, not a "did they move" test: a bot
  holding forward into a wall is legitimately motionless, so distance travelled
  would false-positive constantly.

`state_hash` is a quantised fingerprint of the final simulation state. It is
stable for a given map + seed, which makes it the fastest way to answer "did
this refactor change gameplay at all?":

```sh
./build/arena --simulate --json | grep -o '"state_hash":[0-9]*'   # before
# ... make the change, rebuild ...
./build/arena --simulate --json | grep -o '"state_hash":[0-9]*'   # after
```

Same hash means the change was behaviour-preserving. A different hash means it
was not - which is either the bug or the point, but now you know which.

Vary `--seed` to explore different movement; each seed shifts every bot's phase
and firing cadence, so a handful of seeds covers far more of the map than one.

## `--netcheck` - protocol round trip

Runs a real server and real clients over UDP loopback inside one process, on a
virtual clock, and checks that clients get accepted, snapshots flow, and
client-side prediction tracks the server.

```sh
./build/arena --netcheck --players 8 --seconds 5 --json
```

This is the only automated coverage of the actual wire format. `--simulate`
calls `game_tick` directly and would not notice a broken serializer.

## `--bench` - hot path microbenchmarks

```sh
./build/arena --bench --map maps/de_dust2.txt
```

Reports ns/op for `map_box_overlap`, `map_ramp_surface`, `ray_map`,
`move_slide`, and a whole 8-player `game_tick`. Always benchmark against
`de_dust2.txt`, not `arena.txt` - the default map has 45 boxes and hides every
scaling problem. `de_dust2` has ~2000 primitives, which is where the spatial
grid in `src/game/map.cpp` earns its keep.

## `--check-map` - map validation

```sh
./build/arena --check-map maps/de_dust2.txt
./build/arena --check-map maps/de_dust2.txt --json
```

Voxelises the map and flood-fills the empty space inward from the boundary. If
that flood reaches a spawn, players can walk out of the level. Exits non-zero
when a spawn leaks.

## `arena_tests` - filtering

```sh
./build/arena_tests              # everything
./build/arena_tests --list       # names, one per line
./build/arena_tests ramp         # only tests whose name contains "ramp"
```

## Writing a test that actually catches something

The suite has a hand-rolled harness (`tests/test_harness.h`): `TEST(name)`,
`CHECK`, `CHECK_NEAR`, `CHECK_EQ_INT`. Two habits are worth keeping:

- **Prove the test fails without the fix.** Temporarily revert the change and
  confirm the new test goes red. A regression test that passes against the bug
  is worse than no test, because it certifies the bug as fixed forever.
- **Guard against a vacuous pass.** A property test over random samples should
  assert that the samples actually hit the interesting case - see
  `move_slide_never_ends_inside_solid_geometry`, which counts how many of its
  starting positions were genuinely dangerous and fails if too few were.
