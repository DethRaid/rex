#include "rx/core/memory/system_allocator.h"

namespace rx::memory {

rx_byte* system_allocator::allocate(rx_size _size) {
  return m_stats_allocator.allocate(_size);
}

rx_byte* system_allocator::reallocate(rx_byte* _data, rx_size _size) {
  return m_stats_allocator.reallocate(_data, _size);
}

void system_allocator::deallocate(rx_byte* _data) {
  return m_stats_allocator.deallocate(_data);
}

global<system_allocator> system_allocator::s_instance{"system", "allocator"};

} // namespace rx::memory
