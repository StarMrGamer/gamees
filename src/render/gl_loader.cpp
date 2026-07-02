#include "render/gl_loader.h"

#include "core/log.h"

#include <SDL3/SDL.h>

PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLDRAWARRAYSPROC glDrawArrays = nullptr;
PFNGLENABLEPROC glEnable = nullptr;
PFNGLDISABLEPROC glDisable = nullptr;
PFNGLCLEARCOLORPROC glClearColor = nullptr;
PFNGLCLEARPROC glClear = nullptr;
PFNGLVIEWPORTPROC glViewport = nullptr;
PFNGLDEPTHFUNCPROC glDepthFunc = nullptr;
PFNGLBLENDFUNCPROC glBlendFunc = nullptr;

template <typename T>
static bool load_one(T& fn, const char* name) {
  fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
  if (!fn) log_error("missing GL function %s", name);
  return fn != nullptr;
}

bool gl_load_functions() {
  bool ok = true;
  ok &= load_one(glCreateShader, "glCreateShader");
  ok &= load_one(glShaderSource, "glShaderSource");
  ok &= load_one(glCompileShader, "glCompileShader");
  ok &= load_one(glGetShaderiv, "glGetShaderiv");
  ok &= load_one(glGetShaderInfoLog, "glGetShaderInfoLog");
  ok &= load_one(glCreateProgram, "glCreateProgram");
  ok &= load_one(glAttachShader, "glAttachShader");
  ok &= load_one(glLinkProgram, "glLinkProgram");
  ok &= load_one(glGetProgramiv, "glGetProgramiv");
  ok &= load_one(glGetProgramInfoLog, "glGetProgramInfoLog");
  ok &= load_one(glDeleteShader, "glDeleteShader");
  ok &= load_one(glDeleteProgram, "glDeleteProgram");
  ok &= load_one(glGenVertexArrays, "glGenVertexArrays");
  ok &= load_one(glBindVertexArray, "glBindVertexArray");
  ok &= load_one(glDeleteVertexArrays, "glDeleteVertexArrays");
  ok &= load_one(glGenBuffers, "glGenBuffers");
  ok &= load_one(glBindBuffer, "glBindBuffer");
  ok &= load_one(glBufferData, "glBufferData");
  ok &= load_one(glBufferSubData, "glBufferSubData");
  ok &= load_one(glDeleteBuffers, "glDeleteBuffers");
  ok &= load_one(glEnableVertexAttribArray, "glEnableVertexAttribArray");
  ok &= load_one(glVertexAttribPointer, "glVertexAttribPointer");
  ok &= load_one(glUseProgram, "glUseProgram");
  ok &= load_one(glGetUniformLocation, "glGetUniformLocation");
  ok &= load_one(glUniformMatrix4fv, "glUniformMatrix4fv");
  ok &= load_one(glUniform3f, "glUniform3f");
  ok &= load_one(glUniform1f, "glUniform1f");
  ok &= load_one(glDrawArrays, "glDrawArrays");
  ok &= load_one(glEnable, "glEnable");
  ok &= load_one(glDisable, "glDisable");
  ok &= load_one(glClearColor, "glClearColor");
  ok &= load_one(glClear, "glClear");
  ok &= load_one(glViewport, "glViewport");
  ok &= load_one(glDepthFunc, "glDepthFunc");
  ok &= load_one(glBlendFunc, "glBlendFunc");
  return ok;
}
