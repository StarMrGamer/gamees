#pragma once

#include "core/math.h"
#include "render/gl_loader.h"

#include <vector>

struct HudVertex {
  float x, y;
  float r, g, b, a;
};

struct Hud {
  int screen_w;
  int screen_h;
  GLuint program;
  GLuint vao;
  GLuint vbo;
  std::vector<HudVertex> verts;
};

bool hud_init(Hud& h);
void hud_begin(Hud& h, int screen_w, int screen_h);
void hud_text(Hud& h, float x, float y, float scale, Vec3 color, const char* fmt, ...);
float hud_text_width(const char* text, float scale);
void hud_rect(Hud& h, float x, float y, float w, float h_, Vec3 color, float alpha);
void hud_end(Hud& h);
