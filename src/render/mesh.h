#pragma once

#include "core/math.h"
#include "render/gl_loader.h"

#include <vector>

struct Vertex {
  Vec3 pos;
  Vec3 normal;
  Vec3 color;
};

struct Mesh {
  GLuint vao, vbo;
  int vertex_count;
  int capacity;
};

Mesh mesh_create(const Vertex* verts, int count);
Mesh mesh_create_dynamic(int max_verts);
void mesh_update(Mesh& m, const Vertex* verts, int count);
void mesh_draw(const Mesh& m);

struct MeshBuilder {
  std::vector<Vertex> verts;
  void add_box(Vec3 mn, Vec3 mx, Vec3 color);
  // A box centered on `center`, sized `size`, rotated about its Y axis by
  // `yaw`. Used by the dynamic box batch so entities and particles can share
  // one pre-transformed vertex buffer.
  void add_box_yaw(Vec3 center, Vec3 size, Vec3 color, float yaw);
  // A wedge with a sloped top rising from min.y to max.y along `dir`.
  void add_ramp(Vec3 mn, Vec3 mx, uint8_t dir, Vec3 color);
  void add_quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal, Vec3 color);
};
