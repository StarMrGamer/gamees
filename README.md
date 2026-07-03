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

When hosting, the game listens on all local interfaces and prints detected LAN
join commands in the terminal. Other players can join with `--connect IP:27950`
or by choosing Join by IP from the startup menu. Internet play still requires
the host's router/firewall to allow UDP on the chosen port.

Controls in the SDL client are WASD movement, Space jump by default, Left Shift
dash, Ctrl/C crouch, F fire, arrow keys look, 1 class primary, 2 rocket, 3/4/5
class switch (Ranger/Scout/Tank), and Escape for the settings menu. Jump can be
changed to mouse wheel up/down in settings or with `--jump mwheelup` /
`--jump mwheeldown`; in mouse-wheel mode, Space no longer jumps.

The mid-air double jump has its own bind, separate from the ground jump. By
default it follows the jump button, but it can be moved onto its own key or
wheel in the settings menu or with `--doublejump space|mwheelup|mwheeldown`
(`--doublejump jump` restores the default). Wall jumps stay on the primary jump
button. Ranger primary is rifle, Scout primary is shotgun, and Tank primary is a
fast low-damage LMG; each weapon has its own report and bullet tracers, and
shotgun pellets lose damage with distance, so the shotgun rewards closing in.

Sensitivity and jump bind are saved to `arena.cfg` when changed in settings or
when the client exits. Set `ARENA_CONFIG=/path/to/file.cfg` to use a different
config file. Command-line `--sensitivity` and `--jump` override the saved values
for that launch and are saved when the client exits.

## Netcode

The server stays fully authoritative, but the client now predicts the local
player's movement: unacknowledged inputs are replayed through the shared
simulation on top of each snapshot, so movement and bunny hopping feel
instant regardless of latency. Remote players interpolate one snapshot
interval behind. Prediction needs the server's map available locally at
`maps/<name>.txt` (logged at connect); without it the client falls back to
rendering raw server state. Protocol version 5 — older builds cannot
interoperate.
