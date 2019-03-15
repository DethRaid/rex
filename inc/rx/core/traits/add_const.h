#ifndef RX_CORE_TRAITS_ADD_CONST_H
#define RX_CORE_TRAITS_ADD_CONST_H

#include <rx/core/traits/type_identity.h>

namespace rx::traits {
  
template<typename T>
using add_const = const T;

} // namespace rx::traits

#endif // RX_CORE_TRAITS_ADD_CONST_H
