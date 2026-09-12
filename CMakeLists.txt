cmake_minimum_required(VERSION 3.24)

project(arena LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
  # Build SDL statically. Together with the embedded maps this makes the
  # produced executable the only file needed to run the game - no SDL3.dll
  # beside it, no maps/ directory. A system SDL3 (the usual Linux case) is used
  # as found; this only affects the fetched build, which is what Windows uses.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.0.tar.gz
  )
  FetchContent_MakeAvailable(SDL3)
endif()

# The static build exports a differently named target.
if(TARGET SDL3::SDL3-static)
  set(ARENA_SDL SDL3::SDL3-static)
else()
  set(ARENA_SDL SDL3::SDL3)
endif()

# Maps are compiled into the binary so the executable is the only file needed to
# run the game. Regenerated whenever a map changes, at build time rather than by
# hand, so what ships inside the exe cannot drift from maps/.
set(ARENA_EMBEDDED_MAPS "${CMAKE_BINARY_DIR}/generated/embedded_maps.cpp")
file(GLOB ARENA_MAP_FILES "${CMAKE_SOURCE_DIR}/maps/*.txt")
add_custom_command(
  OUTPUT "${ARENA_EMBEDDED_MAPS}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
  COMMAND ${CMAKE_COMMAND}
          -DMAPS_DIR=${CMAKE_SOURCE_DIR}/maps
          -DOUTPUT=${ARENA_EMBEDDED_MAPS}
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_maps.cmake
  DEPENDS ${ARENA_MAP_FILES} "${CMAKE_SOURCE_DIR}/cmake/embed_maps.cmake"
  COMMENT "Embedding maps into the executable"
  VERBATIM)

set(ARENA_SOURCES
  ${ARENA_EMBEDDED_MAPS}
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
target_link_libraries(arena PRIVATE ${ARENA_SDL})

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
  tests/test_embedded_maps.cpp
  tests/test_movement.cpp
  tests/test_nav.cpp
  tests/test_net.cpp
  tests/test_particles.cpp
  tests/test_snapshot.cpp
  tests/test_weapons.cpp
  ${ARENA_SOURCES}
)
target_include_directories(arena_tests PRIVATE src tests)
target_link_libraries(arena_tests PRIVATE ${ARENA_SDL})

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
    # Plain `-static`, not the narrower -static-libgcc/-static-libstdc++ pair.
    # Those leave libwinpthread-1.dll as a real import - libstdc++ pulls it in
    # after the -Bdynamic switches back - so the executable still needed a
    # MinGW runtime DLL beside it. Verified with objdump: the only imports left
    # now are DLLs that ship with Windows.
    target_link_options(arena PRIVATE -static)
    target_link_options(arena_tests PRIVATE -static)
  endif()
endif()

if(MSVC)
  target_compile_options(arena PRIVATE /W4)
  target_compile_options(arena_tests PRIVATE /W4)
else()
  target_compile_options(arena PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(arena_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
