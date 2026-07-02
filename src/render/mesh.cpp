#include "render/mesh.h"

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
