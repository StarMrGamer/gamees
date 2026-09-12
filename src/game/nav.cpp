#include "game/map.h"
#include "game/map_check.h"
#include "game/nav.h"
#include "game/tuning.h"

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

// Lattice cell holding a position. Y is quantised too, so a tunnel under a
// bridge gets its own node rather than being merged with the road above it.
uint64_t nav_cell_key(Vec3 p) {
  int64_t ix = static_cast<int64_t>(std::floor(p.x / NAV_CELL_XZ));
  int64_t iz = static_cast<int64_t>(std::floor(p.z / NAV_CELL_XZ));
  int64_t iy = static_cast<int64_t>(std::floor(p.y / NAV_CELL_Y));
  return (static_cast<uint64_t>(ix & 0xfffff) << 44) ^
         (static_cast<uint64_t>(iz & 0xfffff) << 24) ^
         (static_cast<uint64_t>(iy & 0xffffff));
}

// Collects the reachability walk into a graph.
struct NavBuilder : ReachVisitor {
  std::unordered_map<uint64_t, int> cell_to_node;
  std::vector<Vec3> nodes;
  // Directed, deduplicated. Stored as a set of (from << 32 | to) so the same
  // pair discovered by several 0.5 m steps only costs one link.
  std::vector<std::vector<int>> adjacency;
  bool overflowed = false;

  int node_for(Vec3 p) {
    uint64_t key = nav_cell_key(p);
    auto it = cell_to_node.find(key);
    if (it != cell_to_node.end()) return it->second;
    if (static_cast<int>(nodes.size()) >= MAX_NAV_NODES) {
      overflowed = true;
      return -1;
    }
    int id = static_cast<int>(nodes.size());
    // The first position found in a cell becomes its node. It is a position
    // the walk actually stood in, which a cell centre would not be.
    nodes.push_back(p);
    adjacency.emplace_back();
    cell_to_node.emplace(key, id);
    return id;
  }

  void link(int a, int b) {
    if (a < 0 || b < 0 || a == b) return;
    for (int existing : adjacency[static_cast<size_t>(a)]) {
      if (existing == b) return;
    }
    adjacency[static_cast<size_t>(a)].push_back(b);
  }

  void node(Vec3 pos) override { node_for(pos); }

  void edge(Vec3 from, Vec3 to) override {
    int a = node_for(from);
    int b = node_for(to);
    if (a < 0 || b < 0) return;
    link(a, b);
    // The walk only ever discovers a step in the direction it travelled. Most
    // are symmetric, but a drop off a ledge is not: assuming it were would
    // route bots up cliffs they cannot climb, and they would grind against the
    // wall forever. Only add the return leg if it is a step, not a climb.
    if (from.y - to.y <= REACH_CLIMB_HEIGHT) link(b, a);
  }
};

float dist(Vec3 a, Vec3 b) {
  Vec3 d = a - b;
  return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

}  // namespace

void map_build_nav(Map* out) {
  if (!out) return;
  NavMesh& nav = out->nav;
  std::memset(&nav, 0, sizeof(nav));

  NavBuilder builder;
  MapReachReport rep = map_walk_reachable(*out, &builder);
  if (!rep.ran || builder.nodes.empty()) return;

  long long links = 0;
  for (const auto& a : builder.adjacency) links += static_cast<long long>(a.size());
  if (links > MAX_NAV_LINKS) {
    builder.overflowed = true;
    return;  // leave built=false; queries fail and callers steer directly
  }

  nav.node_count = static_cast<int>(builder.nodes.size());
  for (int i = 0; i < nav.node_count; ++i) nav.nodes[i] = builder.nodes[static_cast<size_t>(i)];

  int32_t cursor = 0;
  for (int i = 0; i < nav.node_count; ++i) {
    nav.link_start[i] = cursor;
    for (int to : builder.adjacency[static_cast<size_t>(i)]) {
      nav.links[cursor++] = static_cast<uint16_t>(to);
    }
  }
  nav.link_start[nav.node_count] = cursor;
  nav.link_count = cursor;
  nav.built = true;
  nav.complete = !builder.overflowed;
}

int nav_nearest_node(const Map& map, Vec3 p) {
  const NavMesh& nav = map.nav;
  if (!nav.built || nav.node_count <= 0) return -1;
  // A linear scan. At 8192 nodes that is ~15 us, and callers cache their route
  // for a fraction of a second rather than asking every tick, so indexing the
  // nodes would buy less than it costs in a POD-safe structure.
  int best = -1;
  float best_d = 1e30f;
  for (int i = 0; i < nav.node_count; ++i) {
    Vec3 d = nav.nodes[i] - p;
    // Height is weighted up: a node on the floor below is much worse than one
    // the same distance away on this floor.
    float score = d.x * d.x + d.z * d.z + d.y * d.y * 4.0f;
    if (score < best_d) {
      best_d = score;
      best = i;
    }
  }
  return best;
}

bool nav_find_path(const Map& map, Vec3 from, Vec3 to, NavPath* out) {
  if (!out) return false;
  out->count = 0;
  const NavMesh& nav = map.nav;
  if (!nav.built) return false;
  int start = nav_nearest_node(map, from);
  int goal = nav_nearest_node(map, to);
  if (start < 0 || goal < 0) return false;
  if (start == goal) {
    out->count = 1;
    out->nodes[0] = static_cast<uint16_t>(goal);
    return true;
  }

  // A*, with the scratch held thread-locally so a query allocates nothing and
  // Map stays a POD.
  static thread_local std::vector<float> g;
  static thread_local std::vector<int32_t> came;
  static thread_local std::vector<uint8_t> closed;
  static thread_local std::vector<int32_t> heap;   // binary heap of node ids
  static thread_local std::vector<float> heap_f;   // f-score parallel to `heap`
  const int n = nav.node_count;
  g.assign(static_cast<size_t>(n), 1e30f);
  came.assign(static_cast<size_t>(n), -1);
  closed.assign(static_cast<size_t>(n), 0);
  heap.clear();
  heap_f.clear();

  auto push = [&](int node, float f) {
    heap.push_back(node);
    heap_f.push_back(f);
    size_t i = heap.size() - 1;
    while (i > 0) {
      size_t parent = (i - 1) / 2;
      if (heap_f[parent] <= heap_f[i]) break;
      std::swap(heap[parent], heap[i]);
      std::swap(heap_f[parent], heap_f[i]);
      i = parent;
    }
  };
  auto pop = [&]() {
    int top = heap[0];
    heap[0] = heap.back();
    heap_f[0] = heap_f.back();
    heap.pop_back();
    heap_f.pop_back();
    size_t i = 0;
    for (;;) {
      size_t l = i * 2 + 1;
      size_t r = l + 1;
      size_t small = i;
      if (l < heap.size() && heap_f[l] < heap_f[small]) small = l;
      if (r < heap.size() && heap_f[r] < heap_f[small]) small = r;
      if (small == i) break;
      std::swap(heap[small], heap[i]);
      std::swap(heap_f[small], heap_f[i]);
      i = small;
    }
    return top;
  };

  g[static_cast<size_t>(start)] = 0.0f;
  push(start, dist(nav.nodes[start], nav.nodes[goal]));

  bool found = false;
  while (!heap.empty()) {
    int cur = pop();
    if (closed[static_cast<size_t>(cur)]) continue;
    closed[static_cast<size_t>(cur)] = 1;
    if (cur == goal) {
      found = true;
      break;
    }
    for (int32_t e = nav.link_start[cur]; e < nav.link_start[cur + 1]; ++e) {
      int next = nav.links[e];
      if (closed[static_cast<size_t>(next)]) continue;
      float tentative = g[static_cast<size_t>(cur)] + dist(nav.nodes[cur], nav.nodes[next]);
      if (tentative < g[static_cast<size_t>(next)]) {
        g[static_cast<size_t>(next)] = tentative;
        came[static_cast<size_t>(next)] = cur;
        push(next, tentative + dist(nav.nodes[next], nav.nodes[goal]));
      }
    }
  }
  if (!found) return false;

  // Walk the parent chain back, then reverse into the caller's buffer. A route
  // longer than NAV_MAX_PATH is truncated to its tail: the bot re-plans long
  // before it gets that far, so the near end is the part that matters.
  int chain[NAV_MAX_PATH];
  int len = 0;
  for (int at = goal; at >= 0 && len < NAV_MAX_PATH; at = came[static_cast<size_t>(at)]) {
    chain[len++] = at;
  }
  out->count = len;
  for (int i = 0; i < len; ++i) out->nodes[i] = static_cast<uint16_t>(chain[len - 1 - i]);
  return true;
}
