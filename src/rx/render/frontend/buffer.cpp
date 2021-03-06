#include <string.h> // memcpy

#include "rx/render/frontend/buffer.h"
#include "rx/render/frontend/context.h"

namespace Rx::Render::Frontend {

// [Buffer::Format]
bool Buffer::Format::operator!=(const Format& _format) const {
  if (_format.m_hash != m_hash) {
    return true;
  }

  if (_format.m_flags != m_flags) {
    return true;
  }

  if (_format.m_element_type != m_element_type) {
    return true;
  }

  if (_format.m_vertex_stride != m_vertex_stride) {
    return true;
  }

  if (_format.m_instance_stride != m_instance_stride) {
    return true;
  }

  if (_format.m_vertex_attributes.size() != m_vertex_attributes.size()) {
    return true;
  }

  if (_format.m_instance_attributes.size() != m_instance_attributes.size()) {
    return true;
  }

  auto compare_attributes = [](const Vector<Attribute>& _lhs, const Vector<Attribute>& _rhs) {
    // |_lhs| and |_rhs| will have the same size when this is called.
    const auto size = _lhs.size();
    for (Size i = 0; i < size; i++) {
      if (_lhs[i] != _rhs[i]) {
        return false;
      }
    }
    return true;
  };

  if (!compare_attributes(_format.m_vertex_attributes, m_vertex_attributes)) {
    return true;
  }

  if (!compare_attributes(_format.m_instance_attributes, m_instance_attributes)) {
    return true;
  }

  return false;
}

void Buffer::Format::finalize() {
  m_flags |= FINALIZED;

  RX_ASSERT(!!(m_flags & TYPE), "type not recorded");
  RX_ASSERT(!!(m_flags & ELEMENT_TYPE), "element type not recorded");
  RX_ASSERT(!!(m_flags & VERTEX_STRIDE), "vertex stride not recorded");

  if (is_instanced()) {
    RX_ASSERT(!!(m_flags & INSTANCE_STRIDE), "instance stride not recorded");
  }

  // Calculate the final hash value.
  m_hash = Hash<Uint32>{}(m_flags);
  m_hash = hash_combine(m_hash, Hash<ElementType>{}(m_element_type));
  m_hash = hash_combine(m_hash, m_vertex_stride);
  m_hash = hash_combine(m_hash, m_instance_stride);
  m_vertex_attributes.each_fwd([&](const Attribute& _attribute) {
    m_hash = hash_combine(m_hash, _attribute.hash());
  });

  m_instance_attributes.each_fwd([&](const Attribute& _attribute) {
    m_hash = hash_combine(m_hash, _attribute.hash());
  });
}

// [Buffer]
Buffer::Buffer(Context* _frontend)
  : Resource{_frontend, Resource::Type::k_buffer}
  , m_vertices_store{m_frontend->allocator()}
  , m_elements_store{m_frontend->allocator()}
  , m_instances_store{m_frontend->allocator()}
  , m_edits{m_frontend->allocator()}
  , m_recorded{0}
{
}

Buffer::~Buffer() {
}

void Buffer::write_vertices_data(const Byte* _data, Size _size) {
  RX_ASSERT(_data, "null data");
  memcpy(map_vertices(_size), _data, _size);
}

void Buffer::write_elements_data(const Byte* _data, Size _size) {
  RX_ASSERT(_data, "null data");
  memcpy(map_elements(_size), _data, _size);
}

void Buffer::write_instances_data(const Byte* _data, Size _size) {
  RX_ASSERT(_data, "null data");
  memcpy(map_instances(_size), _data, _size);
}

Byte* Buffer::map_vertices(Size _size) {
  // RX_ASSERT(m_recorded & k_vertex_stride, "vertex stride not recorded");
  RX_ASSERT(_size != 0, "_size is zero");
  RX_ASSERT(_size % m_format.vertex_stride() == 0,
    "_size not a multiple of vertex stride");

  m_vertices_store.resize(_size, Utility::UninitializedTag{});
  update_resource_usage(size());

  return m_vertices_store.data();
}

Byte* Buffer::map_elements(Size _size) {
  RX_ASSERT(m_format.is_indexed(), "not an indexed format");
  RX_ASSERT(_size != 0, "_size is zero");
  RX_ASSERT(_size % m_format.element_size() == 0, "_size is not a multiple of element size");

  m_elements_store.resize(_size, Utility::UninitializedTag{});
  update_resource_usage(size());

  return m_elements_store.data();
}

Byte* Buffer::map_instances(Size _size) {
  RX_ASSERT(m_format.is_instanced(), "not an instanced format");
  RX_ASSERT(_size != 0, "_size is zero");
  RX_ASSERT(_size % m_format.instance_stride() == 0, "_size not a multiple of instance stride");

  m_instances_store.resize(_size, Utility::UninitializedTag{});
  update_resource_usage(size());

  return m_instances_store.data();
}

void Buffer::validate() const {
  RX_ASSERT(m_recorded & FORMAT, "format not recorded");
}

Size Buffer::bytes_for_edits() const {
  Size bytes = 0;
  m_edits.each_fwd([&](const Edit& _edit) { bytes += _edit.size; });
  return bytes;
}

void Buffer::optimize_edits() {
  // Determines if |_lhs| is fully inside |_rhs|.
  auto inside = [](const Edit& _lhs, const Edit& _rhs) {
    return !(_lhs.offset < _rhs.offset || _lhs.offset + _lhs.size > _rhs.offset + _rhs.size);
  };

  // When an edit is inside a larger edit, that larger edit will include the
  // nested edit. Remove such edits (duplicate and fully overlapping) to
  // produce a minimal and coalesced edit list for the backend.

  // WARN(dweiler): This behaves O(n^2), except n should be small.
  for (Size i = 0; i < m_edits.size(); i++) {
    for (Size j = 0; j < m_edits.size(); j++) {
      if (i == j) {
        continue;
      }

      // The edits need to be to the same sink.
      const auto& e1 = m_edits[i];
      const auto& e2 = m_edits[j];
      if (e1.sink != e2.sink) {
        continue;
      }

      // Edit exists fully inside another, remove it.
      if (inside(e1, e2)) {
        m_edits.erase(i, i + 1);
      }
    }
  }
}

} // namespace rx::render::frontend
