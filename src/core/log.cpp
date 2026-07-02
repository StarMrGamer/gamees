#include "core/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

static void vlog_line(const char* level, const char* fmt, va_list args) {
  std::fprintf(stderr, "[%s] ", level);
  std::vfprintf(stderr, fmt, args);
  std::fprintf(stderr, "\n");
}

void log_info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlog_line("info", fmt, args);
  va_end(args);
}

void log_warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlog_line("warn", fmt, args);
  va_end(args);
}

void log_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlog_line("error", fmt, args);
  va_end(args);
}

[[noreturn]] void fatal_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlog_line("fatal", fmt, args);
  va_end(args);
  std::exit(1);
}
