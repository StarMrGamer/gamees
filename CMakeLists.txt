cmake_minimum_required(VERSION 3.24)

project(arena LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
  FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.0.tar.gz
  )
  FetchContent_MakeAvailable(SDL3)
endif()

set(ARENA_SOURCES
  src/ai/agent.cpp
  src/audio/mixer.cpp
  src/audio/synth.cpp
  src/client/bot.cpp
  src/client/client.cpp
  src/client/config.cpp
  src/client/game_client.cpp
  src/client/gunfeel.cpp
  src/client/menu.cpp
  src/core/log.cpp
  src/game/collision.cpp
  src/game/history.cpp
  src/game/map.cpp
  src/game/map_check.cpp
  src/game/movement.cpp
  src/game/nav.cpp
  src/game/sim.cpp
  src/game/snapshot.cpp
  src/game/weapons.cpp
  src/net/protocol.cpp
  src/platform/paths.cpp
  src/platform/socket.cpp
  src/render/gl_loader.cpp
  src/render/hud.cpp
  src/render/mesh.cpp
  src/render/particles.cpp
  src/render/renderer.cpp
  src/render/shader.cpp
  src/server/dedicated.cpp
  src/server/server.cpp
  src/tools/bench.cpp
  src/tools/headless.cpp
  src/tools/map_import.cpp
)

add_executable(arena src/main.cpp ${ARENA_SOURCES})
target_include_directories(arena PRIVATE src)
target_link_libraries(arena PRIVATE SDL3::SDL3)

add_executable(arena_tests
  tests/test_agent.cpp
  tests/test_client.cpp
  tests/test_brush.cpp
  tests/test_collision.cpp
  tests/test_grid.cpp
  tests/test_gunfeel.cpp
  tests/test_history.cpp
  tests/test_main.cpp
  tests/test_map.cpp
  tests/test_map_check.cpp
  tests/test_map_import.cpp
  tests/test_math.cpp
  tests/test_audio.cpp
  tests/test_config.cpp
  tests/test_movement.cpp
  tests/test_nav.cpp
  tests/test_net.cpp
  tests/test_particles.cpp
  tests/test_snapshot.cpp
  tests/test_weapons.cpp
  ${ARENA_SOURCES}
)
target_include_directories(arena_tests PRIVATE src tests)
target_link_libraries(arena_tests PRIVATE SDL3::SDL3)

if(WIN32)
  target_link_libraries(arena PRIVATE ws2_32)
  target_link_libraries(arena_tests PRIVATE ws2_32)
  # A Map is over a megabyte once its spatial index is counted, and the tools
  # and tests routinely hold one (sometimes two) in a frame. Windows reserves
  # 1 MB by default, where Linux gives 8; reserve the same here so a build that
  # runs on one platform runs on the other.
  if(MSVC)
    target_link_options(arena PRIVATE /STACK:8388608)
    target_link_options(arena_tests PRIVATE /STACK:8388608)
  else()
    target_link_options(arena PRIVATE -Wl,--stack,8388608)
    target_link_options(arena_tests PRIVATE -Wl,--stack,8388608)
  endif()
  if(MINGW)
    target_link_options(arena PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic,-lwinpthread,-Bdynamic)
    target_link_options(arena_tests PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic,-lwinpthread,-Bdynamic)
  endif()
endif()

if(MSVC)
  target_compile_options(arena PRIVATE /W4)
  target_compile_options(arena_tests PRIVATE /W4)
else()
  target_compile_options(arena PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(arena_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
