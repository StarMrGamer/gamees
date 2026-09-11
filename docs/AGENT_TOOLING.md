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

### Import fidelity, and why a map feels wrong

The engine has three solid primitives: axis-aligned boxes, ramps, and convex
brushes. When importing a real Source map each brush lands in one of those, and
`--import-map` reports the split:

```
fidelity: 974 axis-aligned, 324 ramps, 710 angled
  angled: 683 kept exactly as convex brushes, 27 carved into boxes
terrain: 3984 displacement cells -> 3132 merged ramps (512 skipped as 3D skybox)
```

A **convex brush** is stored the way the source format stores it - as a set of
half-spaces, `dot(n, p) <= d`. That is why an angled wall survives as an angled
wall. Collision is an expanded-plane test for boxes and an interval clip for
rays; rendering recovers each face by clipping a quad on that plane against
every other plane.

This matters more than it sounds. Before brushes existed, an angled solid was
emitted as its *bounding box*: on de_dust2 those 736 brushes were only **65%
solid on average**, so a third of every emitted block was invented geometry.
Diagonal walls filled in their corners and the rooms behind them became
unreachable - which is what "I can't go inside that area" turned out to be.

Measured on de_dust2, each step of the fix:

| approach | boxes | solid cells | reachable |
|----------|-------|-------------|-----------|
| bounding boxes | 1673 | 1849482 | 47635 |
| carved into boxes (0.25 m) | 3617 | 1818232 | 48904 |
| **convex brushes** | **1910** | **1795748** | **49137** |

Brushes win on every axis *and* use half the boxes of carving. Carving stays as
the fallback for the ~27 solids too complex for `MAX_BRUSH_PLANES` (cylinders
and arches, which run to 23+ faces).

Two traps worth knowing about, both of which bit during this work:

- **Plane data needs real precision.** Box corners are fine at three decimals;
  plane normals are not. A normal written as `-0.707` no longer matches the
  bevel plane it is meant to be, so `map_brush_finalize()` tries to append a
  duplicate, overflows `MAX_BRUSH_PLANES`, and rejects the brush - and a
  rejected brush is a hole in the level. Planes are written at six decimals,
  and the importer validates each brush against the *round-tripped* values.
- **Validate with the loader's own code.** The importer calls
  `map_brush_finalize()` on a probe rather than reimplementing the same checks.
  A lookalike check that disagrees with the parser silently drops geometry.

`bevel planes`: every brush carries the six axis-aligned planes of its own
bounding box. They are redundant for point-in-solid, but they are what keeps
the swept-AABB test tight - without them, expanding the angled planes by the
player's extent rounds the brush's edges outward and you collide with thin air.

### What a Source BSP actually contains

Three separate things bit here, and all of them presented as "the map feels
wrong" rather than as an error:

**Terrain is not brushes.** Ground in a Source map is a *displacement*: a base
quad face plus a grid of per-vertex offsets, living in `LUMP_DISPINFO` and
`LUMP_DISP_VERTS`. A brush reader sees none of it. de_dust2 keeps almost all of
its open ground there - 69 patches, 3984 cells - so the level imported with its
outdoor areas simply missing, which is what players fell through. Importing
them tripled the reachable area, 43957 -> 127413 standing positions.

Only a ramp is walkable, so terrain cells become ramps (bounds plus best-fit
plane), with neighbouring cells merged where they share a plane. Merge
*neighbours in the displacement's own grid*, never "cells whose bounding boxes
overlap" - the latter fuses patches from opposite ends of the level into one
enormous slab.

**Not every brush is a wall.** Filter on `CONTENTS_SOLID`. A brush with
`contents != 0` can be a player-clip (an invisible wall the mapper used to shape
movement), glass, a grate, an area portal or an origin marker. de_dust2 has 67
player-clips and 8 glass volumes; imported as solid they become blocks standing
in the level that no player ever saw.

**The 3D skybox is in the same lumps.** Source builds distant scenery as a
separate miniature region hundreds of metres from the map. Its brushes,
displacements and ramps all arrive with everything else. Bound the import to
the playable area - and derive those bounds from the geometry list that has
already been pruned of it. `filter_disconnected()` only prunes *boxes*, so
computing bounds from ramps or brushes lets a single stray skybox slope stretch
them across the level and every skybox patch back in.

### `--probe "X Y Z"` - what does the engine see here?

```sh
./build/arena --probe "35.85 -4.86 -3.56" --map maps/de_dust2.txt
```

Takes exactly the format the client's **P** key copies to the clipboard, so a
position reported from play can be pasted straight in. It reports whether the
player hull is inside solid (and the nearest way out), whether the spot is
standable, floor and ceiling distances, whether dropping from there leaves the
world, and how much geometry is nearby.

It separates *resting contact* from *trapped*: standing exactly on a surface
overlaps it by a fraction of a millimetre, and a probe that calls that "stuck"
sends you chasing a bug that is not there. Anything clearing with a few
millimetres of lift is reported as touching, not trapped.

Keep its solidity test a call into the same code `move_slide()` uses. When it
had its own lookalike copy of the rule it reported a position as stuck that the
simulation was perfectly happy with, which sent a real diagnosis down a false
trail.

### Ramps have a top *and* a bottom

A ramp's solid body runs from its own floor up to its surface. Testing only
"is the surface above me" turns every ramp into a column of rock reaching down
to the void, which buries whatever the terrain arches over - on de_dust2 that
included ground-level areas with spawns in them, reported from play as being
stuck in mid-air.

Terrain also *stacks*, so the plain "highest surface in this column" query
returns a hillside overhead rather than the ground underfoot. Grounding and the
step-up snap use `map_ramp_surface_near()`, which only considers surfaces within
stepping reach of the caller; blocking uses `map_ramp_blocks()`, which requires
the ramp's solid body to actually overlap the player hull.

Flat terrain becomes boxes, not ramps. Roughly a quarter of de_dust2's
displacement patches are level to within 5 cm, and a box is cheaper to collide
against and leaves the ramp budget for geometry that genuinely slopes.

Two rules make that safe. The box top goes at the patch's **lowest** corner,
never its mean or highest: a box that pokes above the terrain it replaces
swallows the player standing on that terrain, which showed up immediately as
positions reported from play turning "inside solid". And the flatness threshold
is also the worst-case lip left against neighbouring patches, so it stays small
- measured on de_dust2, 5 cm converts 714 patches and costs 0.6% of the
reachable area, where 20 cm converts 1552 but costs 3.4%.

Terrain cells are given an adaptive skirt for the same reason. A thin one leaves
a void under every slope that players drop through; a thick fixed one buries the
ground below. Each cell is instead deepened individually, down to just short of
whatever is beneath it, leaving a player's height of headroom. Measured on
de_dust2:

| terrain skirt | reachable | leaks |
|---------------|-----------|-------|
| 0.6 m fixed | 82926 | 285 |
| 4.0 m fixed | 110928 | 212 |
| adaptive | 128074 | 191 |

### What NOT to do about walk leaks

`de_dust2` still reports some walk leaks, at the outer edges where the level's
seal is genuinely missing.

The obvious fix is to drop a barrier box at every leak column. **Do not.** It
was tried: 27 barriers, leaks went to zero, and the reachable-position count
only fell 1.6%, which looked harmless. In play it was not - the barriers stood
in doorways and at the lips of ledges and walled off places you are supposed to
be able to walk into. The metric was too coarse to catch it and the change was
reverted.

Falling out is already handled: `player_move()` returns anyone below
`map.void_y` to a spawn. A leak costs you a teleport, not a crash. Fixing leaks
properly means restoring missing geometry - as importing displacements did,
taking them from 259 to 174 while *adding* play area rather than removing it.

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
