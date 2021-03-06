#ifndef RX_TEXTURE_DXT_H
#define RX_TEXTURE_DXT_H
#include "rx/core/vector.h"

namespace Rx::Texture {

enum class DXTType {
  k_dxt1,
  k_dxt5
};

template<DXTType T>
Vector<Byte> dxt_compress(Memory::Allocator& _allocator,
                             const Byte *const _uncompressed, Size _width, Size _height,
                             Size _channels, Size& out_size_, Size& optimized_blocks_);

} // namespace rx::texture

#endif // RX_TEXTURE_DXT_H
