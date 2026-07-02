#pragma once

#include "core/math.h"
#include "game/map.h"
#include "render/mesh.h"

struct Camera {
  Vec3 pos;
  float yaw, pitch;
};

Mat4 camera_view(const Camera& c);

struct Renderer {
  GLuint program;
  GLint u_mvp, u_model, u_light_dir, u_light_color, u_ambient;
  GLint u_fog_color, u_fog_density, u_cam_pos, u_tint;
  Mat4 view, proj;
  Mesh arena;
  Mesh cube;
  Camera camera;
};

bool renderer_init(Renderer& r, const Map& map);
void renderer_begin_frame(Renderer& r, const Camera& cam, int fb_w, int fb_h, const Map& map);
void renderer_draw_world(Renderer& r);
void renderer_draw_box(Renderer& r, Vec3 center, Vec3 size, Vec3 color, float yaw);
void renderer_end_frame(Renderer& r);
