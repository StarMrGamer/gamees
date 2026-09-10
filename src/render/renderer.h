#pragma once

#include "core/math.h"
#include "game/map.h"
#include "render/mesh.h"

struct Camera {
  Vec3 pos;
  float yaw, pitch;
};

Mat4 camera_view(const Camera& c);

// Transient boxes (players, rockets, pickups, every particle) are batched into
// one pre-transformed vertex buffer and drawn with a single call. The old
// per-box path issued up to MAX_PARTICLES draw calls plus a uniform upload each.
constexpr int RENDERER_MAX_DYNAMIC_BOXES = 2304;
constexpr int RENDERER_BOX_VERTS = 36;

struct Renderer {
  GLuint program;
  GLint u_mvp, u_model, u_light_dir, u_light_color, u_ambient;
  GLint u_fog_color, u_fog_density, u_cam_pos, u_tint;
  // Procedural sky (a fullscreen gradient pass drawn behind the world).
  GLuint sky_program;
  GLuint sky_vao;
  GLint u_sky_right, u_sky_up, u_sky_fwd, u_sky_tan_half, u_sky_aspect;
  GLint u_sky_horizon, u_sky_zenith;
  Mat4 view, proj, view_proj;
  Mesh arena;
  Mesh dynamic_boxes;
  MeshBuilder box_batch;
  Camera camera;
};

bool renderer_init(Renderer& r, const Map& map);
void renderer_begin_frame(Renderer& r, const Camera& cam, int fb_w, int fb_h, const Map& map);
void renderer_draw_world(Renderer& r);
// Queue a world-space box into the dynamic batch; flush draws them all at once.
void renderer_begin_boxes(Renderer& r);
void renderer_queue_box(Renderer& r, Vec3 center, Vec3 size, Vec3 color, float yaw);
void renderer_flush_boxes(Renderer& r);
void renderer_draw_box(Renderer& r, Vec3 center, Vec3 size, Vec3 color, float yaw);
void renderer_end_frame(Renderer& r);
