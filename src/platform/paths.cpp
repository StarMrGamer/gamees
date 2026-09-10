#include "platform/paths.h"

#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

bool executable_directory(char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;

  char buf[1024];
#ifdef _WIN32
  DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) return false;
  buf[n] = 0;
#else
  ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return false;
  buf[n] = 0;
#endif

  char* slash = nullptr;
  for (char* p = buf; *p; ++p) {
    if (*p == '/' || *p == '\\') slash = p;
  }
  if (!slash || slash == buf) return false;
  *slash = 0;

  std::snprintf(out, static_cast<size_t>(cap), "%s", buf);
  return true;
}
