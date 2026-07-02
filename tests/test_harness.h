#pragma once

#include <cmath>
#include <cstdio>
#include <vector>

struct TestCase {
  const char* name;
  void (*fn)();
};

inline std::vector<TestCase>& test_registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct TestRegistrar {
  TestRegistrar(const char* name, void (*fn)()) {
    test_registry().push_back({name, fn});
  }
};

inline bool g_test_failed = false;

#define TEST(name) \
  static void test_##name(); \
  static TestRegistrar registrar_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      g_test_failed = true; \
    } \
  } while (0)

#define CHECK_NEAR(a, b, eps) \
  do { \
    float va = static_cast<float>(a); \
    float vb = static_cast<float>(b); \
    if (std::fabs(va - vb) > static_cast<float>(eps)) { \
      std::printf("%s:%d: CHECK_NEAR failed: %s=%f %s=%f eps=%f\n", __FILE__, __LINE__, #a, va, #b, vb, static_cast<float>(eps)); \
      g_test_failed = true; \
    } \
  } while (0)

#define CHECK_EQ_INT(a, b) \
  do { \
    int va = static_cast<int>(a); \
    int vb = static_cast<int>(b); \
    if (va != vb) { \
      std::printf("%s:%d: CHECK_EQ_INT failed: %s=%d %s=%d\n", __FILE__, __LINE__, #a, va, #b, vb); \
      g_test_failed = true; \
    } \
  } while (0)
