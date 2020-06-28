#include "rx/core/log.h"

#if defined(RX_PLATFORM_POSIX)
#include <signal.h> // raise, SIGABRT
#elif defined(RX_PLATFORM_WINDOWS)
#include <intrin.h> // __debugbreak
#include <stdlib.h> // exit
#endif

namespace Rx {

RX_LOG("abort", logger);

[[noreturn, maybe_unused]]
static void abort_debug() {
#if defined(RX_PLATFORM_POSIX) && (defined(RX_COMPILER_GCC) || defined(RX_COMPILER_CLANG))
  __builtin_trap();
#elif defined(RX_PLATFORM_WINDOWS)
  __debugbreak();
#else
  static_assert(0, "implement abort_debug");
#endif
}

[[maybe_unused]]
static void abort_release() {
#if defined(RX_PLATFORM_POSIX)
  raise(SIGABRT);
#elif defined(RX_PLATFORM_WINDOWS)
  // Windows doesn't support SIGABRT. If we use standard abort when built with
  // VS's debug runtime library, we'll get the annoying:
  //
  // "This application has requested the Runtime to terminate in an unusual way."
  //
  // Try to avoid this using exit instead.
  ::exit(2);
#else
  static_assert(0, "implement abort_release");
#endif
}

[[noreturn]]
static void abort_epilogue() {
  // Forcefully flush the current log contents before we abort, so that any
  // messages that may include the reason for the abortion end up in the log.
  Log::flush();

#if defined(RX_DEBUG)
  abort_debug();
#else
  abort_release();
#endif
}

[[noreturn]]
void abort_full(const char* _message) {
  logger->error("%s", _message);
  abort_epilogue();
}

[[noreturn]]
void abort_truncated(const char* _message) {
  logger->error("%s... [truncated]", _message);
  abort_epilogue();
}

} // namespace rx
