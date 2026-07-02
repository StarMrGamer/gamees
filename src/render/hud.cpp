#include "render/hud.h"

#include "render/shader.h"
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "vendor/stb_easy_font.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>

constexpr int HUD_BUFFER_BYTES = 1024 * 1024;

static const char* HUD_VS = R"GLSL(
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec4 a_color;
out vec4 v_color;
void main() {
  gl_Position = vec4(a_pos, 0.0, 1.0);
  v_color = a_color;
}
)GLSL";

static const char* HUD_FS = R"GLSL(
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main() {
  out_color = v_color;
}
)GLSL";

bool hud_init(Hud& h) {
  h = {};
  h.program = shader_compile(HUD_VS, HUD_FS);
  glGenVertexArrays(1, &h.vao);
  glGenBuffers(1, &h.vbo);
  glBindVertexArray(h.vao);
  glBindBuffer(GL_ARRAY_BUFFER, h.vbo);
  glBufferData(GL_ARRAY_BUFFER, HUD_BUFFER_BYTES, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(HudVertex), reinterpret_cast<void*>(sizeof(float) * 2));
  glBindVertexArray(0);
  h.verts.reserve(4096);
  return true;
}

void hud_begin(Hud& h, int screen_w, int screen_h) {
  h.screen_w = screen_w;
  h.screen_h = screen_h;
  h.verts.clear();
}

static HudVertex hud_vertex(const Hud& h, float x, float y, Vec3 color, float alpha) {
  float nx = (x / static_cast<float>(h.screen_w)) * 2.0f - 1.0f;
  float ny = 1.0f - (y / static_cast<float>(h.screen_h)) * 2.0f;
  return {nx, ny, color.x, color.y, color.z, alpha};
}

struct EasyFontVertex {
  float x, y, z;
  unsigned char color[4];
};

static void sanitize_text(char* text) {
  for (char* p = text; p && *p; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (c == '\n') continue;
    if (c < 32 || c > 126) *p = '?';
  }
}

void hud_text(Hud& h, float x, float y, float scale, Vec3 color, const char* fmt, ...) {
  char tmp[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(tmp, sizeof(tmp), fmt, args);
  va_end(args);

  if (tmp[0] == 0 || scale <= 0.0f || h.screen_w <= 0 || h.screen_h <= 0) return;
  sanitize_text(tmp);

  char buf[64 * 1024];
  int quads = stb_easy_font_print(0.0f, 0.0f, tmp, nullptr, buf, sizeof(buf));
  const EasyFontVertex* verts = reinterpret_cast<const EasyFontVertex*>(buf);
  for (int i = 0; i < quads; ++i) {
    const EasyFontVertex& a = verts[i * 4 + 0];
    const EasyFontVertex& b = verts[i * 4 + 1];
    const EasyFontVertex& c = verts[i * 4 + 2];
    const EasyFontVertex& d = verts[i * 4 + 3];
    HudVertex ha = hud_vertex(h, x + a.x * scale, y + a.y * scale, color, 1.0f);
    HudVertex hb = hud_vertex(h, x + b.x * scale, y + b.y * scale, color, 1.0f);
    HudVertex hc = hud_vertex(h, x + c.x * scale, y + c.y * scale, color, 1.0f);
    HudVertex hd = hud_vertex(h, x + d.x * scale, y + d.y * scale, color, 1.0f);
    h.verts.push_back(ha);
    h.verts.push_back(hb);
    h.verts.push_back(hc);
    h.verts.push_back(ha);
    h.verts.push_back(hc);
    h.verts.push_back(hd);
  }
}

float hud_text_width(const char* text, float scale) {
  if (!text) text = "";
  char tmp[512];
  std::snprintf(tmp, sizeof(tmp), "%s", text);
  sanitize_text(tmp);
  return static_cast<float>(stb_easy_font_width(tmp)) * scale;
}

void hud_rect(Hud& h, float x, float y, float w, float h_, Vec3 color, float alpha) {
  HudVertex a = hud_vertex(h, x, y, color, alpha);
  HudVertex b = hud_vertex(h, x + w, y, color, alpha);
  HudVertex c = hud_vertex(h, x + w, y + h_, color, alpha);
  HudVertex d = hud_vertex(h, x, y + h_, color, alpha);
  h.verts.push_back(a);
  h.verts.push_back(b);
  h.verts.push_back(c);
  h.verts.push_back(a);
  h.verts.push_back(c);
  h.verts.push_back(d);
}

void hud_end(Hud& h) {
  if (h.verts.empty()) return;
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(h.program);
  glBindVertexArray(h.vao);
  glBindBuffer(GL_ARRAY_BUFFER, h.vbo);
  size_t max_verts = HUD_BUFFER_BYTES / sizeof(HudVertex);
  size_t draw_verts = h.verts.size() < max_verts ? h.verts.size() : max_verts;
  glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(HudVertex) * draw_verts), h.verts.data());
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(draw_verts));
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}
