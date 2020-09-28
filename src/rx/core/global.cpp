#include <string.h> // strcmp
#include <stdlib.h> // malloc, free

#include "rx/core/global.h"
#include "rx/core/log.h"

#include "rx/core/concurrency/scope_lock.h"
#include "rx/core/concurrency/spin_lock.h"

namespace Rx {

RX_LOG("global", logger);

static Concurrency::SpinLock g_lock;

static GlobalGroup g_group_system{"system"};

Byte* GlobalNode::allocate(Size _size) {
  return static_cast<Byte*>(malloc(_size));
}

void GlobalNode::init() {
  const auto flags = m_argument_store.as_tag();
  if (flags & INITIALIZED) {
    return;
  }

  m_storage_dispatch(StorageMode::INIT, data(), m_argument_store.as_ptr());
  Globals::s_initialized_list.push_back(&m_initialized);
  m_argument_store.retag(flags | INITIALIZED);
}

void GlobalNode::fini() {
  const auto flags = m_argument_store.as_tag();
  if (!(flags & INITIALIZED)) {
    return;
  }

  if (flags & ARGUMENTS) {
    m_storage_dispatch(StorageMode::FINI, data(), m_argument_store.as_ptr());
    free(m_argument_store.as_ptr());
    m_argument_store = nullptr;
  } else {
    m_storage_dispatch(StorageMode::FINI, data(), nullptr);
  }

  Globals::s_initialized_list.erase(&m_initialized);
}

// GlobalGroup
GlobalNode* GlobalGroup::find(const char* _name) {
  for (auto node = m_list.enumerate_head(&GlobalNode::m_grouped); node; node.next()) {
    if (!strcmp(node->name(), _name)) {
      return node.data();
    }
  }
  return nullptr;
}

void GlobalGroup::init() {
  for (auto node = m_list.enumerate_head(&GlobalNode::m_grouped); node; node.next()) {
    node->init();
  }
}

void GlobalGroup::fini() {
  for (auto node = m_list.enumerate_tail(&GlobalNode::m_grouped); node; node.prev()) {
    node->fini();
  }
}

// Globals
GlobalGroup* Globals::find(const char* _name) {
  for (auto group = s_group_list.enumerate_head(&GlobalGroup::m_link); group; group.next()) {
    if (!strcmp(group->name(), _name)) {
      return group.data();
    }
  }
  return nullptr;
}

void Globals::link() {
  // Link ungrouped globals from |s_node_list| managed by |GlobalNode::m_ungrouped|
  // with the appropriate group given by |GlobalGroup::m_list|, which is managed
  // by |GlobalNode::m_grouped| when the given global shares the same group
  // name as the group.
  Concurrency::ScopeLock lock{g_lock};
  for (auto node = s_node_list.enumerate_head(&GlobalNode::m_ungrouped); node; node.next()) {
    bool unlinked = true;
    for (auto group = s_group_list.enumerate_head(&GlobalGroup::m_link); group; group.next()) {
      if (!strcmp(node->m_group, group->name())) {
        group->m_list.push(&node->m_grouped);
        unlinked = false;
        break;
      }
    }

    if (unlinked) {
      // NOTE(dweiler): If you've hit this code-enforced crash it means there
      // exists an rx::Global<T> that is associated with a group by name which
      // doesn't exist. This can be caused by misnaming the group in the
      // global's constructor, or because the rx::GlobalGroup with that name
      // doesn't exist in any translation unit.
      *reinterpret_cast<volatile int*>(0) = 0;
    }
  }
}

void Globals::init() {
  for (auto group = s_group_list.enumerate_head(&GlobalGroup::m_link); group; group.next()) {
    group->init();
  }
}

void Globals::fini() {
  // Finalize all globals in the reverse order they were initialized.
  for (auto node = s_initialized_list.enumerate_tail(&GlobalNode::m_initialized); node; node.prev()) {
    node->fini();
  }
}

void Globals::link(GlobalNode* _node) {
  Concurrency::ScopeLock lock{g_lock};
  s_node_list.push(&_node->m_ungrouped);
}

void Globals::link(GlobalGroup* _group) {
  Concurrency::ScopeLock lock{g_lock};
  s_group_list.push(&_group->m_link);
}

// Register Globals::fini with atexit.
static struct AtExit {
  AtExit() {
    atexit(&Globals::fini);
  }
} g_globals_at_exit;

} // namespace rx
