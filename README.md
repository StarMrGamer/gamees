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
./build/arena --dedicated --port 27950
./build/arena --bot 127.0.0.1:27950 --name bot1
```

Controls in the SDL client are WASD movement, Space jump, Left Shift dash,
Ctrl/C crouch, F fire, arrow keys look, and 1/2 weapon switch.
