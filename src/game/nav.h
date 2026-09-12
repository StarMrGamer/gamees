#pragma once

#include "core/math.h"

#include <cstdint>

// A waypoint graph for bot navigation.
//
// It is not generated from the geometry directly. It is generated from the
// reachability walk in map_check.cpp - the breadth-first search that steps a
// real player hull around the level and resolves each step with the real drop
// physics. Every node is therefore a position a player can genuinely stand in,
// and every edge is a step the movement code actually accepted. A mesh built
// from the brushes instead would confidently route a bot through a gap
// narrower than its own shoulders.
//
// The walk's own resolution (0.5 m) is far finer than navigation needs, so
// positions are collapsed onto a coarse lattice: one node per occupied cell.
// Measured: de_dust2 (167 x 140 m, the largest map here) collapses to 1712
// nodes and 6770 links, arena to 688 and 2648. The budgets leave room for a
// map several times larger, and overflowing them degrades to direct steering
// rather than failing.
constexpr int MAX_NAV_NODES = 8192;
constexpr int MAX_NAV_LINKS = 65536;
constexpr float NAV_CELL_XZ = 3.0f;
constexpr float NAV_CELL_Y = 2.5f;
// Longest route the pathfinder will return. A bot only ever consumes the first
// couple of waypoints before re-planning, so this bounds work rather than reach.
constexpr int NAV_MAX_PATH = 64;

struct NavMesh {
  bool built;
  // False when the walk overflowed the node or link budget. Queries then fail
  // and callers fall back to steering straight at the target, which is what
  // the bot did before this existed - degraded, not broken.
  bool complete;
  int node_count;
  int link_count;
  Vec3 nodes[MAX_NAV_NODES];
  int32_t link_start[MAX_NAV_NODES + 1];  // CSR, same shape as MapGrid
  uint16_t links[MAX_NAV_LINKS];
};

struct NavPath {
  int count;
  uint16_t nodes[NAV_MAX_PATH];
};
