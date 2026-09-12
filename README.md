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

## Run

```sh
./build/arena --host
./build/arena --connect 127.0.0.1:27950 --name player2
./build/arena --connect 127.0.0.1:27950 --name player2 --sensitivity 3.0
./build/arena --connect 192.168.1.42:27950 --name player2 --class scout --jump mwheelup
./build/arena --connect 127.0.0.1:27950 --name player2 --jump mwheelup --doublejump space
./build/arena --dedicated --port 27950
./build/arena --bot 127.0.0.1:27950 --name bot1
```

The bot plays properly: it picks targets it can actually see, leads rockets,
circles while it shoots, closes to shotgun range or holds rifle range depending
on its class, bunny hops, goes for health when hurt, and does not walk off the
map. It beats the old spin-and-spray bot 40 matches to 0.

Maps are looked up relative to the current working directory and, failing
that, relative to the executable (and up to three parent directories), so the
binary can be launched from anywhere — including a `dist/` folder or the
`build/` directory.

When hosting, the game listens on all local interfaces and prints detected LAN
join commands in the terminal. Other players can join with `--connect IP:27950`
or by choosing Join by IP from the startup menu. Internet play still requires
the host's router/firewall to allow UDP on the chosen port.

Controls in the SDL client are WASD movement, Space jump by default, Left Shift
dash, Ctrl/C crouch, F fire, arrow keys look, 1 class primary, 2 rocket, 3/4/5
class switch (Ranger/Scout/Tank), and Escape for the settings menu.

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

Protocol version 6 — older builds cannot interoperate.
