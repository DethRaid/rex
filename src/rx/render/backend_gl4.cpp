#include <SDL.h>
#include <SDL_opengl.h>

#include <rx/render/backend_gl4.h>
#include <rx/render/command.h>
#include <rx/render/buffer.h>
#include <rx/render/target.h>
#include <rx/render/program.h>
#include <rx/render/texture.h>

#include <rx/core/algorithm.h>
#include <rx/core/debug.h>
#include <rx/core/log.h>

#include <rx/core/filesystem/file.h>

#include <rx/math/log2.h>

RX_LOG("render/gl4", gl4_log);

namespace rx::render {

// buffers
static void (GLAPIENTRYP pglCreateBuffers)(GLsizei, GLuint*);
static void (GLAPIENTRYP pglDeleteBuffers)(GLsizei, const GLuint*);
static void (GLAPIENTRYP pglNamedBufferData)(GLuint, GLsizeiptr, const void*, GLenum);
static void (GLAPIENTRYP pglNamedBufferSubData)(GLuint, GLintptr, GLsizeiptr, const void*);

// vertex arrays
static void (GLAPIENTRYP pglCreateVertexArrays)(GLsizei, GLuint*);
static void (GLAPIENTRYP pglDeleteVertexArrays)(GLsizei, const GLuint*);
static void (GLAPIENTRYP pglVertexArrayVertexBuffer)(GLuint, GLuint, GLuint, GLintptr, GLsizei);
static void (GLAPIENTRYP pglVertexArrayElementBuffer)(GLuint, GLuint);
static void (GLAPIENTRYP pglEnableVertexArrayAttrib)(GLuint, GLuint);
static void (GLAPIENTRYP pglVertexArrayAttribFormat)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint);
static void (GLAPIENTRYP pglVertexArrayAttribBinding)(GLuint, GLuint, GLuint);
static void (GLAPIENTRYP pglBindVertexArray)(GLuint);

// textures
static void (GLAPIENTRYP pglCreateTextures)(GLenum, GLsizei, GLuint*);
static void (GLAPIENTRYP pglDeleteTextures)(GLsizei, const GLuint*);
static void (GLAPIENTRYP pglTextureStorage1D)(GLuint, GLsizei, GLenum, GLsizei);
static void (GLAPIENTRYP pglTextureStorage2D)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
static void (GLAPIENTRYP pglTextureStorage3D)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
static void (GLAPIENTRYP pglTextureSubImage1D)(GLuint, GLint, GLint, GLsizei, GLenum, GLenum, const void*);
static void (GLAPIENTRYP pglTextureSubImage2D)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
static void (GLAPIENTRYP pglTextureSubImage3D)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*);
static void (GLAPIENTRYP pglTextureParameteri)(GLuint, GLenum, GLint);
static void (GLAPIENTRYP pglTextureParameterf)(GLuint, GLenum, GLfloat);
static void (GLAPIENTRYP pglGenerateTextureMipmap)(GLuint);
static void (GLAPIENTRYP pglBindTextureUnit)(GLuint, GLuint);
static void (GLAPIENTRYP pglPixelStorei)(GLenum, GLint);

// frame buffers
static void (GLAPIENTRYP pglCreateFramebuffers)(GLsizei, GLuint*);
static void (GLAPIENTRYP pglDeleteFramebuffers)(GLsizei, const GLuint*);
static void (GLAPIENTRYP pglNamedFramebufferTexture)(GLuint, GLenum, GLuint, GLint);
static void (GLAPIENTRYP pglBindFramebuffer)(GLenum, GLuint);
static void (GLAPIENTRYP pglClearNamedFramebufferfv)(GLuint, GLenum, GLint, const GLfloat*);
static void (GLAPIENTRYP pglClearNamedFramebufferfi)(GLuint, GLenum, GLint, GLfloat, GLint);

// shaders and programs
static void (GLAPIENTRYP pglShaderSource)(GLuint, GLsizei, const GLchar**, const GLint*);
static GLuint (GLAPIENTRYP pglCreateShader)(GLenum);
static void (GLAPIENTRYP pglDeleteShader)(GLuint);
static void (GLAPIENTRYP pglCompileShader)(GLuint);
static void (GLAPIENTRYP pglGetShaderiv)(GLuint, GLenum, GLint*);
static void (GLAPIENTRYP pglGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static void (GLAPIENTRYP pglGetProgramiv)(GLuint, GLenum, GLint*);
static void (GLAPIENTRYP pglGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
static void (GLAPIENTRYP pglAttachShader)(GLuint, GLuint);
static void (GLAPIENTRYP pglLinkProgram)(GLuint);
static void (GLAPIENTRYP pglDetachShader)(GLuint, GLuint);
static GLuint (GLAPIENTRYP pglCreateProgram)();
static void (GLAPIENTRYP pglDeleteProgram)(GLuint);
static void (GLAPIENTRYP pglUseProgram)(GLuint);
static GLint (GLAPIENTRYP pglGetUniformLocation)(GLuint, const GLchar*);

// state
static void (GLAPIENTRYP pglEnable)(GLenum);
static void (GLAPIENTRYP pglDisable)(GLenum);
static void (GLAPIENTRYP pglScissor)(GLint, GLint, GLsizei, GLsizei);
static void (GLAPIENTRYP pglColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
static void (GLAPIENTRYP pglBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
static void (GLAPIENTRYP pglDepthFunc)(GLenum);
static void (GLAPIENTRYP pglDepthMask)(GLboolean);
static void (GLAPIENTRYP pglFrontFace)(GLenum);
static void (GLAPIENTRYP pglCullFace)(GLenum);
static void (GLAPIENTRYP pglStencilMask)(GLuint);
static void (GLAPIENTRYP pglStencilFunc)(GLenum, GLint, GLuint);
static void (GLAPIENTRYP pglStencilOpSeparate)(GLenum, GLenum, GLenum, GLenum);
static void (GLAPIENTRYP pglPolygonMode)(GLenum, GLenum);
static void (GLAPIENTRYP pglViewport)(GLint, GLint, GLsizei, GLsizei);

// query
static void (GLAPIENTRYP pglGetIntegerv)(GLenum, GLint*);
static void (GLAPIENTRYP pglGetFloatv)(GLenum, GLfloat*);
static const GLubyte* (GLAPIENTRYP pglGetString)(GLenum);
static const GLubyte* (GLAPIENTRYP pglGetStringi)(GLenum, GLuint);

// draw calls
static void (GLAPIENTRYP pglDrawArrays)(GLenum, GLint, GLsizei);
static void (GLAPIENTRYP pglDrawElements)(GLenum, GLsizei, GLenum, const GLvoid*);

// utility functions to convert rx rhi to gl
static GLenum convert_blend_factor(render::blend_state::factor_type _factor_type) {
  switch (_factor_type) {
  case render::blend_state::factor_type::k_constant_alpha:
    return GL_CONSTANT_ALPHA;
  case render::blend_state::factor_type::k_constant_color:
    return GL_CONSTANT_COLOR;
  case render::blend_state::factor_type::k_dst_alpha:
    return GL_DST_ALPHA;
  case render::blend_state::factor_type::k_dst_color:
    return GL_DST_COLOR;
  case render::blend_state::factor_type::k_one:
    return GL_ONE;
  case render::blend_state::factor_type::k_one_minus_constant_alpha:
    return GL_ONE_MINUS_CONSTANT_ALPHA;
  case render::blend_state::factor_type::k_one_minus_constant_color:
    return GL_ONE_MINUS_CONSTANT_COLOR;
  case render::blend_state::factor_type::k_one_minus_dst_alpha:
    return GL_ONE_MINUS_DST_ALPHA;
  case render::blend_state::factor_type::k_one_minus_dst_color:
    return GL_ONE_MINUS_DST_COLOR;
  case render::blend_state::factor_type::k_one_minus_src_alpha:
    return GL_ONE_MINUS_SRC_ALPHA;
  case render::blend_state::factor_type::k_one_minus_src_color:
    return GL_ONE_MINUS_SRC_COLOR;
  case render::blend_state::factor_type::k_src_alpha:
    return GL_SRC_ALPHA;
  case render::blend_state::factor_type::k_src_alpha_saturate:
    return GL_SRC_ALPHA_SATURATE;
  case render::blend_state::factor_type::k_src_color:
    return GL_SRC_COLOR;
  case render::blend_state::factor_type::k_zero:
    return GL_ZERO;
  }
  RX_ASSERT(false, "unreachable");
}

static GLenum convert_stencil_operation(render::stencil_state::operation_type _operation_type) {
  switch (_operation_type) {
  case render::stencil_state::operation_type::k_decrement:
    return GL_DECR;
  case render::stencil_state::operation_type::k_decrement_wrap:
    return GL_DECR_WRAP;
  case render::stencil_state::operation_type::k_increment:
    return GL_INCR;
  case render::stencil_state::operation_type::k_increment_wrap:
    return GL_INCR_WRAP;
  case render::stencil_state::operation_type::k_invert:
    return GL_INVERT;
  case render::stencil_state::operation_type::k_keep:
    return GL_KEEP;
  case render::stencil_state::operation_type::k_replace:
    return GL_REPLACE;
  case render::stencil_state::operation_type::k_zero:
    return GL_ZERO;
  }
  RX_ASSERT(false, "unreachable");
}

static GLenum convert_stencil_function(render::stencil_state::function_type _function_type) {
  switch (_function_type) {
  case render::stencil_state::function_type::k_always:
    return GL_ALWAYS;
  case render::stencil_state::function_type::k_equal:
    return GL_EQUAL;
  case render::stencil_state::function_type::k_greater:
    return GL_GREATER;
  case render::stencil_state::function_type::k_greater_equal:
    return GL_GEQUAL;
  case render::stencil_state::function_type::k_less:
    return GL_LESS;
  case render::stencil_state::function_type::k_less_equal:
    return GL_LEQUAL;
  case render::stencil_state::function_type::k_never:
    return GL_NEVER;
  case render::stencil_state::function_type::k_not_equal:
    return GL_NOTEQUAL;
  }
  RX_ASSERT(false, "unreachable");
}

static GLenum convert_polygon_mode(render::polygon_state::mode_type _mode_type) {
  switch (_mode_type) {
  case render::polygon_state::mode_type::k_fill:
    return GL_FILL;
  case render::polygon_state::mode_type::k_line:
    return GL_LINE;
  case render::polygon_state::mode_type::k_point:
    return GL_POINT;
  }
  RX_ASSERT(false, "unreachable");
}

static GLenum convert_texture_data_format(render::texture::data_format _data_format) {
  switch (_data_format) {
  case render::texture::data_format::k_rgba_u8:
    return GL_RGBA8;
  case render::texture::data_format::k_bgra_u8:
    return GL_RGBA8; // not a bug
  case render::texture::data_format::k_d16:
    return GL_DEPTH_COMPONENT16;
  case render::texture::data_format::k_d24:
    return GL_DEPTH_COMPONENT24;
  case render::texture::data_format::k_d32:
    return GL_DEPTH_COMPONENT32;
  case render::texture::data_format::k_d32f:
    return GL_DEPTH_COMPONENT32F;
  case render::texture::data_format::k_d24_s8:
    return GL_DEPTH24_STENCIL8;
  case render::texture::data_format::k_d32f_s8:
    return GL_DEPTH32F_STENCIL8;
  case render::texture::data_format::k_s8:
    return GL_STENCIL_INDEX8;
  }
  RX_ASSERT(false, "unreachable");
}

static GLenum convert_texture_format(render::texture::data_format _data_format) {
  switch (_data_format) {
  case render::texture::data_format::k_rgba_u8:
    return GL_RGBA;
  case render::texture::data_format::k_bgra_u8:
    return GL_BGRA; // not a bug
  case render::texture::data_format::k_d16:
    return GL_DEPTH_COMPONENT;
  case render::texture::data_format::k_d24:
    return GL_DEPTH_COMPONENT;
  case render::texture::data_format::k_d32:
    return GL_DEPTH_COMPONENT;
  case render::texture::data_format::k_d32f:
    return GL_DEPTH_COMPONENT;
  case render::texture::data_format::k_d24_s8:
    return GL_DEPTH_STENCIL;
  case render::texture::data_format::k_d32f_s8:
    return GL_DEPTH_STENCIL;
  case render::texture::data_format::k_s8:
    return GL_STENCIL_INDEX;
  }
  RX_ASSERT(false, "unreachable");
}

static GLenum convert_primitive_type(render::primitive_type _primitive_type) {
  switch (_primitive_type) {
  case render::primitive_type::k_triangles:
    return GL_TRIANGLES;
  }
  RX_ASSERT(false, "unreachable");
}

struct filter {
  GLuint min;
  GLuint mag;
};

static filter convert_texture_filter(const render::texture::filter_options& _filter_options) {
  static constexpr const GLenum k_min_table[]{
    GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST,
    GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR
  };

  int filter_index{_filter_options.bilinear ? 1 : 0};
  filter_index |= _filter_options.mip_maps << 1;
  filter_index |= _filter_options.trilinear << 2;

  const GLenum mag{static_cast<GLenum>(filter_index & 1 ? GL_LINEAR : GL_NEAREST)};
  const GLenum min{k_min_table[filter_index]};

  return {min, mag};
}

static GLenum convert_texture_wrap(const render::texture::wrap_options::category _category) {
  switch (_category) {
  case render::texture::wrap_options::category::k_clamp_to_edge:
    return GL_CLAMP_TO_EDGE;
  case render::texture::wrap_options::category::k_clamp_to_border:
    return GL_CLAMP_TO_BORDER;
  case render::texture::wrap_options::category::k_mirrored_repeat:
    return GL_MIRRORED_REPEAT;
  case render::texture::wrap_options::category::k_repeat:
    return GL_REPEAT;
  }
  RX_ASSERT(false, "unreachable");
}

namespace detail {
  struct buffer {
    buffer() {
      pglCreateBuffers(2, bo);
      pglCreateVertexArrays(1, &va);
    }

    ~buffer() {
      pglDeleteBuffers(2, bo);
      pglDeleteVertexArrays(1, &va);
    }

    GLuint bo[2];
    GLuint va;
  };

  struct target {
    target() {
      pglCreateFramebuffers(1, &fbo);
    }

    ~target() {
      pglDeleteFramebuffers(1, &fbo);
    }

    GLuint fbo;
  };

  struct program {
    program() {
      handle = pglCreateProgram();
    }

    ~program() {
      pglDeleteProgram(handle);
    }

    GLuint handle;
    array<GLint> uniforms;
  };

  struct texture1D {
    texture1D() {
      pglCreateTextures(GL_TEXTURE_1D, 1, &tex);
    }

    ~texture1D() {
      pglDeleteTextures(1, &tex);
    }

    GLuint tex;
  };

  struct texture2D {
    texture2D() {
      pglCreateTextures(GL_TEXTURE_2D, 1, &tex);
    }

    ~texture2D() {
      pglDeleteTextures(1, &tex);
    }

    GLuint tex;
  };

  struct texture3D {
    texture3D() {
      pglCreateTextures(GL_TEXTURE_3D, 1, &tex);
    }

    ~texture3D() {
      pglDeleteTextures(1, &tex);
    }

    GLuint tex;
  };

  struct textureCM {
    textureCM() {
      pglCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &tex);
    }

    ~textureCM() {
      pglDeleteTextures(1, &tex);
    }

    GLuint tex;
  };

  struct state : ::rx::render::state {
    state() {
      pglFrontFace(GL_CW);
      pglDepthFunc(GL_LEQUAL);
      pglGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_swap_chain_fbo);
      pglDisable(GL_MULTISAMPLE);

      const auto vendor{reinterpret_cast<const char*>(pglGetString(GL_VENDOR))};
      const auto renderer{reinterpret_cast<const char*>(pglGetString(GL_RENDERER))};
      const auto version{reinterpret_cast<const char*>(pglGetString(GL_VERSION))};

      gl4_log(log::level::k_info, "GL %s %s %s", vendor, version, renderer);

      GLint extensions{0};
      pglGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
      for (GLint i{0}; i < extensions; i++) {
        const auto name{reinterpret_cast<const char*>(pglGetStringi(GL_EXTENSIONS, i))};
        gl4_log(log::level::k_verbose, "extension '%s' supported", name);
      }
    }

    void use_enable(GLenum _thing, bool _enable) {
      if (_enable) {
        pglEnable(_thing);
      } else {
        pglDisable(_thing);
      }
    }

    void use_state(const ::rx::render::state* _render_state) {
      const auto& scissor{_render_state->scissor};
      const auto& blend{_render_state->blend};
      const auto& cull{_render_state->cull};
      const auto& stencil{_render_state->stencil};
      const auto& polygon{_render_state->polygon};

      if (this->scissor != scissor) {
        const auto enabled{scissor.enabled()};
        const auto offset{scissor.offset()};
        const auto size{scissor.size()};

        if (this->scissor.enabled() != enabled) {
          use_enable(GL_SCISSOR_TEST, enabled);
          this->scissor.record_enable(enabled);
        }

        if (enabled) {
          if (this->scissor.offset() != offset || this->scissor.size() != size) {
            pglScissor(offset.x, offset.y, size.w, size.h);

            this->scissor.record_offset(offset);
            this->scissor.record_size(size);
          }
        }
      }

      if (this->blend != blend) {
        const auto enabled{blend.enabled()};
        const auto color_src_factor{blend.color_src_factor()};
        const auto color_dst_factor{blend.color_dst_factor()};
        const auto alpha_src_factor{blend.alpha_src_factor()};
        const auto alpha_dst_factor{blend.alpha_dst_factor()};
        const auto write_mask{blend.write_mask()};

        if (this->blend.enabled() != enabled) {
          use_enable(GL_BLEND, enabled);
          this->blend.record_enable(enabled);
        }

        if (enabled) {
          if (this->blend.write_mask() != write_mask) {
            if (write_mask != m_color_mask) {
              const bool r{!!(write_mask & (1 << 0))};
              const bool g{!!(write_mask & (1 << 1))};
              const bool b{!!(write_mask & (1 << 2))};
              const bool a{!!(write_mask & (1 << 3))};
              pglColorMask(r, g, b, a);
              m_color_mask = write_mask;
              this->blend.record_write_mask(write_mask);
            }
          }

          if (this->blend.color_src_factor() != color_src_factor ||
              this->blend.color_dst_factor() != color_dst_factor ||
              this->blend.alpha_src_factor() != alpha_src_factor ||
              this->blend.alpha_dst_factor() != alpha_dst_factor)
          {
            pglBlendFuncSeparate(
              convert_blend_factor(color_src_factor),
              convert_blend_factor(color_dst_factor),
              convert_blend_factor(alpha_src_factor),
              convert_blend_factor(alpha_dst_factor));

            this->blend.record_color_blend_factors(color_src_factor, color_dst_factor);
            this->blend.record_alpha_blend_factors(alpha_src_factor, alpha_dst_factor);
          }
        }
      }

      if (this->depth != depth) {
        const auto test{depth.test()};
        const auto write{depth.write()};

        if (this->depth.test() != test) {
          use_enable(GL_DEPTH_TEST, test);
          this->depth.record_test(test);
        }

        if (test) {
          if (this->depth.write() != write) {
            pglDepthMask(write ? GL_TRUE : GL_FALSE);
            this->depth.record_write(write);
          }
        }
      }

      if (this->cull != cull) {
        const auto front_face{cull.front_face()};
        const auto cull_face{cull.cull_face()};
        const auto enabled{cull.enabled()};

        if (this->cull.enabled() != enabled) {
          use_enable(GL_CULL_FACE, enabled);
          this->cull.record_enable(enabled);
        }

        if (enabled) {
          if (this->cull.front_face() != front_face) {
            switch (front_face) {
            case cull_state::front_face_type::k_clock_wise:
              pglFrontFace(GL_CW);
              break;
            case cull_state::front_face_type::k_counter_clock_wise:
              pglFrontFace(GL_CCW);
              break;
            }
            this->cull.record_front_face(front_face);
          }

          if (this->cull.cull_face() != cull_face) {
            switch (cull_face) {
            case cull_state::cull_face_type::k_front:
              pglCullFace(GL_FRONT);
              break;
            case cull_state::cull_face_type::k_back:
              pglCullFace(GL_BACK);
              break;
            }
            this->cull.record_cull_face(cull_face);
          }
        }
      }

      if (this->stencil != stencil) {
        const auto enabled{stencil.enabled()};
        const auto write_mask{stencil.write_mask()};
        const auto function{stencil.function()};
        const auto reference{stencil.reference()};
        const auto mask{stencil.mask()};
        const auto front_fail_action{stencil.front_fail_action()};
        const auto front_depth_fail_action{stencil.front_depth_fail_action()};
        const auto front_depth_pass_action{stencil.front_depth_pass_action()};
        const auto back_fail_action{stencil.back_fail_action()};
        const auto back_depth_fail_action{stencil.back_depth_fail_action()};
        const auto back_depth_pass_action{stencil.back_depth_pass_action()};

        if (this->stencil.enabled() != enabled) {
          use_enable(GL_STENCIL_TEST, enabled);
          this->stencil.record_enable(enabled);
        }

        if (enabled) {
          if (this->stencil.write_mask() != write_mask) {
            pglStencilMask(write_mask);
            this->stencil.record_write_mask(write_mask);
          }

          if (this->stencil.function() != function ||
              this->stencil.reference() != reference ||
              this->stencil.mask() != mask)
          {
            pglStencilFunc(
              convert_stencil_function(function),
              static_cast<GLint>(reference),
              static_cast<GLuint>(mask));

            this->stencil.record_function(function);
            this->stencil.record_reference(reference);
            this->stencil.record_mask(mask);
          }

          if (this->stencil.front_fail_action() != front_fail_action ||
              this->stencil.front_depth_fail_action() != front_depth_fail_action ||
              this->stencil.front_depth_pass_action() != front_depth_pass_action)
          {
            pglStencilOpSeparate(
              GL_FRONT,
              convert_stencil_operation(front_fail_action),
              convert_stencil_operation(front_depth_fail_action),
              convert_stencil_operation(front_depth_pass_action));

            this->stencil.record_front_fail_action(front_fail_action);
            this->stencil.record_front_depth_fail_action(front_depth_fail_action);
            this->stencil.record_front_depth_pass_action(front_depth_pass_action);
          }

          if (this->stencil.back_fail_action() != back_fail_action ||
              this->stencil.back_depth_fail_action() != back_depth_fail_action ||
              this->stencil.back_depth_pass_action() != back_depth_pass_action)
          {
            pglStencilOpSeparate(
              GL_BACK,
              convert_stencil_operation(back_fail_action),
              convert_stencil_operation(back_depth_fail_action),
              convert_stencil_operation(back_depth_pass_action));

            this->stencil.record_back_fail_action(back_fail_action);
            this->stencil.record_back_depth_fail_action(back_depth_fail_action);
            this->stencil.record_back_depth_pass_action(back_depth_pass_action);
          }
        }
      }

      if (this->polygon != polygon) {
        const auto mode{polygon.mode()};
        pglPolygonMode(GL_FRONT_AND_BACK, convert_polygon_mode(mode));
        this->polygon.record_mode(mode);
      }

      // flush all changes to this for updated hash
      flush();
    }

    void use_target(const ::rx::render::target* _render_target) {
      const auto this_target{reinterpret_cast<const target*>(_render_target + 1)};
      if (this_target->fbo != m_bound_fbo) {
        pglBindFramebuffer(GL_FRAMEBUFFER, this_target->fbo);
        m_bound_fbo = this_target->fbo;
      }
    }

    void use_buffer(const ::rx::render::buffer* _render_buffer) {
      const auto this_buffer{reinterpret_cast<const buffer*>(_render_buffer + 1)};
      if (this_buffer->va != m_bound_vao) {
        pglBindVertexArray(this_buffer->va);
        m_bound_vao = this_buffer->va;
      }
    }

    rx_u8 m_color_mask;

    GLint m_swap_chain_fbo;

    GLuint m_bound_vao;
    GLuint m_bound_fbo;
  };
};

template<typename F>
static void fetch(const char* _name, F& function_) {
  auto address{SDL_GL_GetProcAddress(_name)};
  gl4_log(log::level::k_verbose, "loaded %08p '%s'", address, _name);
  *reinterpret_cast<void**>(&function_) = address;
}

static GLuint compile_shader(GLenum _type, const string& _file_name) {
  filesystem::file file{_file_name.data(), "rb"};
  if (!file) {
    gl4_log(log::level::k_error, "failed to open '%s'", _file_name);
    return 0;
  }
  
  auto size{file.size()};
  if (!size) {
    gl4_log(log::level::k_error, "failed to size '%s'", _file_name);
    return 0;
  }

  // NOTE: size + 1 because we inject a null-terminator on the contents
  array<char> data{&memory::g_system_allocator, *size + 1, 0};
  if (!file.read(reinterpret_cast<rx_byte*>(data.data()), data.size())) {
    gl4_log(log::level::k_error, "failed to read '%s'", _file_name);
    return 0;
  }

  const GLchar* texts[]{data.data()};
  const GLint sizes[]{static_cast<GLint>(data.size())};

  GLuint handle{pglCreateShader(_type)};
  pglShaderSource(handle, 1, texts, sizes);

  GLint status{0};
  pglGetShaderiv(handle, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    GLint log_size{0};
    pglGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_size);

    gl4_log(log::level::k_error, "failed compiling shader '%s'", _file_name);

    if (log_size) {
      array<char> error_log{&memory::g_system_allocator, static_cast<rx_size>(log_size)};
      pglGetShaderInfoLog(handle, log_size, &log_size, error_log.data());
      gl4_log(log::level::k_error, "\n%s\n%s", error_log.data(), data.data());
    }

    pglDeleteShader(handle);
    return 0;
  }

  return handle;
}

backend_gl4::backend_gl4(frontend::allocation_info& allocation_info_) {
  allocation_info_.buffer_size = sizeof(detail::buffer);
  allocation_info_.target_size = sizeof(detail::target);
  allocation_info_.program_size = sizeof(detail::program);
  allocation_info_.texture1D_size = sizeof(detail::texture1D);
  allocation_info_.texture2D_size = sizeof(detail::texture2D);
  allocation_info_.texture3D_size = sizeof(detail::texture3D);
  allocation_info_.textureCM_size = sizeof(detail::textureCM);

  // buffers
  fetch("glCreateBuffers", pglCreateBuffers);
  fetch("glDeleteBuffers", pglDeleteBuffers);
  fetch("glNamedBufferData", pglNamedBufferData);
  fetch("glNamedBufferSubData", pglNamedBufferSubData);

  // vertex arrays
  fetch("glCreateVertexArrays", pglCreateVertexArrays);
  fetch("glDeleteVertexArrays", pglDeleteVertexArrays);
  fetch("glVertexArrayVertexBuffer", pglVertexArrayVertexBuffer);
  fetch("glVertexArrayElementBuffer", pglVertexArrayElementBuffer);
  fetch("glEnableVertexArrayAttrib", pglEnableVertexArrayAttrib);
  fetch("glVertexArrayAttribFormat", pglVertexArrayAttribFormat);
  fetch("glVertexArrayAttribBinding", pglVertexArrayAttribBinding);
  fetch("glBindVertexArray", pglBindVertexArray);

  // textures
  fetch("glCreateTextures", pglCreateTextures);
  fetch("glDeleteTextures", pglDeleteTextures);
  fetch("glTextureStorage1D", pglTextureStorage1D);
  fetch("glTextureStorage2D", pglTextureStorage2D);
  fetch("glTextureStorage3D", pglTextureStorage3D);
  fetch("glTextureSubImage1D", pglTextureSubImage1D);
  fetch("glTextureSubImage2D", pglTextureSubImage2D);
  fetch("glTextureSubImage3D", pglTextureSubImage3D);
  fetch("glTextureParameteri", pglTextureParameteri);
  fetch("glTextureParameterf", pglTextureParameterf);
  fetch("glGenerateTextureMipmap", pglGenerateTextureMipmap);
  fetch("glBindTextureUnit", pglBindTextureUnit);
  fetch("glPixelStorei", pglPixelStorei);

  // frame buffers
  fetch("glCreateFramebuffers", pglCreateFramebuffers);
  fetch("glDeleteFramebuffers", pglDeleteFramebuffers);
  fetch("glNamedFramebufferTexture", pglNamedFramebufferTexture);
  fetch("glBindFramebuffer", pglBindFramebuffer);
  fetch("glClearNamedFramebufferfv", pglClearNamedFramebufferfv);
  fetch("glClearNamedFramebufferfi", pglClearNamedFramebufferfi);

  // shaders and programs
  fetch("glShaderSource", pglShaderSource);
  fetch("glCreateShader", pglCreateShader);
  fetch("glDeleteShader", pglDeleteShader);
  fetch("glCompileShader", pglCompileShader);
  fetch("glGetShaderiv", pglGetShaderiv);
  fetch("glGetShaderInfoLog", pglGetShaderInfoLog);
  fetch("glGetProgramiv", pglGetProgramiv);
  fetch("glGetProgramInfoLog", pglGetProgramInfoLog);
  fetch("glAttachShader", pglAttachShader);
  fetch("glLinkProgram", pglLinkProgram);
  fetch("glDetachShader", pglDetachShader);
  fetch("glCreateProgram", pglCreateProgram);
  fetch("glDeleteProgram", pglDeleteProgram);
  fetch("glUseProgram", pglUseProgram);
  fetch("glGetUniformLocation", pglGetUniformLocation);

  // state
  fetch("glEnable", pglEnable);
  fetch("glDisable", pglDisable);
  fetch("glScissor", pglScissor);
  fetch("glColorMask", pglColorMask);
  fetch("glBlendFuncSeparate", pglBlendFuncSeparate);
  fetch("glDepthFunc", pglDepthFunc);
  fetch("glDepthMask", pglDepthMask);
  fetch("glFrontFace", pglFrontFace);
  fetch("glCullFace", pglCullFace);
  fetch("glStencilMask", pglStencilMask);
  fetch("glStencilFunc", pglStencilFunc);
  fetch("glStencilOpSeparate", pglStencilOpSeparate);
  fetch("glPolygonMode", pglPolygonMode);
  fetch("glViewport", pglViewport);

  // query
  fetch("glGetIntegerv", pglGetIntegerv);
  fetch("glGetFloatv", pglGetFloatv);
  fetch("glGetString", pglGetString);
  fetch("glGetStringi", pglGetStringi);

  // draw calls
  fetch("glDrawArrays", pglDrawArrays);
  fetch("glDrawElements", pglDrawElements);

  m_impl = malloc(sizeof(detail::state));
  utility::construct<detail::state>(m_impl);
}

backend_gl4::~backend_gl4() {
}

void backend_gl4::process(rx_byte* _command) {
  auto state{reinterpret_cast<detail::state*>(m_impl)};
  auto header{reinterpret_cast<command_header*>(_command)};
  switch (header->type) {
  case command_type::k_resource_allocate:
    {
      const auto resource{reinterpret_cast<const resource_command*>(header + 1)};
      switch (resource->type) {
      case resource_command::category::k_buffer:
        utility::construct<detail::buffer>(resource->as_buffer + 1);
        break;
      case resource_command::category::k_target:
        {
          const auto render_target{resource->as_target};
          if (!render_target->is_swapchain()) {
            utility::construct<detail::target>(resource->as_target + 1);
          }
        }
        break;
      case resource_command::category::k_program:
        utility::construct<detail::program>(resource->as_program + 1);
        break;
      case resource_command::category::k_texture1D:
        utility::construct<detail::texture1D>(resource->as_texture1D + 1);
        break;
      case resource_command::category::k_texture2D:
        utility::construct<detail::texture2D>(resource->as_texture2D + 1);
        break;
      case resource_command::category::k_texture3D:
        utility::construct<detail::texture3D>(resource->as_texture3D + 1);
        break;
      case resource_command::category::k_textureCM:
        utility::construct<detail::textureCM>(resource->as_textureCM + 1);
        break;
      }
    }
    break;
  case command_type::k_resource_destroy:
    {
      const auto resource{reinterpret_cast<const resource_command*>(header + 1)};
      switch (resource->type) {
      case resource_command::category::k_buffer:
        utility::destruct<detail::buffer>(resource->as_buffer + 1);
        break;
      case resource_command::category::k_target:
        {
          const auto render_target{resource->as_target};
          if (!render_target->is_swapchain()) {
            utility::destruct<detail::target>(render_target + 1);
          }
        } 
        break;
      case resource_command::category::k_program:
        utility::destruct<detail::program>(resource->as_program + 1);
        break;
      case resource_command::category::k_texture1D:
        utility::destruct<detail::texture1D>(resource->as_texture1D + 1);
        break;
      case resource_command::category::k_texture2D:
        utility::destruct<detail::texture2D>(resource->as_texture2D + 1);
        break;
      case resource_command::category::k_texture3D:
        utility::destruct<detail::texture3D>(resource->as_texture3D + 1);
        break;
      case resource_command::category::k_textureCM:
        utility::destruct<detail::textureCM>(resource->as_textureCM + 1);
        break;
      }
    }
    break;
  case command_type::k_resource_construct:
    {
      const auto resource{reinterpret_cast<const resource_command*>(header + 1)};
      switch (resource->type) {
      case resource_command::category::k_buffer:
        {
          const auto render_buffer{resource->as_buffer};
          const auto buffer{reinterpret_cast<const detail::buffer*>(render_buffer + 1)};
          const auto& vertices{render_buffer->vertices()};
          const auto& elements{render_buffer->elements()};

          if (vertices.size()) {
            pglNamedBufferData(buffer->bo[0], vertices.size(), vertices.data(), GL_STATIC_DRAW);
          }

          if (elements.size()) {
            pglNamedBufferData(buffer->bo[1], elements.size(), elements.data(), GL_STATIC_DRAW);
          }

          pglVertexArrayVertexBuffer(buffer->va, 0, buffer->bo[0], 0,
            static_cast<GLsizei>(render_buffer->stride()));
          pglVertexArrayElementBuffer(buffer->va, buffer->bo[1]);

          const auto& attributes{render_buffer->attributes()};
          for (rx_size i{0}; i < attributes.size(); i++) {
            const auto& attribute{attributes[i]};
            const auto index{static_cast<GLuint>(i)};
            pglEnableVertexArrayAttrib(buffer->va, index);
            switch (attribute.type) {
            case buffer::attribute::category::k_f32:
              pglVertexArrayAttribFormat(
                buffer->va,
                index,
                static_cast<GLsizei>(attribute.count),
                GL_FLOAT,
                GL_FALSE,
                static_cast<GLsizei>(attribute.offset));
              break;
            case buffer::attribute::category::k_u8:
              pglVertexArrayAttribFormat(
                buffer->va,
                index,
                static_cast<GLsizei>(attribute.count),
                GL_UNSIGNED_BYTE,
                GL_FALSE,
                static_cast<GLsizei>(attribute.offset));
              break;
            }
            pglVertexArrayAttribBinding(buffer->va, index, 0);
          }
        }
        break;
      case resource_command::category::k_target:
        {
          const auto render_target{resource->as_target};
          if (render_target->is_swapchain()) {
            // swap chain targets don't have an user-defined attachments
            break;
          }

          const auto target{reinterpret_cast<const detail::target*>(render_target + 1)};
          const auto depth_stencil{render_target->depth_stencil()};
          if (depth_stencil) {
            // combined depth stencil format
            const auto texture{reinterpret_cast<const detail::texture2D*>(depth_stencil + 1)};
            pglNamedFramebufferTexture(target->fbo, GL_DEPTH_STENCIL_ATTACHMENT, texture->tex, 0);
          } else {
            const auto depth{render_target->depth()};
            const auto stencil{render_target->stencil()};
            if (depth) {
              const auto texture{reinterpret_cast<const detail::texture2D*>(depth + 1)};
              pglNamedFramebufferTexture(target->fbo, GL_DEPTH_ATTACHMENT, texture->tex, 0);
            }
            if (stencil) {
              const auto texture{reinterpret_cast<const detail::texture2D*>(stencil + 1)};
              pglNamedFramebufferTexture(target->fbo, GL_STENCIL_ATTACHMENT, texture->tex, 0);
            }
          }
          // color attachments
          const auto& attachments{render_target->attachments()};
          for (rx_size i{0}; i < attachments.size(); i++) {
            const auto attachment{static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i)};
            const auto render_texture{attachments[i]};
            const auto texture{reinterpret_cast<detail::texture2D*>(render_texture + 1)};
            pglNamedFramebufferTexture(target->fbo, attachment, texture->tex, 0);
          }
        }
        break;
      case resource_command::category::k_program:
        {
          const auto render_program{resource->as_program};
          const auto program{reinterpret_cast<detail::program*>(render_program + 1)};

          // TODO(dweiler): preprocessor for GLSL
          const auto& name{render_program->info().name};
          const auto vert{compile_shader(GL_VERTEX_SHADER, {"%s_vert.glsl", name})};
          const auto frag{compile_shader(GL_FRAGMENT_SHADER, {"%s_frag.glsl", name})};

          if (vert && frag) {
            pglAttachShader(program->handle, vert);
            pglAttachShader(program->handle, frag);

            render_program->info().data.each_fwd([](const string& _data) {
              // TODO
            });

            render_program->info().layout.each_fwd([](const string& _layout) {
              // TODO
            });

            pglLinkProgram(program->handle);

            // TODO

            pglDetachShader(program->handle, vert);
            pglDetachShader(program->handle, frag);
          }

          // fetch uniform locations
          render_program->uniforms().each_fwd([program](const uniform& _uniform) {
            program->uniforms.push_back(pglGetUniformLocation(program->handle, _uniform.name().data()));
          });

          pglDeleteShader(vert);
          pglDeleteShader(frag);
        }
        break;
      case resource_command::category::k_texture1D:
        {
          const auto render_texture{resource->as_texture1D};
          const auto texture{reinterpret_cast<const detail::texture1D*>(render_texture + 1)};
          const auto wrap{render_texture->wrap()};
          const auto wrap_s{convert_texture_wrap(wrap.s)};
          const auto dimensions{render_texture->dimensions()};
          const auto format{render_texture->format()};
          const auto filter{render_texture->filter()};
          const auto& data{render_texture->data()};

          GLint levels{1};
          if (filter.mip_maps) {
            levels = math::log2(rx_u32(1 + dimensions));
          }

          pglTextureParameteri(texture->tex, GL_TEXTURE_MIN_FILTER, convert_texture_filter(filter).min);
          pglTextureParameteri(texture->tex, GL_TEXTURE_MAG_FILTER, convert_texture_filter(filter).mag);
          pglTextureParameteri(texture->tex, GL_TEXTURE_WRAP_S, wrap_s);
          pglTextureParameteri(texture->tex, GL_TEXTURE_BASE_LEVEL, 0);
          pglTextureParameteri(texture->tex, GL_TEXTURE_MAX_LEVEL, levels);
          pglTextureStorage1D(
            texture->tex,
            levels,
            convert_texture_data_format(format),
            dimensions);

          if (data.size()) {
            pglTextureSubImage1D(
              texture->tex,
              0,
              0,
              dimensions,
              convert_texture_format(format),
              GL_UNSIGNED_BYTE,
              data.data());

            if (filter.mip_maps) {
              pglGenerateTextureMipmap(texture->tex);
            }
          }
        }
        break;
      case resource_command::category::k_texture2D:
        {
          const auto render_texture{resource->as_texture2D};
          const auto texture{reinterpret_cast<const detail::texture2D*>(render_texture + 1)};
          const auto wrap{render_texture->wrap()};
          const auto wrap_s{convert_texture_wrap(wrap.s)};
          const auto wrap_t{convert_texture_wrap(wrap.t)};
          const auto dimensions{render_texture->dimensions()};
          const auto format{render_texture->format()};
          const auto filter{render_texture->filter()};
          const auto& data{render_texture->data()};

          GLint levels{1};
          if (filter.mip_maps) {
            levels = math::log2(rx_u32(1 + max(dimensions.w, dimensions.h)));
          }

          pglTextureParameteri(texture->tex, GL_TEXTURE_MIN_FILTER, convert_texture_filter(filter).min);
          pglTextureParameteri(texture->tex, GL_TEXTURE_MAG_FILTER, convert_texture_filter(filter).mag);
          pglTextureParameteri(texture->tex, GL_TEXTURE_WRAP_S, wrap_s);
          pglTextureParameteri(texture->tex, GL_TEXTURE_WRAP_T, wrap_t);
          pglTextureParameteri(texture->tex, GL_TEXTURE_BASE_LEVEL, 0);
          pglTextureParameteri(texture->tex, GL_TEXTURE_MAX_LEVEL, levels);
          pglTextureStorage2D(
            texture->tex,
            levels,
            convert_texture_data_format(format),
            dimensions.w,
            dimensions.h);

          if (data.size()) {
            pglTextureSubImage2D(
              texture->tex,
              0,
              0,
              0,
              dimensions.w,
              dimensions.h,
              convert_texture_format(format),
              GL_UNSIGNED_BYTE,
              data.data());

            if (filter.mip_maps) {
              pglGenerateTextureMipmap(texture->tex);
            }
          }
        }
        break;
      case resource_command::category::k_texture3D:
        {
          const auto render_texture{resource->as_texture3D};
          const auto texture{reinterpret_cast<const detail::texture3D*>(render_texture + 1)};
          const auto wrap{render_texture->wrap()};
          const auto wrap_s{convert_texture_wrap(wrap.s)};
          const auto wrap_t{convert_texture_wrap(wrap.t)};
          const auto wrap_r{convert_texture_wrap(wrap.r)};
          const auto dimensions{render_texture->dimensions()};
          const auto format{render_texture->format()};
          const auto filter{render_texture->filter()};
          const auto& data{render_texture->data()};

          GLint levels{1};
          if (filter.mip_maps) {
            levels = math::log2(rx_u32(1 + max(dimensions.w, dimensions.h, dimensions.d)));
          }

          pglTextureParameteri(texture->tex, GL_TEXTURE_MIN_FILTER, convert_texture_filter(filter).min);
          pglTextureParameteri(texture->tex, GL_TEXTURE_MAG_FILTER, convert_texture_filter(filter).mag);
          pglTextureParameteri(texture->tex, GL_TEXTURE_WRAP_S, wrap_s);
          pglTextureParameteri(texture->tex, GL_TEXTURE_WRAP_T, wrap_t);
          pglTextureParameteri(texture->tex, GL_TEXTURE_WRAP_R, wrap_r);
          pglTextureParameteri(texture->tex, GL_TEXTURE_BASE_LEVEL, 0);
          pglTextureParameteri(texture->tex, GL_TEXTURE_MAX_LEVEL, levels);
          pglTextureStorage3D(
            texture->tex,
            levels,
            convert_texture_data_format(format),
            dimensions.w,
            dimensions.h,
            dimensions.d);

          if (data.size()) {
            pglTextureSubImage3D(
              texture->tex,
              0,
              0,
              0,
              0,
              dimensions.w,
              dimensions.h,
              dimensions.d,
              convert_texture_format(format),
              GL_UNSIGNED_BYTE,
              data.data());

            if (filter.mip_maps) {
              pglGenerateTextureMipmap(texture->tex);
            }
          }
        }
        break;
      case resource_command::category::k_textureCM:
        break;
      }
    }
    break;
  case command_type::k_clear:
    {
      const auto command{reinterpret_cast<clear_command*>(header + 1)};
      const auto render_target{command->render_target};
      const auto this_target{reinterpret_cast<detail::target*>(render_target + 1)};
      const bool clear_depth{!!(command->clear_mask & RX_RENDER_CLEAR_DEPTH)};
      const bool clear_stencil{!!(command->clear_mask & RX_RENDER_CLEAR_STENCIL)};  
      const auto clear_color{command->clear_mask & ~(RX_RENDER_CLEAR_DEPTH | RX_RENDER_CLEAR_STENCIL)};

      // ensure depth writes are enabled when clearing depth
      if (state->depth.write() && clear_depth) {
        pglDepthMask(GL_TRUE);
        state->depth.record_write(true);
        state->depth.flush();
      }

      // ensure stencil writes are enabled when clearing stencil
      if (state->stencil.write_mask() != 0xff && clear_stencil) {
        pglStencilMask(0xff);
        state->stencil.record_write_mask(0xff);
        state->stencil.flush();
      }

      // ensure scissor is disabled when clearing
      if (state->scissor.enabled()) {
        pglDisable(GL_SCISSOR_TEST);
        state->scissor.record_enable(false);
        state->scissor.flush();
      }

      // ensure color mask is appropriate for clear
      if (state->blend.write_mask() != render::blend_state::k_mask_all && clear_color) {
        pglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        state->blend.record_write_mask(0xff);
        state->blend.flush();
      }

      // treat swapchain specially since it's special in GL
      const GLuint fbo{render_target->is_swapchain() ? state->m_swap_chain_fbo : this_target->fbo};

      if (clear_color) {
        for (rx_u32 i{0}; i < 32; i++) {
          if (clear_color & (1 << i)) {
            // NOTE: -2 because depth stencil attachments behave as index 0 and 1 respectively
            pglClearNamedFramebufferfv(fbo, GL_COLOR, static_cast<GLint>(i - 2),
              &command->clear_color[0]);
            break;
          }
        }
      }
      if (clear_depth && clear_stencil) {
        pglClearNamedFramebufferfi(fbo, GL_DEPTH_STENCIL,
          0, command->clear_color.r, static_cast<GLint>(command->clear_color.g));
      } else if (clear_depth) {
        pglClearNamedFramebufferfv(fbo, GL_DEPTH, 0, &command->clear_color.r);
      } else if (clear_stencil) {
        pglClearNamedFramebufferfv(fbo, GL_STENCIL, 0, &command->clear_color.g);
      }
    }
    break;
  case command_type::k_draw:
    [[fallthrough]];
  case command_type::k_draw_elements:
    {
      const auto command{reinterpret_cast<draw_command*>(header + 1)};
      const auto render_target{command->render_target};
      const auto render_buffer{command->render_buffer};
      const auto render_state{reinterpret_cast<render::state*>(command)};

      state->use_target(render_target);
      state->use_buffer(render_buffer);
      state->use_state(render_state);

      if (header->type == command_type::k_draw_elements) {
        switch (render_buffer->element_type()) {
        case render::buffer::element_category::k_none:
          // [[unreachable]]
          break;
        case render::buffer::element_category::k_u8:
          pglDrawElements(
            convert_primitive_type(command->type),
            command->count,
            GL_UNSIGNED_BYTE,
            reinterpret_cast<void*>(sizeof(GLubyte) * command->offset));
          break;
        case render::buffer::element_category::k_u16:
          pglDrawElements(
            convert_primitive_type(command->type),
            command->count,
            GL_UNSIGNED_SHORT,
            reinterpret_cast<void*>(sizeof(GLushort) * command->offset));
          break;
        case render::buffer::element_category::k_u32:
          pglDrawElements(
            convert_primitive_type(command->type),
            command->count,
            GL_UNSIGNED_INT,
            reinterpret_cast<void*>(sizeof(GLuint) * command->offset));
          break;
        }
      } else {
        pglDrawArrays(
          convert_primitive_type(command->type),
          command->offset,
          command->count);
      }
    }
    break;
  }
}

void backend_gl4::swap() {
  SDL_GL_SwapWindow(SDL_GL_GetCurrentWindow());
}


} // namespace rx::render