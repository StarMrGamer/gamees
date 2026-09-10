#pragma once

#include <string>
#include <vector>

// Microbenchmarks for the simulation's hot paths. Reported as nanoseconds per
// operation so a change can be judged from a terminal without a profiler; the
// same binary and flags are what CI and an agent should compare across commits.

struct BenchResult {
  std::string name;
  std::string unit;   // what one "op" is
  double ns_per_op = 0.0;
  double ops_per_second = 0.0;
  long long ops = 0;
};

// Runs the full suite against `map_path`. Returns false (with `error` set) if
// the map cannot be loaded.
bool bench_run(const char* map_path, std::vector<BenchResult>* out, std::string* error);

std::string bench_results_json(const std::vector<BenchResult>& results);
