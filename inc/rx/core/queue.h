#ifndef RX_CORE_QUEUE_H
#define RX_CORE_QUEUE_H

#include <rx/core/utility/move.h>
#include <rx/core/utility/forward.h>

#include <rx/core/memory/system_allocator.h>

namespace rx {

template<typename T>
struct queue {
  constexpr queue(memory::allocator* _allocator);
  constexpr queue();

  ~queue();
  bool is_empty() const;

  void push(const T& value);

  template<typename... Ts>
  void emplace(Ts&&... _arguments);

  T pop();
  void clear();

private:
  struct node {
    constexpr node(const T& value) : m_value{value} {}
    constexpr node(T&& value) : m_value{utility::move(value)} {}
    T m_value;
    node* m_next;
  };

  memory::allocator* m_allocator;
  node* m_first;
  node* m_last;
};

template<typename T>
inline constexpr queue<T>::queue(memory::allocator* _allocator)
  : m_allocator{_allocator}
{
}

template<typename T>
inline constexpr queue<T>::queue()
  : m_allocator{&memory::g_system_allocator}
{
}

template<typename T>
inline queue<T>::~queue() {
  clear();
}

template<typename T>
inline bool queue<T>::is_empty() const {
  return !m_first;
}

template<typename T>
inline void queue<T>::push(const T& _value) {
  auto new_node{reinterpret_cast<node*>(m_allocator->allocate(sizeof(node)))};
  utility::construct<T>(new_node, _value);
  if (is_empty()) {
    m_first = new_node;
    m_last = m_first;
  } else {
    auto last_node{m_last};
    last_node->m_next = new_node;
    m_last = last_node->m_next;
  }
}

template<typename T>
template<typename... Ts>
inline void queue<T>::emplace(Ts&&... _arguments) {
  auto new_node{reinterpret_cast<node*>(m_allocator->allocate(sizeof(node)))};
  utility::construct(new_node, utility::forward<Ts>(_arguments)...);
  if (is_empty()) {
    m_first = new_node;
    m_last = m_first;
  } else {
    auto last_node{m_last};
    last_node->m_next = new_node;
    m_last = last_node->m_next;
  }
}

template<typename T>
inline T queue<T>::pop() {
  RX_ASSERT(!is_empty(), "empty queue");

  auto this_node{m_first};

  T value{utility::move(this_node->m_value)};
  m_first = utility::move(this_node->m_next);

  utility::destruct<T>(this_node);
  m_allocator->deallocate(reinterpret_cast<rx_byte*>(this_node));

  return value;
}

template<typename T>
inline void queue<T>::clear() {
  while (!is_empty()) {
    pop();
  }
}

} // namespace rx

#endif // RX_CORE_QUEUE_H