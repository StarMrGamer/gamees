#include "render/shader.h"

#include "core/log.h"

static GLuint compile_stage(GLenum type, const char* src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048]{};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    fatal_error("shader compile failed: %s", log);
  }
  return shader;
}

GLuint shader_compile(const char* vs_src, const char* fs_src) {
  GLuint vs = compile_stage(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile_stage(GL_FRAGMENT_SHADER, fs_src);
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048]{};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    fatal_error("shader link failed: %s", log);
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}
