#include "test_harness.h"

int main() {
  int passed = 0;
  int total = 0;
  for (const TestCase& tc : test_registry()) {
    ++total;
    g_test_failed = false;
    tc.fn();
    if (!g_test_failed) {
      ++passed;
    } else {
      std::printf("FAILED %s\n", tc.name);
    }
  }
  std::printf("PASSED %d/%d\n", passed, total);
  return passed == total ? 0 : 1;
}
