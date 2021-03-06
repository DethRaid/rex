#ifndef RX_RENDER_FRONTEND_BUFFER_H
#define RX_RENDER_FRONTEND_BUFFER_H
#include "rx/core/vector.h"
#include "rx/core/hash.h"

#include "rx/render/frontend/resource.h"

namespace Rx::Render::Frontend {

struct Context;

struct Buffer : Resource {
  struct Attribute {
    enum class Type : Uint8 {
      // Scalars
      k_f32,    // 1 x Float32

      // Vectors
      k_vec2f,  // 2 x Float32
      k_vec3f,  // 3 x Float32
      k_vec4f,  // 4 x Float32
      k_vec4b,  // 4 x Byte

      // Matrices
      k_mat4x4f // 4x4 x Float32
    };

    bool operator!=(const Attribute& _other) const;
    Size hash() const;

    Type type;
    Size offset;
  };

  enum class ElementType : Uint32 {
    k_none,
    k_u8,
    k_u16,
    k_u32
  };

  enum class Type : Uint32 {
    k_static,
    k_dynamic
  };

  // Type that describes the format.
  struct Format {
    Format();

    void record_type(Type _type);

    void record_element_type(ElementType _type);

    void record_vertex_stride(Size _stride);
    void record_instance_stride(Size _stride);

    void record_vertex_attribute(const Attribute& _attribute);
    void record_instance_attribute(const Attribute& _attribute);

    bool is_indexed() const;
    bool is_instanced() const;

    Type type() const;
    ElementType element_type() const;

    const Vector<Attribute>& vertex_attributes() const &;
    const Vector<Attribute>& instance_attributes() const &;

    Size vertex_stride() const;
    Size instance_stride() const;
    Size element_size() const;

    bool operator!=(const Format& _format) const;
    bool operator==(const Format& _format) const;

    void finalize();
    Size hash() const;

  private:
    enum {
      TYPE            = 1 << 0,
      ELEMENT_TYPE    = 1 << 1,
      VERTEX_STRIDE   = 1 << 2,
      INSTANCE_STRIDE = 1 << 3,
      FINALIZED       = 1 << 4
    };

    Uint32 m_flags;
    Type m_type;
    ElementType m_element_type;
    Size m_vertex_stride;
    Size m_instance_stride;
    Vector<Attribute> m_vertex_attributes;
    Vector<Attribute> m_instance_attributes;
    Size m_hash;
  };

  struct Edit {
    Size sink;
    Size offset;
    Size size;
  };

  Buffer(Context* _frontend);
  ~Buffer();

  // Write |_size| bytes from |_data| into vertex store.
  template<typename T>
  void write_vertices(const T* _data, Size _size);

  // Write |_size| bytes from |_data| into element store.
  template<typename T>
  void write_elements(const T* _data, Size _size);

  // Write |_size| bytes from |_data| into instance store.
  template<typename T>
  void write_instances(const T* _data, Size _size);

  // Map |_size| bytes of vertices.
  Byte* map_vertices(Size _size);

  // Map |_size| bytes of elements.
  Byte* map_elements(Size _size);

  // Map |_size| bytes of instances.
  Byte* map_instances(Size _size);

  // Record buffer format.
  void record_format(const Format& _format);

  // Records an edit to the buffer at byte |_offset| of size |_size|.
  void record_vertices_edit(Size _offset, Size _size);
  void record_elements_edit(Size _offset, Size _size);
  void record_instances_edit(Size _offset, Size _size);

  const Vector<Byte>& vertices() const &;
  const Vector<Byte>& elements() const &;
  const Vector<Byte>& instances() const &;

  Size size() const;

  const Vector<Edit>& edits() const;
  Size bytes_for_edits() const;
  void optimize_edits();
  void clear_edits();

  void validate() const;

  const Format& format() const &;

private:
  friend struct Arena;

  void write_vertices_data(const Byte* _data, Size _size);
  void write_elements_data(const Byte* _data, Size _size);
  void write_instances_data(const Byte* _data, Size _size);

  enum {
    FORMAT = 1 << 1
  };

  Vector<Byte> m_vertices_store;
  Vector<Byte> m_elements_store;
  Vector<Byte> m_instances_store;

  Format m_format;
  Vector<Edit> m_edits;
  Uint32 m_recorded;
};


// [Buffer::Attribute]
inline bool Buffer::Attribute::operator!=(const Attribute& _other) const {
  return _other.offset != offset || _other.type != type;
}

inline Size Buffer::Attribute::hash() const {
  return hash_combine(Hash<Size>{}(offset), Hash<Type>{}(type));
}

// [Buffer::Format]
inline Buffer::Format::Format()
  : m_flags{0}
  , m_element_type{ElementType::k_none}
  , m_vertex_stride{0}
  , m_instance_stride{0}
  , m_hash{0}
{
}

inline void Buffer::Format::record_type(Type _type) {
  RX_ASSERT(!(m_flags & FINALIZED), "finalized");
  m_type = _type;
  m_flags |= TYPE;
}

inline void Buffer::Format::record_element_type(ElementType _type) {
  RX_ASSERT(!(m_flags & FINALIZED), "finalized");
  m_element_type = _type;
  m_flags |= ELEMENT_TYPE;
}

inline void Buffer::Format::record_vertex_stride(Size _stride) {
  RX_ASSERT(!(m_flags & FINALIZED), "finalized");
  m_vertex_stride = _stride;
  m_flags |= VERTEX_STRIDE;
}

inline void Buffer::Format::record_instance_stride(Size _stride) {
  RX_ASSERT(!(m_flags & FINALIZED), "finalized");
  m_instance_stride = _stride;
  m_flags |= INSTANCE_STRIDE;
}

inline void Buffer::Format::record_vertex_attribute(const Attribute& _attribute) {
  RX_ASSERT(!(m_flags & FINALIZED), "finalized");
  m_vertex_attributes.push_back(_attribute);
}

inline void Buffer::Format::record_instance_attribute(const Attribute& _attribute) {
  RX_ASSERT(!(m_flags & FINALIZED), "finalized");
  m_instance_attributes.push_back(_attribute);
}

inline bool Buffer::Format::is_indexed() const {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return m_element_type != ElementType::k_none;
}

inline bool Buffer::Format::is_instanced() const {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return !m_instance_attributes.is_empty();
}

inline Buffer::Type Buffer::Format::type() const {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return m_type;
}

inline Buffer::ElementType Buffer::Format::element_type() const {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return m_element_type;
}

inline const Vector<Buffer::Attribute>& Buffer::Format::vertex_attributes() const & {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return m_vertex_attributes;
}

inline const Vector<Buffer::Attribute>& Buffer::Format::instance_attributes() const & {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return m_instance_attributes;
}

inline Size Buffer::Format::vertex_stride() const {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return m_vertex_stride;
}

inline Size Buffer::Format::instance_stride() const {
  RX_ASSERT(m_flags & FINALIZED, "not finalized");
  return m_instance_stride;
}

inline Size Buffer::Format::element_size() const {
  switch (m_element_type) {
  case ElementType::k_none:
    return 0;
  case ElementType::k_u8:
    return 1;
  case ElementType::k_u16:
    return 2;
  case ElementType::k_u32:
    return 4;
  }
  RX_HINT_UNREACHABLE();
}

inline bool Buffer::Format::operator==(const Format& _format) const {
  return !operator!=(_format);
}

inline Size Buffer::Format::hash() const {
  return m_hash;
}

// [Buffer]
template<typename T>
inline void Buffer::write_vertices(const T* _data, Size _size) {
  write_vertices_data(reinterpret_cast<const Byte*>(_data), _size);
}

template<typename T>
inline void Buffer::write_elements(const T* _data, Size _size) {
  static_assert((traits::is_same<T, Uint8> || traits::is_same<T, Uint16> || traits::is_same<T, Uint32>),
    "unsupported element type T");

  RX_ASSERT(_size % sizeof(T) == 0, "_size isn't a multiple of T");

  if constexpr (traits::is_same<T, Byte>) {
    write_elements_data(reinterpret_cast<const Byte*>(_data), _size);
  } else if constexpr (traits::is_same<T, Uint16>) {
    write_elements_data(reinterpret_cast<const Byte*>(_data), _size);
  } else if constexpr (traits::is_same<T, Uint32>) {
    write_elements_data(reinterpret_cast<const Byte*>(_data), _size);
  }
}

inline void Buffer::record_format(const Format& _format) {
  RX_ASSERT(!(m_recorded & FORMAT), "already recorded");
  m_format = _format;
  m_recorded |= FORMAT;
}

inline void Buffer::record_vertices_edit(Size _offset, Size _size) {
  m_edits.emplace_back(1_z, _offset, _size);
}

inline void Buffer::record_elements_edit(Size _offset, Size _size) {
  RX_ASSERT(m_format.element_type() != ElementType::k_none,
    "cannot record edit to elements");
  m_edits.emplace_back(0_z, _offset, _size);
}

inline void Buffer::record_instances_edit(Size _offset, Size _size) {
  RX_ASSERT(m_format.is_instanced(), "cannot record edit to instances");
  m_edits.emplace_back(2_z, _offset, _size);
}

inline const Vector<Byte>& Buffer::vertices() const & {
  return m_vertices_store;
}

inline const Vector<Byte>& Buffer::elements() const & {
  return m_elements_store;
}

inline const Vector<Byte>& Buffer::instances() const & {
  return m_instances_store;
}

inline Size Buffer::size() const {
  return m_vertices_store.size() + m_elements_store.size() + m_instances_store.size();
}

inline const Vector<Buffer::Edit>& Buffer::edits() const {
  return m_edits;
}

inline void Buffer::clear_edits() {
  m_edits.clear();
}

inline const Buffer::Format& Buffer::format() const & {
  return m_format;
}

} // namespace rx::render::frontend

#endif // RX_RENDER_FRONTEND_BUFFER_H
