#pragma once

#include "core/math.h"

#include <cstddef>
#include <string>

// Imports source maps from other engines into the arena plain-text map format.
// Three source dialect are understood:
//   - Valve Source compiled ".bsp" (VBSP; what ships with Source games) -- binary
//   - Quake / idTech ".map"  (brush/entity text; TrenchBroom, Q1/Q2/Q3, Hammer export)
//   - Valve Source ".vmf"    (KeyValues; Hammer native)
// The text dialects are auto-detected from their content; ".bsp" by its magic.
//
// Solids are approximated as axis-aligned boxes (the arena engine's only
// primitive): each brush is converted to the AABB of its convex hull, so sloped
// or curved brushes become their bounding blocks. Entities become spawn points
// and health packs; textures map to muted vertex colours.

struct MapImportOptions {
  // Metres per source unit. Source/Quake units are ~one inch, so the default
  // makes a 72-unit-tall player about 1.8 m.
  float scale = 0.0254f;
  // Upper bound on emitted boxes; the largest brushes win if the map exceeds it.
  int max_boxes = 0;  // 0 => MAX_MAP_BOXES
  // Keep solid detail/brush entities (func_detail, func_brush, ...).
  bool include_detail = true;
};

struct MapImportResult {
  std::string name;      // sanitized map name actually used
  int brushes_seen = 0;
  int boxes_written = 0;
  int ramps_written = 0;
  int boxes_dropped = 0;    // degenerate / filtered
  int boxes_truncated = 0;  // dropped because of max_boxes
  int spawns = 0;
  int health = 0;
  bool truncated = false;
  // Set when the exterior flood-fill reaches a spawn, i.e. a gap to the void.
  bool leaks_to_void = false;
  int leaked_spawns = 0;
  Vec3 first_leak{};
};

// Convert source map text into arena map text. `source_name` is only a hint
// (its extension and the text itself select the dialect). Returns false and
// fills `error` on failure.
bool map_source_to_arena(const char* text, const char* source_name,
                         const MapImportOptions& options,
                         std::string* out_text, MapImportResult* result,
                         std::string* error);

// Same, but for arbitrary bytes: detects a compiled Source/Quake ".bsp"
// (VBSP magic) first and falls back to text parsing otherwise.
bool map_import_bytes_to_arena(const void* data, size_t size, const char* source_name,
                               const MapImportOptions& options,
                               std::string* out_text, MapImportResult* result,
                               std::string* error);

// Read `in_path`, convert it, and write arena text. When `out_path` is null or
// empty the output is written to "maps/<name>.txt".
bool map_import_file(const char* in_path, const char* out_path,
                     const MapImportOptions& options, MapImportResult* result,
                     std::string* error);

// Lowercase [a-z0-9_] form of a name, safe to use as a map file stem.
std::string map_import_sanitize_name(const char* name);
