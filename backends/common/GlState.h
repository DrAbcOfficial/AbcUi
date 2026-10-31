#pragma once

#include "GlApi.h"

#include <array>

namespace gl {

inline void enable(GLenum capability, bool value) {
  if (value)
    glEnable(capability);
  else
    glDisable(capability);
}

// State is scoped to each operation, including draws. VGUI may upload an atlas
// or movie between any two draws: never leave its cached texture binding stale.
// VAO isolation keeps the engine's fixed-pipeline vertex arrays untouched.
class State {
public:
  explicit State(gl::Api &api) : api(api) {
    glGetIntegerv(gl::kCurrentProgram, &program);
    glGetIntegerv(gl::kVertexArrayBinding, &vao);
    glGetIntegerv(gl::kArrayBufferBinding, &buffer);
    glGetIntegerv(gl::kPixelUnpackBufferBinding, &unpackBuffer);
    glGetIntegerv(gl::kReadFramebufferBinding, &read);
    glGetIntegerv(gl::kDrawFramebufferBinding, &draw);
    glGetIntegerv(gl::kActiveTexture, &active);
    for (int i = 0; i < 2; ++i) {
      api.ActiveTexture(gl::kTexture0 + i);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &textures[i]);
      glGetIntegerv(gl::kSamplerBinding, &samplers[i]);
    }
    api.ActiveTexture(active);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    glGetIntegerv(GL_POLYGON_MODE, polygon);
    glGetIntegerv(gl::kBlendSrcRgb, &srcRgb);
    glGetIntegerv(gl::kBlendDstRgb, &dstRgb);
    glGetIntegerv(gl::kBlendSrcAlpha, &srcAlpha);
    glGetIntegerv(gl::kBlendDstAlpha, &dstAlpha);
    glGetIntegerv(gl::kBlendEquationRgb, &equationRgb);
    glGetIntegerv(gl::kBlendEquationAlpha, &equationAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth);
    for (size_t i = 0; i < capabilities.size(); ++i)
      enabled[i] = glIsEnabled(capabilities[i]);
  }

  ~State() {
    api.UseProgram(program);
    api.BindVertexArray(vao);
    api.BindBuffer(gl::kArrayBuffer, buffer);
    api.BindBuffer(gl::kPixelUnpackBuffer, unpackBuffer);
    api.BindFramebuffer(gl::kReadFramebuffer, read);
    api.BindFramebuffer(gl::kDrawFramebuffer, draw);
    for (int i = 0; i < 2; ++i) {
      api.ActiveTexture(gl::kTexture0 + i);
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      api.BindSampler(i, samplers[i]);
    }
    api.ActiveTexture(active);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    glPolygonMode(GL_FRONT, polygon[0]);
    glPolygonMode(GL_BACK, polygon[1]);
    api.BlendFuncSeparate(srcRgb, dstRgb, srcAlpha, dstAlpha);
    api.BlendEquationSeparate(equationRgb, equationAlpha);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthMask);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClearDepth(clearDepth);
    for (size_t i = 0; i < capabilities.size(); ++i)
      enable(capabilities[i], enabled[i] != GL_FALSE);
  }

  GLint read = 0;
  GLint draw = 0;

private:
  gl::Api &api;
  GLint program = 0, vao = 0, buffer = 0, unpackBuffer = 0, active = 0;
  GLint textures[2]{}, samplers[2]{}, viewport[4]{}, scissor[4]{}, polygon[2]{};
  GLint srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0;
  GLint equationRgb = 0, equationAlpha = 0;
  GLboolean colorMask[4]{}, depthMask = GL_FALSE;
  GLfloat clearColor[4]{};
  GLdouble clearDepth = 1;
  static constexpr std::array<GLenum, 13> capabilities{GL_BLEND,
                                                       GL_DEPTH_TEST,
                                                       GL_STENCIL_TEST,
                                                       GL_CULL_FACE,
                                                       GL_SCISSOR_TEST,
                                                       GL_ALPHA_TEST,
                                                       GL_COLOR_LOGIC_OP,
                                                       gl::kFramebufferSrgb,
                                                       gl::kRasterizerDiscard,
                                                       GL_POLYGON_OFFSET_FILL,
                                                       0x809E,
                                                       0x80A0,
                                                       0x0B42};
  // SAMPLE_ALPHA_TO_COVERAGE, SAMPLE_COVERAGE, POLYGON_STIPPLE.
  std::array<GLboolean, capabilities.size()> enabled{};
};

} // namespace gl
