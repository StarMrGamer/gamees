#pragma once

void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
[[noreturn]] void fatal_error(const char* fmt, ...);
