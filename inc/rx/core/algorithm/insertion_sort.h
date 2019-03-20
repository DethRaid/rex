#ifndef RX_CORE_ALGORITHM_INSERTION_SORT_H
#define RX_CORE_ALGORITHM_INSERTION_SORT_H

#include <rx/core/utility/move.h>

namespace rx::algorithm {

template<typename T, typename F>
inline void insertion_sort(T* _start, T* _end, F&& _compare) {
  for (T* item1 = _start + 1; item1 < _end; item1++) {
    if (_compare(*item1, *(item1 - 1))) {
      T temp{utility::move(*item1)};
      *item1 = utility::move(*(item1 - 1));
      T* item2 = item1 - 1;
      for (; item2 > start && _compare(temp, *(item2 - 1)); --item2) {
        *item2 = utility::move(*(item2 - 1));
      }
      *item2 = utility::move(temp);
    }
  }
}

} // namespace rx::algorithm

#endif // RX_CORE_ALGORITHM_INSERTION_SORT_H