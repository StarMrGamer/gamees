#include "render/renderer.h"

#include "core/log.h"
#include "render/shader.h"

static const char* VS = R"GLSL(
#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec3 a_color;
uniform mat4 u_mvp;
uniform mat4 u_model;
out vec3 v_world;
out vec3 v_normal;
out vec3 v_color;
void main() {
  vec4 world = u_model * vec4(a_pos, 1.0);
  v_world = world.xyz;
  v_normal = mat3(u_model) * a_normal;
  v_color = a_color;
  gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)GLSL";

static const char* FS = R"GLSL(
#version 330 core
in vec3 v_world;
in vec3 v_normal;
in vec3 v_color;
uniform vec3 u_light_dir;
uniform vec3 u_light_color;
uniform vec3 u_ambient;
uniform vec3 u_fog_color;
uniform float u_fog_density;
uniform vec3 u_cam_pos;
uniform vec3 u_tint;
out vec4 out_color;
void main() {
  vec3 n = normalize(v_normal);
  float ndl = max(dot(n, -normalize(u_light_dir)), 0.0);
  vec3 lit = v_color * u_tint * (u_ambient + u_light_color * ndl);
  float d = length(u_cam_pos - v_world);
  float fogf = clamp(1.0 - exp(-u_fog_density * d), 0.0, 1.0);
  out_color = vec4(mix(lit, u_fog_color, fogf), 1.0);
}
)GLSL";

Mat4 camera_view(const Camera& c) {
  Vec3 eye = c.pos;
  Vec3 center = eye + angles_forward(c.yaw, c.pitch);
  return mat4_look_at(eye, center, {0, 1, 0});
}

static void set_common_uniforms(Renderer& r, const Map& map) {
  glUniform3f(r.u_light_dir, map.light_dir.x, map.light_dir.y, map.light_dir.z);
  glUniform3f(r.u_light_color, 0.92f, 0.88f, 0.78f);
  glUniform3f(r.u_ambient, 0.30f, 0.34f, 0.38f);
  glUniform3f(r.u_fog_color, map.fog_color.x, map.fog_color.y, map.fog_color.z);
  glUniform1f(r.u_fog_density, map.fog_density);
  glUniform3f(r.u_cam_pos, r.camera.pos.x, r.camera.pos.y, r.camera.pos.z);
}

bool renderer_init(Renderer& r, const Map& map) {
  if (!gl_load_functions()) return false;
  r.program = shader_compile(VS, FS);
  r.u_mvp = glGetUniformLocation(r.program, "u_mvp");
  r.u_model = glGetUniformLocation(r.program, "u_model");
  r.u_light_dir = glGetUniformLocation(r.program, "u_light_dir");
  r.u_light_color = glGetUniformLocation(r.program, "u_light_color");
  r.u_ambient = glGetUniformLocation(r.program, "u_ambient");
  r.u_fog_color = glGetUniformLocation(r.program, "u_fog_color");
  r.u_fog_density = glGetUniformLocation(r.program, "u_fog_density");
  r.u_cam_pos = glGetUniformLocation(r.program, "u_cam_pos");
  r.u_tint = glGetUniformLocation(r.program, "u_tint");

  MeshBuilder world;
  for (int i = 0; i < map.box_count; ++i) {
    world.add_box(map.boxes[i].min, map.boxes[i].max, map.boxes[i].color);
  }
  r.arena = mesh_create(world.verts.data(), static_cast<int>(world.verts.size()));

  MeshBuilder cube;
  cube.add_box({-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {1, 1, 1});
  r.cube = mesh_create(cube.verts.data(), static_cast<int>(cube.verts.size()));
  return true;
}

void renderer_begin_frame(Renderer& r, const Camera& cam, int fb_w, int fb_h, const Map& map) {
  r.camera = cam;
  float aspect = fb_h > 0 ? static_cast<float>(fb_w) / static_cast<float>(fb_h) : 1.0f;
  r.view = camera_view(cam);
  r.proj = mat4_perspective(70.0f * PI / 180.0f, aspect, 0.05f, 500.0f);
  glViewport(0, 0, fb_w, fb_h);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glClearColor(map.sky_color.x, map.sky_color.y, map.sky_color.z, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(r.program);
  set_common_uniforms(r, map);
}

void renderer_draw_world(Renderer& r) {
  Mat4 model = mat4_identity();
  Mat4 mvp = mat4_mul(r.proj, mat4_mul(r.view, model));
  glUniformMatrix4fv(r.u_model, 1, GL_FALSE, model.m);
  glUniformMatrix4fv(r.u_mvp, 1, GL_FALSE, mvp.m);
  glUniform3f(r.u_tint, 1.0f, 1.0f, 1.0f);
  mesh_draw(r.arena);
}

void renderer_draw_box(Renderer& r, Vec3 center, Vec3 size, Vec3 color, float yaw) {
  Mat4 model = mat4_mul(mat4_translate(center), mat4_mul(mat4_rotate_y(yaw), mat4_scale(size)));
  Mat4 mvp = mat4_mul(r.proj, mat4_mul(r.view, model));
  glUniformMatrix4fv(r.u_model, 1, GL_FALSE, model.m);
  glUniformMatrix4fv(r.u_mvp, 1, GL_FALSE, mvp.m);
  glUniform3f(r.u_tint, color.x, color.y, color.z);
  mesh_draw(r.cube);
}

void renderer_end_frame(Renderer&) {}
