#include "test_harness.h"

#include <cstring>

// Usage:
//   arena_tests             run everything
//   arena_tests --list      print test names, one per line
//   arena_tests SUBSTRING   run only tests whose name contains SUBSTRING
//
// The filter exists so an agent (or a person) iterating on one failure can
// re-run just that test instead of the whole suite.
int main(int argc, char** argv) {
  const char* filter = nullptr;
  bool list_only = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--list") == 0) {
      list_only = true;
    } else if (argv[i][0] != '-') {
      filter = argv[i];
    }
  }

  if (list_only) {
    for (const TestCase& tc : test_registry()) std::printf("%s\n", tc.name);
    return 0;
  }

  int passed = 0;
  int total = 0;
  int skipped = 0;
  for (const TestCase& tc : test_registry()) {
    if (filter && !std::strstr(tc.name, filter)) {
      ++skipped;
      continue;
    }
    ++total;
    g_test_failed = false;
    tc.fn();
    if (!g_test_failed) {
      ++passed;
    } else {
      std::printf("FAILED %s\n", tc.name);
    }
  }

  if (total == 0) {
    std::printf("no tests matched '%s'\n", filter ? filter : "");
    return 1;
  }
  std::printf("PASSED %d/%d", passed, total);
  if (skipped > 0) std::printf(" (%d skipped by filter)", skipped);
  std::printf("\n");
  return passed == total ? 0 : 1;
}
