# arena

`arena` is a C++20 LAN-oriented arena FPS prototype built from the approved
project contract in `docs/superpowers/plans/2026-07-02-arena-fps-contract.md`.

## Build

Standard contract commands:

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/arena_tests
```

This environment currently has SDL3, `g++`, and Ninja installed. If `cmake` is
not on `PATH`, install it or place the contract-approved CMake release at
`tools/cmake/bin/cmake`.

### Windows

```sh
cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

**`build-win/arena.exe` is the only file needed to run the game.** No SDL3.dll,
no `maps/` directory, no MinGW runtime beside it. SDL is linked statically, the
MinGW runtime with `-static`, and every map under `maps/` is compiled into the
binary at build time. Verified with `objdump -p`: the only DLLs it imports are
ones that ship with Windows.

That costs size - the executable is about 19 MB - and it is the whole point:
one file to copy, nothing to install.

Maps on disk still win over the baked copies, so editing `maps/arena.txt` next
to the binary takes effect without a rebuild. The one file the game writes is
`arena.cfg`, created on demand when settings change.

## Run

```sh
./build/arena --host
./build/arena --connect 127.0.0.1:27950 --name player2
./build/arena --connect 127.0.0.1:27950 --name player2 --sensitivity 3.0
./build/arena --connect 192.168.1.42:27950 --name player2 --class scout --jump mwheelup
./build/arena --connect 127.0.0.1:27950 --name player2 --jump mwheelup --doublejump space
./build/arena --dedicated --port 27950
./build/arena --host --bots 3 --bot-skill normal
./build/arena --bot 127.0.0.1:27950 --name bot1
./build/arena --bot 127.0.0.1:27950 --name bot1 --bot-skill hard
```

**`--bots N` is the way to play against them.** The bots run as threads of your
own game, so closing it is guaranteed to take them with it. Launching `--bot` as
a separate background process works too, but that bot outlives the window that
spawned it and turns up in your next session - it now gives up and exits ten
seconds after its server stops answering, rather than lingering forever.

The bot has four difficulty tiers: **easy, normal, hard** and **demon**. Each
turns faster, reacts sooner, aims straighter and sees further round than the one
below it, so lower tiers can be flanked and higher ones punish you for standing
still. `--bot-skill` picks the tier and defaults to `normal`; `demon` is the
original, all-seeing bot kept as a fixed yardstick rather than a fair opponent.

Bot difficulty is `--bot-skill easy|normal|hard|demon`, defaulting to `normal`.
**`demon` is not a fair opponent** - it has a 360-degree field of view and
sweeps its aim at 515 deg/s - it exists as a fixed yardstick to measure
against. The playable tiers have a real field of view (120-160 degrees), so
they can be flanked, and human-plausible reaction times (0.45 / 0.30 / 0.22 s
against a human's ~0.25 s). Being shot still turns a bot round, whichever
direction it came from.

The bot plays properly: it picks targets it can actually see, leads rockets,
circles while it shoots, closes to shotgun range or holds rifle range depending
on its class, chains dashes into slide-jumps to move at 13 m/s against a 8 m/s
walk, wall jumps, goes for health when hurt, paths around geometry rather than
into it, and does not walk off the map. It beats the old spin-and-spray bot 40
matches to 0.

Maps are looked up relative to the current working directory and, failing
that, relative to the executable (and up to three parent directories), so the
binary can be launched from anywhere — including a `dist/` folder or the
`build/` directory.

When hosting, the game listens on all local interfaces and prints detected LAN
join commands in the terminal. Other players can join with `--connect IP:27950`
or by choosing Join by IP from the startup menu. Internet play still requires
the host's router/firewall to allow UDP on the chosen port.

**Sniper** is the fourth class. It carries the only weapon that reaches across a
whole map, and right mouse scopes it - a client-side view change the simulation
never sees, so it costs nothing on the wire, with the aim sensitivity scaled to
match so the same mouse travel covers the same distance on screen.

One hit kills a Scout or another Sniper outright, leaves a Ranger on 10 health
and a Tank on 42. In exchange it is the most fragile class in the game (80
health, and it takes 8% extra damage), it fires once every 1.45 s, and its
sustained damage is *below* the rifle's - 62/s against 75. All of its power is
in the burst, so a miss costs more than any other weapon's.

The rifle and the LMG have **no view punch**. They are the sustained-fire
weapons, and a camera that moves on every shot fights your tracking instead of
rewarding it; they still get viewmodel recoil and a muzzle flash, so the shot is
just as visible. The shotgun, rocket and sniper do kick, hardest of all on the
sniper.

Firing is predicted locally: the muzzle flash, tracer, report and view punch
all happen on the frame the button goes down, rather than when the server's
snapshot comes back. The server stays authoritative for damage - this only
decides when you see and hear your own gun. View punch is applied to the camera
only, so recoil never moves where your bullets go.

Controls in the SDL client are WASD movement, Space jump by default, Left Shift
dash, Ctrl/C crouch, F fire, arrow keys look, 1 class primary, 2 rocket, 3/4/5/6
class switch (Ranger/Scout/Tank/Sniper), right mouse to scope with a sniper, and
Escape for the settings menu.

Bullets leave a tracer that actually flies - a short streak launched from the
muzzle at 200-420 m/s depending on the weapon, terminating in a dust puff timed
to land as it arrives. The shotgun's seven pellets get the shortest and slowest
streaks, since seven full-length tracers at once are a wall of light.

**F3** shows a performance overlay: frame time p50/p99, the CPU cost split into
sim/net, render and swap, how much of the frame was spent deliberately idle in
the frame limiter, and the vertex and draw-call counts. It also mirrors a line
to the terminal once a second, and `--perf-log FILE` writes the same figures as
CSV four times a second whether the overlay is up or not. Percentiles rather than
an average, because a 4 ms mean with a 40 ms hitch every second reads as
250 fps and feels awful.

Two map-inspection aids: **N** toggles noclip (fly through geometry, no gravity,
WASD plus Space/Ctrl for up and down, hold Shift to move faster), and **P**
copies your current position to the clipboard as `x y z` — holding it also shows
the figures on screen. Paste that straight into
`arena --probe "x y z" --map maps/<map>.txt` to see what the engine thinks is at
that spot. Noclip refuses to switch off while you are inside a wall,
since that would wedge you there permanently. Jump can be
changed to mouse wheel up/down in settings or with `--jump mwheelup` /
`--jump mwheeldown`; in mouse-wheel mode, Space no longer jumps.

The mid-air double jump has its own bind, separate from the ground jump, and it
only ever fires from that bind — the normal jump button never triggers it. It
defaults to Left Alt and can be moved onto its own key or wheel in the settings
menu or with `--doublejump lalt|space|mwheelup|mwheeldown`. Wall jumps stay on
the primary jump button. Ranger primary is rifle, Scout primary is shotgun, and
Tank primary is a fast low-damage LMG; each weapon has its own report and bullet
tracers, and shotgun pellets lose damage with distance, so the shotgun rewards
closing in.

Sensitivity, jump and double-jump binds are saved to `arena.cfg` when changed in
settings or when the client exits. The file is looked up relative to the current
working directory and then next to the executable (like maps), so bindings
persist no matter where the game is launched from. Set
`ARENA_CONFIG=/path/to/file.cfg` to use a different config file. Command-line
`--sensitivity`, `--jump` and `--doublejump` override the saved values for that
launch and are saved when the client exits.

## Importing maps

`arena` can convert maps from other engines into its own text format:

```sh
./build/arena --import-map path/to/map.map --import-out maps/my_map.txt
./build/arena --import-map path/to/map.vmf            # writes maps/<name>.txt
```

The source dialect is auto-detected. Three are supported:

- **Valve Source compiled `.bsp`** (`VBSP`) — the form that ships inside a game.
  Point it at your own installed copy, e.g. Counter-Strike: Source:
  ```sh
  ./build/arena --import-map "/path/to/Counter-Strike Source/cstrike/maps/de_dust2.bsp"
  ```
  Brush planes, materials and the entity lump are read. Non-solid volumes are
  skipped so they don't become walls: trigger volumes (`TOOLS/TOOLSTRIGGER`,
  e.g. buy zones, which otherwise seal spawn rooms), area portals, water and
  origins; `func_door` brushes are skipped too (there are no doors here, so
  keeping them would wall off entrances). Sloped brushes become the engine's
  `ramp` primitive and are genuinely walkable slopes. Geometry not connected to
  the main playable mass (the 3D skybox and other floating decoration) is
  dropped, and spawns embedded in geometry are discarded.
- **Quake/idTech `.map`** (TrenchBroom, Q1/Q2/Q3, or a Hammer export).
- **Valve Source `.vmf`** (Hammer's native format).

Each brush becomes the axis-aligned box of its convex hull, so slopes and
curved surfaces are approximated as blocks; `info_player_*` entities become
spawn points, `item_health*` become health packs, and texture names map to
muted colours. Useful options:

- `--import-scale <metres-per-unit>` — default `0.0254` (source units are inches,
  so a 72-unit player is ~1.8 m).
- `--import-max-boxes <n>` — cap on emitted boxes; the largest brushes win if a
  map exceeds it (default 4096, the engine's map box limit).

The result is an ordinary arena map, so it renders, collides and plays like any
other:

```sh
./build/arena --host --map maps/my_map.txt
```

## Map tools

- `ramp x y z w h d dir r g b` is a first-class map primitive: a wedge whose
  top slopes from `y` to `y+h` along `dir`, walkable like a real slope (used by
  the importer instead of stair-steps).
- `./build/arena --eval --map maps/arena.txt --matches 40` plays bot against bot
  headlessly and reports who won, at around 10000x realtime.
- Bots navigate with a waypoint graph built by walking the map the way a player
  does, so every route they follow is one the movement code actually accepts.
- `./build/arena --check-map maps/<name>.txt` voxelises the map and checks that
  no spawn is reachable from outside through a gap in the walls, printing the
  first leak if one is found.
- Falling below a map's lowest solid geometry crosses its **void layer**
  (`void_y`): the player is teleported to the nearest spawn with all momentum
  cancelled, so holes don't drop you out of the world.
- The sky is a procedural gradient (map `sky` colour at the horizon up to a
  zenith blue); levels have no imported ceiling, so they are open to the sky.

## Netcode

The server stays fully authoritative, but the client now predicts the local
player's movement: unacknowledged inputs are replayed through the shared
simulation on top of each snapshot, so movement and bunny hopping feel
instant regardless of latency. Prediction needs the server's map available
locally at `maps/<name>.txt` (logged at connect); without it the client falls
back to rendering raw server state.

Remote entities are interpolated against a jitter-buffered render clock over a
short ring of recent snapshots, so they stay smooth when packets arrive late,
early, or out of order and a few dropped snapshots cost nothing. Each input
carries the server tick the client's view was showing; the server keeps ~1.5 s
of player poses and rewinds remote hit boxes to that tick for hitscan, so shots
land where the shooter saw them instead of needing to lead targets. Rocket
projectiles are not rewound. Set `LAG_COMP_MAX_REWIND` in `src/game/tuning.h`
to bound the rewind window (default 60 ticks).

Protocol version 7 — older builds cannot interoperate.
