#pragma once

// Writes the directory containing the running executable (without a trailing
// separator) into `out`. Returns false if it cannot be determined.
bool executable_directory(char* out, int cap);
