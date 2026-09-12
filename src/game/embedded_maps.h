#pragma once

// Maps baked into the executable at build time by cmake/embed_maps.cmake.
//
// The point is that the built binary is the only file needed to run the game -
// no maps/ directory beside it. map_load() still reads from disk first, so an
// edited map during development wins over the baked copy.

// Text of an embedded map, matched on the basename of `path` so "maps/x.txt",
// "./maps/x.txt" and "x.txt" all resolve. Returns nullptr when not embedded.
const char* embedded_map_text(const char* path, int* size_out = nullptr);

int embedded_map_count();
const char* embedded_map_name(int index);
