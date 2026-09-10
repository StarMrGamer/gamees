#include "render/mesh.h"

#include <cmath>
#include <cstddef>

static void setup_mesh(Mesh& m, const Vertex* verts, int count, int capacity, GLenum usage) {
  m.vertex_count = count;
  m.capacity = capacity;
  glGenVertexArrays(1, &m.vao);
  glGenBuffers(1, &m.vbo);
  glBindVertexArray(m.vao);
  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(Vertex) * capacity), verts, usage);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, pos)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
  glBindVertexArray(0);
}

Mesh mesh_create(const Vertex* verts, int count) {
  Mesh m{};
  setup_mesh(m, verts, count, count > 0 ? count : 1, GL_STATIC_DRAW);
  return m;
}

Mesh mesh_create_dynamic(int max_verts) {
  Mesh m{};
  setup_mesh(m, nullptr, 0, max_verts, GL_DYNAMIC_DRAW);
  return m;
}

void mesh_update(Mesh& m, const Vertex* verts, int count) {
  if (count > m.capacity) count = m.capacity;
  m.vertex_count = count;
  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(Vertex) * count), verts);
}

void mesh_draw(const Mesh& m) {
  if (m.vertex_count <= 0) return;
  glBindVertexArray(m.vao);
  glDrawArrays(GL_TRIANGLES, 0, m.vertex_count);
}

void MeshBuilder::add_quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal, Vec3 color) {
  verts.push_back({a, normal, color});
  verts.push_back({b, normal, color});
  verts.push_back({c, normal, color});
  verts.push_back({a, normal, color});
  verts.push_back({c, normal, color});
  verts.push_back({d, normal, color});
}

static Vec3 rotate_y(Vec3 v, float yaw) {
  float s = std::sin(yaw);
  float c = std::cos(yaw);
  return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

void MeshBuilder::add_box_yaw(Vec3 center, Vec3 size, Vec3 color, float yaw) {
  Vec3 h = size * 0.5f;
  auto P = [&](float sx, float sy, float sz) {
    return center + rotate_y({sx * h.x, sy * h.y, sz * h.z}, yaw);
  };
  auto N = [&](Vec3 n) { return rotate_y(n, yaw); };
  Vec3 p100 = P(1, -1, -1), p101 = P(1, -1, 1), p111 = P(1, 1, 1), p110 = P(1, 1, -1);
  Vec3 p001 = P(-1, -1, 1), p000 = P(-1, -1, -1), p010 = P(-1, 1, -1), p011 = P(-1, 1, 1);
  add_quad(p100, p101, p111, p110, N({1, 0, 0}), color);
  add_quad(p001, p000, p010, p011, N({-1, 0, 0}), color);
  add_quad(p010, p110, p111, p011, N({0, 1, 0}), color);
  add_quad(p001, p101, p100, p000, N({0, -1, 0}), color);
  add_quad(p101, p001, p011, p111, N({0, 0, 1}), color);
  add_quad(p000, p100, p110, p010, N({0, 0, -1}), color);
}

void MeshBuilder::add_ramp(Vec3 mn, Vec3 mx, uint8_t dir, Vec3 color) {
  Vec3 v0, v1, v2, v3, v4, v5;
  switch (dir) {
    case 0:  // +x
      v0 = {mn.x, mn.y, mn.z}; v1 = {mx.x, mn.y, mn.z}; v2 = {mx.x, mx.y, mn.z};
      v3 = {mn.x, mn.y, mx.z}; v4 = {mx.x, mn.y, mx.z}; v5 = {mx.x, mx.y, mx.z};
      break;
    case 1:  // -x
      v0 = {mx.x, mn.y, mn.z}; v1 = {mn.x, mn.y, mn.z}; v2 = {mn.x, mx.y, mn.z};
      v3 = {mx.x, mn.y, mx.z}; v4 = {mn.x, mn.y, mx.z}; v5 = {mn.x, mx.y, mx.z};
      break;
    case 2:  // +z
      v0 = {mn.x, mn.y, mn.z}; v1 = {mn.x, mn.y, mx.z}; v2 = {mn.x, mx.y, mx.z};
      v3 = {mx.x, mn.y, mn.z}; v4 = {mx.x, mn.y, mx.z}; v5 = {mx.x, mx.y, mx.z};
      break;
    default:  // -z
      v0 = {mn.x, mn.y, mx.z}; v1 = {mn.x, mn.y, mn.z}; v2 = {mn.x, mx.y, mn.z};
      v3 = {mx.x, mn.y, mx.z}; v4 = {mx.x, mn.y, mn.z}; v5 = {mx.x, mx.y, mn.z};
      break;
  }
  Vec3 center = (v0 + v1 + v2 + v3 + v4 + v5) / 6.0f;
  auto face = [&](Vec3 a, Vec3 b, Vec3 d, Vec3 e, bool tri) {
    Vec3 n = vec3_cross(b - a, d - a);
    if (vec3_length(n) < 1e-6f) return;
    n = vec3_normalize(n);
    Vec3 fc = tri ? (a + b + d) / 3.0f : (a + b + d + e) * 0.25f;
    if (vec3_dot(n, fc - center) < 0.0f) n = -n;
    if (tri) add_quad(a, b, d, d, n, color);
    else add_quad(a, b, d, e, n, color);
  };
  face(v0, v1, v4, v3, false);  // bottom
  face(v0, v3, v5, v2, false);  // sloped top
  face(v1, v2, v5, v4, false);  // tall vertical face
  face(v0, v2, v1, v0, true);   // side
  face(v3, v4, v5, v3, true);   // side
}

void MeshBuilder::add_box(Vec3 mn, Vec3 mx, Vec3 color) {
  Vec3 p000{mn.x, mn.y, mn.z};
  Vec3 p001{mn.x, mn.y, mx.z};
  Vec3 p010{mn.x, mx.y, mn.z};
  Vec3 p011{mn.x, mx.y, mx.z};
  Vec3 p100{mx.x, mn.y, mn.z};
  Vec3 p101{mx.x, mn.y, mx.z};
  Vec3 p110{mx.x, mx.y, mn.z};
  Vec3 p111{mx.x, mx.y, mx.z};
  add_quad(p100, p101, p111, p110, {1, 0, 0}, color);
  add_quad(p001, p000, p010, p011, {-1, 0, 0}, color);
  add_quad(p010, p110, p111, p011, {0, 1, 0}, color);
  add_quad(p001, p101, p100, p000, {0, -1, 0}, color);
  add_quad(p101, p001, p011, p111, {0, 0, 1}, color);
  add_quad(p000, p100, p110, p010, {0, 0, -1}, color);
}
