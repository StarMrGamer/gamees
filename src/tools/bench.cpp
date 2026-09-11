#include "tools/bench.h"

#include "core/rng.h"
#include "game/collision.h"
#include "game/map.h"
#include "game/sim.h"
#include "game/tuning.h"
#include "game/weapons.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {

using Clock = std::chrono::steady_clock;

// Keeps the optimiser from deleting a benchmark body whose result is unused.
volatile unsigned g_bench_sink = 0;

BenchResult timed(const char* name, const char* unit, long long ops, double seconds) {
  BenchResult r;
  r.name = name;
  r.unit = unit;
  r.ops = ops;
  r.ns_per_op = ops > 0 ? (seconds * 1e9) / static_cast<double>(ops) : 0.0;
  r.ops_per_second = seconds > 0.0 ? static_cast<double>(ops) / seconds : 0.0;
  return r;
}

// A spread of points inside the map bounds, biased toward the spawns so the
// samples land in the playable space rather than in solid rock.
std::vector<Vec3> sample_points(const Map& map, int count, Rng& rng) {
  std::vector<Vec3> pts;
  pts.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    Vec3 base = map.spawn_count > 0 ? map.spawns[rng_int(rng, 0, map.spawn_count - 1)]
                                    : Vec3{0.0f, 0.0f, 0.0f};
    pts.push_back({base.x + rng_float(rng, -18.0f, 18.0f),
                   base.y + rng_float(rng, -2.0f, 6.0f),
                   base.z + rng_float(rng, -18.0f, 18.0f)});
  }
  return pts;
}

}  // namespace

bool bench_run(const char* map_path, std::vector<BenchResult>* out, std::string* error) {
  if (!out) return false;
  auto map_storage = std::make_unique<Map>();
  Map& map = *map_storage;
  if (!map_load(map_path, &map)) {
    if (error) *error = std::string("failed to load map '") + (map_path ? map_path : "") + "'";
    return false;
  }

  Rng rng{0xC0FFEEull};
  const int N = 20000;
  std::vector<Vec3> pts = sample_points(map, N, rng);
  std::vector<Vec3> dirs;
  dirs.reserve(static_cast<size_t>(N));
  for (int i = 0; i < N; ++i) {
    float yaw = rng_float(rng, -PI, PI);
    float pitch = rng_float(rng, -0.6f, 0.6f);
    dirs.push_back(angles_forward(yaw, pitch));
  }

  // 1. Broadphase overlap: the query move_slide leans on hardest.
  {
    auto t0 = Clock::now();
    unsigned hits = 0;
    for (int i = 0; i < N; ++i) {
      if (map_box_overlap(map, player_aabb(pts[static_cast<size_t>(i)], false))) ++hits;
    }
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    g_bench_sink += hits;
    out->push_back(timed("map_box_overlap", "overlap query", N, secs));
  }

  // 2. Ramp surface lookup over the whole column. Only the reporting tools ask
  //    for this one; it is here because it is the unbounded case, and the gap
  //    between it and the next row is what the query below buys.
  {
    auto t0 = Clock::now();
    unsigned hits = 0;
    for (int i = 0; i < N; ++i) {
      float y = 0.0f;
      if (map_ramp_surface(map, pts[static_cast<size_t>(i)].x, pts[static_cast<size_t>(i)].z, &y)) {
        ++hits;
      }
    }
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    g_bench_sink += hits;
    out->push_back(timed("map_ramp_surface", "surface query", N, secs));
  }

  // 3. The bounded form, which is the one move_slide actually calls - three
  //    times per step, between the ground check and the ramp snap.
  {
    auto t0 = Clock::now();
    unsigned hits = 0;
    for (int i = 0; i < N; ++i) {
      const Vec3& p = pts[static_cast<size_t>(i)];
      float y = 0.0f;
      if (map_ramp_surface_near(map, p.x, p.z, p.y, &y)) ++hits;
    }
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    g_bench_sink += hits;
    out->push_back(timed("map_ramp_surface_near", "surface query", N, secs));
  }

  // 4. Hitscan against world geometry; a shotgun blast costs seven of these.
  {
    auto t0 = Clock::now();
    float acc = 0.0f;
    for (int i = 0; i < N; ++i) {
      acc += ray_map(map, pts[static_cast<size_t>(i)], dirs[static_cast<size_t>(i)], 120.0f);
    }
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    g_bench_sink += static_cast<unsigned>(acc);
    out->push_back(timed("ray_map", "world ray", N, secs));
  }

  // 5. move_slide over a full second of travel at combat speed.
  {
    const int steps = 20000;
    auto t0 = Clock::now();
    Vec3 pos = map.spawn_count > 0 ? map.spawns[0] : Vec3{0.0f, 2.0f, 0.0f};
    Vec3 vel{9.0f, 0.0f, 5.0f};
    for (int i = 0; i < steps; ++i) {
      MoveResult r = move_slide(map, pos, vel, false, TICK_DT);
      pos = r.pos;
      vel = r.vel;
      if (vel.x == 0.0f && vel.z == 0.0f) {  // ran into a wall; bounce off it
        vel = {rng_float(rng, -9.0f, 9.0f), 0.0f, rng_float(rng, -9.0f, 9.0f)};
      }
      vel.y -= GRAVITY * TICK_DT;
      if (pos.y < map.void_y) pos = map.spawn_count > 0 ? map.spawns[0] : Vec3{0.0f, 2.0f, 0.0f};
    }
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    g_bench_sink += static_cast<unsigned>(pos.x);
    out->push_back(timed("move_slide", "movement step", steps, secs));
  }

  // 6. The whole authoritative tick with a full server of players. This is the
  //    number that decides how much headroom a dedicated server has.
  {
    GameState state{};
    Rng sim_rng{0x5eedull};
    game_init(state, map, 1000000);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      char name[16];
      std::snprintf(name, sizeof(name), "b%d", i);
      game_player_join(state, map, name);
    }
    const int ticks = 3000;
    auto t0 = Clock::now();
    for (int t = 0; t < ticks; ++t) {
      PlayerInput inputs[MAX_PLAYERS]{};
      for (int i = 0; i < MAX_PLAYERS; ++i) {
        inputs[i].sequence = static_cast<uint32_t>(t + 1);
        inputs[i].buttons = BTN_FORWARD | BTN_FIRE;
        if ((t + i) % 31 == 0) inputs[i].buttons |= BTN_JUMP;
        inputs[i].yaw = std::sin(static_cast<float>(t) * 0.02f + static_cast<float>(i)) * PI;
      }
      game_tick(state, map, inputs, sim_rng);
    }
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    g_bench_sink += state.tick;
    BenchResult r = timed("game_tick", "server tick (8 players)", ticks, secs);
    out->push_back(r);
  }

  return true;
}

std::string bench_results_json(const std::vector<BenchResult>& results) {
  std::string s = "[";
  for (size_t i = 0; i < results.size(); ++i) {
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "%s{\"name\":\"%s\",\"unit\":\"%s\",\"ops\":%lld,"
                  "\"ns_per_op\":%.1f,\"ops_per_second\":%.0f}",
                  i ? "," : "", results[i].name.c_str(), results[i].unit.c_str(),
                  results[i].ops, results[i].ns_per_op, results[i].ops_per_second);
    s += buf;
  }
  s += "]";
  return s;
}
