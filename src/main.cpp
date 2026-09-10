#include <SDL3/SDL_main.h>

#include "client/bot.h"
#include "client/config.h"
#include "client/game_client.h"
#include "client/menu.h"
#include "core/log.h"
#include "game/map.h"
#include "game/map_check.h"
#include "game/tuning.h"
#include "net/protocol.h"
#include "platform/paths.h"
#include "platform/socket.h"
#include "server/dedicated.h"
#include "tools/bench.h"
#include "tools/headless.h"
#include "tools/map_import.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

enum RunMode {
  MODE_MENU,
  MODE_CONNECT,
  MODE_HOST,
  MODE_DEDICATED,
  MODE_BOT,
  MODE_IMPORT,
  MODE_CHECK,
  MODE_SIMULATE,
  MODE_BENCH,
  MODE_NETCHECK,
  MODE_HELP,
};

// Kept next to the flag parser on purpose: an agent (or a person) discovers
// what this binary can do by running it with --help, so the two must not
// drift apart.
static void print_usage() {
  std::printf(
      "arena - LAN arena FPS\n"
      "\n"
      "Play:\n"
      "  arena                          launch the menu\n"
      "  arena --connect ADDR           join a server\n"
      "  arena --host                   host and play\n"
      "  arena --dedicated              headless server, no window\n"
      "  arena --bot ADDR               scripted client, for load testing\n"
      "\n"
      "Headless tools (no window, no GPU - safe for CI and agents):\n"
      "  arena --simulate               run the sim with scripted inputs, print a report\n"
      "  arena --bench                  microbenchmark the simulation hot paths\n"
      "  arena --netcheck               run server + clients over loopback, check the round trip\n"
      "  arena --check-map FILE         verify a map is sealed against the void\n"
      "  arena --import-map FILE        convert a .map (Valve 220) into arena format\n"
      "\n"
      "Options:\n"
      "  --map FILE                     map to use (default maps/arena.txt)\n"
      "  --port N, --fraglimit N        server settings\n"
      "  --name NAME, --class C         client identity (ranger|scout|tank)\n"
      "  --sensitivity F                mouse sensitivity, 0.1 to 20.0\n"
      "  --jump BIND, --doublejump BIND space|mwheelup|mwheeldown (doublejump also lalt)\n"
      "  --ticks N, --players N         --simulate length and player count\n"
      "  --seconds N                    --netcheck duration\n"
      "  --seed N, --trace-every N      --simulate determinism seed and trace sampling\n"
      "  --json                         machine-readable output for tool modes\n"
      "  --import-out FILE, --import-scale F, --import-max-boxes N\n"
      "  --help                         this message\n");
}

static bool parse_port(const char* s, uint16_t* out) {
  int p = std::atoi(s);
  if (p <= 0 || p > 65535) return false;
  *out = static_cast<uint16_t>(p);
  return true;
}

static bool parse_sensitivity(const char* s, float* out) {
  if (!s || !out) return false;
  char* end = nullptr;
  float value = std::strtof(s, &end);
  if (end == s || *end != 0 || value < 0.1f || value > 20.0f) return false;
  *out = value;
  return true;
}

static bool parse_player_class_arg(const char* s, uint8_t* out) {
  if (!s || !out) return false;
  if (std::strcmp(s, "ranger") == 0 || std::strcmp(s, "RANGER") == 0) {
    *out = CLASS_RANGER;
    return true;
  }
  if (std::strcmp(s, "scout") == 0 || std::strcmp(s, "SCOUT") == 0) {
    *out = CLASS_SCOUT;
    return true;
  }
  if (std::strcmp(s, "tank") == 0 || std::strcmp(s, "TANK") == 0) {
    *out = CLASS_TANK;
    return true;
  }
  return false;
}

static bool parse_jump_bind_arg(const char* s, uint8_t* out) {
  return client_jump_bind_parse(s, out);
}

static bool parse_airjump_bind_arg(const char* s, uint8_t* out) {
  return client_airjump_bind_parse(s, out);
}

// Let maps (and the client's prediction map) be found relative to the
// executable, so the game runs from any working directory. The parents are
// included so a binary built in build/ still finds the repo's maps/ folder.
static void register_data_search_paths() {
  char dir[1024];
  if (!executable_directory(dir, sizeof(dir))) return;
  map_add_search_dir(dir);
  std::string d = dir;
  for (int i = 0; i < 3; ++i) {
    size_t slash = d.find_last_of("/\\");
    if (slash == std::string::npos || slash == 0) break;
    d.resize(slash);
    map_add_search_dir(d.c_str());
  }
}

static void log_host_join_addresses(uint16_t port) {
  log_info("hosting on UDP port %u", port);
  NetAddress addrs[8]{};
  int count = net_local_addresses(addrs, 8, port);
  if (count <= 0) {
    log_info("friends can join with --connect <your-ip>:%u; could not auto-detect a LAN IPv4 address", port);
    return;
  }
  log_info("friends on your LAN can join with:");
  for (int i = 0; i < count; ++i) {
    char text[64]{};
    net_address_to_string(addrs[i], text, sizeof(text));
    log_info("  ./arena --connect %s --name player2", text);
  }
}

int main(int argc, char** argv) {
  register_data_search_paths();

  RunMode mode = MODE_MENU;
  const char* address = "127.0.0.1";
  const char* name = "player";
  const char* map_path = "maps/arena.txt";
  const char* import_in = nullptr;
  const char* import_out = nullptr;
  float import_scale = 0.0254f;
  int import_max_boxes = 0;
  bool json_output = false;
  SimOptions sim_options;
  NetCheckOptions netcheck_options;
  uint16_t port = DEFAULT_PORT;
  int fraglimit = DEFAULT_FRAG_LIMIT;
  ClientSettings client_settings{};
  client_config_load(client_settings);

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
      mode = MODE_CONNECT;
      address = argv[++i];
    } else if (std::strcmp(argv[i], "--host") == 0) {
      mode = MODE_HOST;
    } else if (std::strcmp(argv[i], "--dedicated") == 0) {
      mode = MODE_DEDICATED;
    } else if (std::strcmp(argv[i], "--bot") == 0 && i + 1 < argc) {
      mode = MODE_BOT;
      address = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      mode = MODE_HELP;
    } else if (std::strcmp(argv[i], "--simulate") == 0) {
      mode = MODE_SIMULATE;
    } else if (std::strcmp(argv[i], "--bench") == 0) {
      mode = MODE_BENCH;
    } else if (std::strcmp(argv[i], "--netcheck") == 0) {
      mode = MODE_NETCHECK;
    } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      int value = std::atoi(argv[++i]);
      if (value <= 0) fatal_error("invalid --seconds");
      netcheck_options.seconds = value;
    } else if (std::strcmp(argv[i], "--json") == 0) {
      json_output = true;
    } else if (std::strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
      int value = std::atoi(argv[++i]);
      if (value <= 0) fatal_error("invalid --ticks");
      sim_options.ticks = value;
    } else if (std::strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
      int value = std::atoi(argv[++i]);
      if (value <= 0 || value > MAX_PLAYERS) fatal_error("invalid --players");
      sim_options.players = value;
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      sim_options.seed = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--trace-every") == 0 && i + 1 < argc) {
      int value = std::atoi(argv[++i]);
      if (value < 0) fatal_error("invalid --trace-every");
      sim_options.trace_every = value;
    } else if (std::strcmp(argv[i], "--check-map") == 0 && i + 1 < argc) {
      mode = MODE_CHECK;
      map_path = argv[++i];
    } else if (std::strcmp(argv[i], "--import-map") == 0 && i + 1 < argc) {
      mode = MODE_IMPORT;
      import_in = argv[++i];
    } else if (std::strcmp(argv[i], "--import-out") == 0 && i + 1 < argc) {
      import_out = argv[++i];
    } else if (std::strcmp(argv[i], "--import-scale") == 0 && i + 1 < argc) {
      const char* text = argv[++i];
      char* end = nullptr;
      float value = std::strtof(text, &end);
      if (end == text || *end != 0 || value <= 0.0f) fatal_error("invalid --import-scale");
      import_scale = value;
    } else if (std::strcmp(argv[i], "--import-max-boxes") == 0 && i + 1 < argc) {
      int value = std::atoi(argv[++i]);
      if (value <= 0) fatal_error("invalid --import-max-boxes");
      import_max_boxes = value;
    } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      if (!parse_port(argv[++i], &port)) fatal_error("invalid --port");
    } else if (std::strcmp(argv[i], "--fraglimit") == 0 && i + 1 < argc) {
      fraglimit = std::atoi(argv[++i]);
      if (fraglimit <= 0) fatal_error("invalid --fraglimit");
    } else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      name = argv[++i];
    } else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
      map_path = argv[++i];
    } else if (std::strcmp(argv[i], "--sensitivity") == 0 && i + 1 < argc) {
      if (!parse_sensitivity(argv[++i], &client_settings.sensitivity)) {
        fatal_error("invalid --sensitivity, use a value from 0.1 to 20.0");
      }
    } else if (std::strcmp(argv[i], "--class") == 0 && i + 1 < argc) {
      if (!parse_player_class_arg(argv[++i], &client_settings.player_class)) {
        fatal_error("invalid --class, use ranger, scout, or tank");
      }
    } else if (std::strcmp(argv[i], "--jump") == 0 && i + 1 < argc) {
      if (!parse_jump_bind_arg(argv[++i], &client_settings.jump_bind)) {
        fatal_error("invalid --jump, use space, mwheelup, or mwheeldown");
      }
    } else if (std::strcmp(argv[i], "--doublejump") == 0 && i + 1 < argc) {
      if (!parse_airjump_bind_arg(argv[++i], &client_settings.airjump_bind)) {
        fatal_error("invalid --doublejump, use jump, space, mwheelup, or mwheeldown");
      }
    } else if (std::strcmp(argv[i], "--fly") == 0) {
      // Accepted for the world milestone; the current client always uses player camera.
    } else {
      fatal_error("unknown or incomplete argument '%s'", argv[i]);
    }
  }

  if (mode == MODE_HELP) {
    print_usage();
    return 0;
  }

  if (mode == MODE_SIMULATE) {
    SimReport report;
    std::string error;
    if (!headless_simulate(map_path, sim_options, &report, &error)) {
      log_error("simulate failed: %s", error.c_str());
      return 1;
    }
    if (!report.trace.empty()) std::fputs(report.trace.c_str(), stdout);
    if (json_output) {
      std::printf("%s\n", sim_report_json(report).c_str());
    } else {
      log_info("simulated %d ticks with %d players on '%s'", report.ticks, report.players,
               map_path);
      log_info("  state hash %u (stable for a given map + seed)", report.state_hash);
      log_info("  %d frags, %d shots, %d void resets, top speed %.1f m/s", report.total_frags,
               report.shots_fired, report.void_resets,
               static_cast<double>(report.max_speed));
      log_info("  %.0f sim ticks/s (%.0fx realtime)", report.ticks_per_second,
               report.ticks_per_second / TICK_RATE);
    }
    // Non-zero on the two failures worth failing a build over.
    if (report.nan_seen) {
      log_error("simulation produced a non-finite player state");
      return 1;
    }
    if (report.stuck_seen) {
      log_error("a player was stuck inside solid geometry (first at tick %u, %.1f %.1f %.1f)",
                report.first_stuck_tick, static_cast<double>(report.first_stuck_pos.x),
                static_cast<double>(report.first_stuck_pos.y),
                static_cast<double>(report.first_stuck_pos.z));
      return 1;
    }
    return 0;
  }

  if (mode == MODE_NETCHECK) {
    netcheck_options.clients = sim_options.players > 1 ? sim_options.players - 1 : 3;
    netcheck_options.port = port == DEFAULT_PORT ? 28150 : port;
    NetCheckReport report;
    std::string error;
    if (!headless_netcheck(map_path, netcheck_options, &report, &error)) {
      log_error("netcheck failed to start: %s", error.c_str());
      return 1;
    }
    if (json_output) {
      std::printf("%s\n", netcheck_report_json(report).c_str());
    } else {
      log_info("netcheck on '%s': %d/%d clients, %u server ticks, %u snapshots",
               map_path, report.clients_connected, report.clients_expected,
               report.server_ticks, report.snapshots_received);
      log_info("  worst prediction error %.3f m",
               static_cast<double>(report.max_prediction_error));
    }
    if (!report.ok) {
      log_error("netcheck failed: %s", report.failure.c_str());
      return 1;
    }
    return 0;
  }

  if (mode == MODE_BENCH) {
    std::vector<BenchResult> results;
    std::string error;
    if (!bench_run(map_path, &results, &error)) {
      log_error("bench failed: %s", error.c_str());
      return 1;
    }
    if (json_output) {
      std::printf("%s\n", bench_results_json(results).c_str());
    } else {
      log_info("benchmark on '%s'", map_path);
      for (const BenchResult& r : results) {
        log_info("  %-18s %9.1f ns/%s  (%.0f/s)", r.name.c_str(), r.ns_per_op,
                 r.unit.c_str(), r.ops_per_second);
      }
    }
    return 0;
  }

  char menu_addr[64]{};
  if (mode == MODE_MENU) {
    MenuResult r = menu_run(menu_addr, sizeof(menu_addr));
    if (r == MENU_QUIT) return 0;
    if (r == MENU_HOST) mode = MODE_HOST;
    if (r == MENU_JOIN) {
      mode = MODE_CONNECT;
      address = menu_addr;
    }
  }

  if (mode == MODE_IMPORT) {
    MapImportOptions options;
    options.scale = import_scale;
    options.max_boxes = import_max_boxes;
    MapImportResult result;
    std::string error;
    if (!map_import_file(import_in, import_out, options, &result, &error)) {
      log_error("map import failed: %s", error.c_str());
      return 1;
    }
    std::string out = (import_out && import_out[0]) ? import_out
                                                    : ("maps/" + result.name + ".txt");
    log_info("imported '%s' -> %s: %d brushes -> %d boxes + %d ramps (%d dropped)%s",
             import_in, out.c_str(), result.brushes_seen, result.boxes_written,
             result.ramps_written, result.boxes_dropped + result.boxes_truncated,
             result.truncated ? " [truncated]" : "");
    log_info("  %d spawns, %d health", result.spawns, result.health);
    if (result.leaks_to_void) {
      log_warn("  %d/%d spawns connect to the void (first at %.1f %.1f %.1f)",
               result.leaked_spawns, result.spawns, result.first_leak.x,
               result.first_leak.y, result.first_leak.z);
    }
    return 0;
  }

  if (mode == MODE_CHECK) {
    Map map{};
    if (!map_load(map_path, &map)) {
      log_error("failed to load map '%s'", map_path);
      return 1;
    }
    MapCheckReport rep = map_check_leaks(map, map.spawns, map.spawn_count);
    if (!rep.ran) {
      log_error("map has no geometry to check");
      return 1;
    }
    if (json_output) {
      std::printf("{\"map\":\"%s\",\"name\":\"%s\",\"boxes\":%d,\"ramps\":%d,"
                  "\"spawns\":%d,\"void_y\":%.2f,\"voxel\":%.2f,\"solid_cells\":%lld,"
                  "\"exterior_cells\":%lld,\"leaked\":%s,\"leaked_spawns\":%d}\n",
                  map_path, map.name, map.box_count, map.ramp_count, map.spawn_count,
                  static_cast<double>(map.void_y), static_cast<double>(rep.voxel_size),
                  rep.solid_cells, rep.exterior_cells, rep.leaked ? "true" : "false",
                  rep.leaked_points);
      return rep.leaked ? 1 : 0;
    }
    log_info("map '%s': %d boxes, %d ramps, %d spawns", map_path, map.box_count,
             map.ramp_count, map.spawn_count);
    log_info("  void_y=%.1f, voxel=%.2fm, %lld solid cells",
             static_cast<double>(map.void_y), static_cast<double>(rep.voxel_size),
             rep.solid_cells);
    if (rep.leaked) {
      log_warn("%d/%d spawns leak to the void (first at %.1f %.1f %.1f)",
               rep.leaked_points, rep.point_count, static_cast<double>(rep.first_leak.x),
               static_cast<double>(rep.first_leak.y), static_cast<double>(rep.first_leak.z));
      return 1;
    }
    log_info("no gap to the void: all %d spawns are sealed", rep.point_count);
    return 0;
  }

  if (mode == MODE_DEDICATED) {
    return dedicated_main(port, map_path, fraglimit);
  }

  if (mode == MODE_HOST) {
    ServerThread st{};
    if (!server_thread_start(st, port, map_path, fraglimit)) return 1;
    log_host_join_addresses(port);
    char local[64];
    std::snprintf(local, sizeof(local), "127.0.0.1:%u", port);
    NetAddress server{};
    if (!net_address_parse(local, port, &server)) fatal_error("failed to parse loopback address");
    return game_client_main(server, name, &st, map_path, client_settings);
  }

  NetAddress server{};
  if (!net_address_parse(address, port, &server)) {
    fatal_error("invalid address '%s'", address);
  }

  if (mode == MODE_BOT) {
    return bot_main(server, name, 0);
  }

  return game_client_main(server, name, nullptr, map_path, client_settings);
}
