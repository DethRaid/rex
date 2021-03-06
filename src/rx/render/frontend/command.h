#ifndef RX_RENDER_FRONTEND_COMMAND_H
#define RX_RENDER_FRONTEND_COMMAND_H

#include "rx/core/source_location.h"
#include "rx/core/memory/bump_point_allocator.h"
#include "rx/core/utility/nat.h"
#include "rx/math/vec4.h"
#include "rx/render/frontend/state.h"

namespace Rx::Render::Frontend {

struct Target;
struct Buffer;
struct Program;
struct Texture;
struct Texture1D;
struct Texture2D;
struct Texture3D;
struct TextureCM;
struct Downloader;

#define RX_RENDER_CLEAR_DEPTH (1 << 0)
#define RX_RENDER_CLEAR_STENCIL (1 << 1)
#define RX_RENDER_CLEAR_COLOR(INDEX) (1 << (2 + (INDEX)))

enum class PrimitiveType : Uint8 {
  TRIANGLES,
  TRIANGLE_STRIP,
  POINTS,
  LINES
};

enum class CommandType : Uint8 {
  RESOURCE_ALLOCATE,
  RESOURCE_CONSTRUCT,
  RESOURCE_UPDATE,
  RESOURCE_DESTROY,
  CLEAR,
  DRAW,
  BLIT,
  DOWNLOAD,
  PROFILE
};

struct alignas(Memory::Allocator::ALIGNMENT) CommandHeader {
  struct Info {
    constexpr Info(const char *_description, const SourceLocation &_source_location)
      : description{_description}
      , source_info{_source_location}
    {
    }

    const char *description;
    SourceLocation source_info;
  };

  CommandType type;
  Info tag;
};

#define RX_RENDER_TAG(_description) \
  ::Rx::Render::Frontend::CommandHeader::Info{(_description), RX_SOURCE_LOCATION}

struct CommandBuffer {
  CommandBuffer(Memory::Allocator &_allocator, Size _size);
  ~CommandBuffer();

  Byte *allocate(Size _size, CommandType _command,
    const CommandHeader::Info &_info);

  void reset();

  Size used() const;

  Size size() const;

private:
  Memory::Allocator &m_base_allocator;
  Byte *m_base_memory;
  Memory::BumpPointAllocator m_allocator;
};

struct Buffers {
  constexpr Buffers();

  static constexpr Size k_max_buffers{8};

  void add(int _buffer);

  bool operator==(const Buffers &_buffers) const;

  bool operator!=(const Buffers &_buffers) const;

  Size size() const;

  bool is_empty() const;

  int operator[](Size _index) const;

  int last() const;

  const int *data() const;

private:
  union {
    Utility::Nat m_nat;
    int m_elements[k_max_buffers];
  };
  Size m_index;
};

struct Textures {
  static constexpr const Size k_max_textures{8};

  constexpr Textures();

  int add(Texture* _texture);

  bool is_empty() const;

  Size size() const;

  void clear();

  Texture *operator[](Size _index) const;

private:
  union {
    Utility::Nat m_nat;
    Texture* m_handles[k_max_textures];
  };
  Size m_index;
};

struct DrawCommand {
  Buffers draw_buffers;
  Textures draw_textures;
  State render_state;
  Target *render_target;
  Buffer *render_buffer;
  Program *render_program;
  Size count;
  Size offset;
  Size instances;
  Size base_vertex;
  Size base_instance;
  PrimitiveType type;
  Uint64 dirty_uniforms_bitset;

  const Byte *uniforms() const;

  Byte *uniforms();
};

struct ClearCommand {
  Buffers draw_buffers;
  State render_state;
  Target *render_target;
  bool clear_depth;
  bool clear_stencil;
  Uint32 clear_colors;
  Uint8 stencil_value;
  Float32 depth_value;
  Math::Vec4f color_values[Buffers::k_max_buffers];
};

struct BlitCommand {
  State render_state;
  Target *src_target;
  Size src_attachment;
  Target *dst_target;
  Size dst_attachment;
};

struct DownloadCommand {
  Target* src_target;
  Size src_attachment;
  Math::Vec2z offset;
  Downloader* downloader;
};

struct ProfileCommand {
  const char *tag;
};

struct ResourceCommand {
  enum class Type : Uint8 {
    BUFFER,
    TARGET,
    PROGRAM,
    TEXTURE1D,
    TEXTURE2D,
    TEXTURE3D,
    TEXTURECM,
    DOWNLOADER
  };

  Type type;

  union {
    Target* as_target;
    Buffer* as_buffer;
    Program* as_program;
    Texture1D* as_texture1D;
    Texture2D* as_texture2D;
    Texture3D* as_texture3D;
    TextureCM* as_textureCM;
    Downloader* as_downloader;
  };
};

struct UpdateCommand {
  enum class Type : Uint8 {
    BUFFER,
    TEXTURE1D,
    TEXTURE2D,
    TEXTURE3D
  };

  Type type;

  union {
    Buffer *as_buffer;
    Texture1D *as_texture1D;
    Texture2D *as_texture2D;
    Texture3D *as_texture3D;
  };

  // The number of edits to the resource in this update.
  Size edits;

  // The edit stream is an additional, variably-sized stream of data included as
  // a footer on this structure. It's contents encode a variable amount of edits
  // to the given resource.
  //
  // The encoding of the edit stream is a list of Size integers. The number
  // of integers per edit is determined by the resource Type |kind|.
  //
  // Buffer edits are represented by a three-tuple of integers of the format
  // {
  //   sink:   sink to edit: 0 = elements, 1 = vertices, 2 = instances
  //   offset: byte offset
  //   size:   size in bytes
  // }
  //
  // Texture edits are represented by a variable-tuple of integers of the format
  // {
  //   level:  miplevel to edit
  //   offset: offset in pixels (1, 2, or 3 integers for 1D, 2D and 3D textures, respectively)
  //   size:   size in pixels (1, 2, or 3 integers for 1D, 2D and 3D textures, respectively)
  // }
  const Size *edit() const;

  Size *edit();
};

// command_buffer
inline Size CommandBuffer::used() const {
  return m_allocator.used();
}

inline Size CommandBuffer::size() const {
  return m_allocator.size();
}

// textures
inline constexpr Textures::Textures()
  : m_nat{}
  , m_index{0}
{
}

inline int Textures::add(Texture* _texture) {
  RX_ASSERT(m_index < k_max_textures, "too many draw textures");
  m_handles[m_index] = _texture;
  return static_cast<int>(m_index++);
}

inline bool Textures::is_empty() const {
  return m_index == 0;
}

inline Size Textures::size() const {
  return m_index;
}

inline void Textures::clear() {
  m_index = 0;
}

inline Texture *Textures::operator[](Size _index) const {
  RX_ASSERT(_index < k_max_textures, "out of bounds");
  return reinterpret_cast<Texture *>(m_handles[_index]);
}

// buffers
inline constexpr Buffers::Buffers()
  : m_nat{}
  , m_index{0}
{
}

inline void Buffers::add(int _buffer) {
  RX_ASSERT(m_index < k_max_buffers, "too many draw buffers");
  m_elements[m_index++] = _buffer;
}

inline bool Buffers::operator==(const Buffers &_buffers) const {
  // Comparing buffers is approximate and not exact. When we share
  // a common initial sequence of elements we compare equal provided
  // the sequence length isn't larger than ours.
  if (_buffers.m_index > m_index) {
    return false;
  }
  for (Size i = 0; i < m_index; i++) {
    if (_buffers.m_elements[i] != m_elements[i]) {
      return false;
    }
  }
  return true;
}

inline bool Buffers::operator!=(const Buffers &_buffers) const {
  return !operator==(_buffers);
}

inline Size Buffers::size() const {
  return m_index;
}

inline bool Buffers::is_empty() const {
  return m_index == 0;
}

inline int Buffers::operator[](Size _index) const {
  RX_ASSERT(_index < k_max_buffers, "out of bounds");
  return m_elements[_index];
}

inline int Buffers::last() const {
  return m_elements[m_index - 1];
}

inline const int* Buffers::data() const {
  return m_elements;
}

// draw_command
inline const Byte *DrawCommand::uniforms() const {
  // NOTE: standard permits aliasing with char (Byte)
  return reinterpret_cast<const Byte *>(this) + sizeof *this;
}

inline Byte *DrawCommand::uniforms() {
  // NOTE: standard permits aliasing with char (Byte)
  return reinterpret_cast<Byte *>(this) + sizeof *this;
}

// update_command
inline const Size *UpdateCommand::edit() const {
  // NOTE: standard permits aliasing with char (Byte)
  const auto *opaque = reinterpret_cast<const Byte *>(this) + sizeof *this;
  return reinterpret_cast<const Size *>(opaque);
}

inline Size *UpdateCommand::edit() {
  // NOTE: standard permits aliasing with char (Byte)
  auto *opaque = reinterpret_cast<Byte *>(this) + sizeof *this;
  return reinterpret_cast<Size *>(opaque);
}

} // namespace rx::render::frontend

#endif // RX_RENDER_FRONTEND_COMMAND_H
