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

Two checks run, and either one failing exits non-zero.

**Seal check** (`leaked`) voxelises the map and flood-fills the empty space
inward from the boundary. If that flood reaches a spawn, the level is open at
spawn height.

**Walk check** (`walk_leaks`) is the stronger one, and the one that finds real
bugs. It breadth-first searches every standing position a player can reach from
a spawn, resolving each candidate step with the actual `move_slide()` physics
rather than a voxel approximation. A leak is a reachable spot from which the
next step drops the player below `map.void_y` - somewhere you can walk to and
fall out of the world.

Using the real physics matters. The player hull is 0.6 m wide, so a 0.4 m slot
in the floor is *not* a hole - you straddle it. A point-sampled voxel check
reports slots like that as leaks, and chasing those false positives wastes more
time than the check saves. `map_check_ignores_a_gap_narrower_than_the_player`
pins this down.

Falling out is not fatal - `player_move()` returns anyone below `void_y` to a
spawn - so a leak is a level bug, not a crash. It still means players can walk
off the world and get teleported mid-fight.

### Sealing a map that leaks

`maps/de_dust2.txt` was imported from a Source map without its outer terrain
and skybox, which left 259 reachable spots where you could step off a floor
edge into open air. The fix was mechanical:

1. Run the walk check to get the leak columns.
2. Emit a barrier box at each one, tall enough (2 m) that a 1.225 m jump cannot
   clear it, merging adjacent columns into rectangles.
3. Re-run - barriers change reachability, so this iterates. de_dust2 converged
   in two passes: 259 leaks -> 7 -> 0, for 27 added boxes.

Check the reachable-position count before and after. It should drop by a
percent or two (the lip columns you just blocked). A large drop means a barrier
walled off a corridor and cut real play space out of the map - de_dust2 went
47635 -> 46875, or -1.6%.

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
