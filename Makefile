# Compilation options:
# * LTO       - Link time optimization
# * ASAN      - Address sanitizer
# * TSAN      - Thread sanitizer
# * UBSAN     - Undefined behavior sanitizer
# * DEBUG     - Debug build
# * PROFILE   - Profile build
LTO ?= 0
ASAN ?= 0
TSAN ?= 0
UBSAN ?= 0
DEBUG ?= 0
PROFILE ?= 0

rwildcard = $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))

# C compiler
CC := gcc
CC ?= clang

# C++ compiler
# We use the C frontend with -xcpp to avoid linking in C++ runtime library
CXX := $(CC)

SRCS := $(call rwildcard, src/, *cpp) $(call rwildcard, src/, *c)
OBJS := $(filter %.o,$(SRCS:.cpp=.o)) $(filter %.o,$(SRCS:.c=.o))
DEPS := $(filter %.d,$(SRCS:.cpp=.d)) $(filter %.d,$(SRCS:.c=.d))

#
# Shared C and C++ compilation flags.
#
CFLAGS := -Isrc
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += `sdl2-config --cflags`

# Disable some unneeded features.
CFLAGS += -fno-asynchronous-unwind-tables

# Enable link-time optimizations if requested.
ifeq ($(LTO),1)
	CFLAGS += -flto
endif

ifeq ($(DEBUG),1)
	CFLAGS += -g
	CFLAGS += -DRX_DEBUG

	# Disable all optimizations in debug builds.
	CFLAGS += -O0

	# Ensure there's a frame pointer in debug builds.
	CFLAGS += -fno-omit-frame-pointer
else ifeq ($(PROFILE),1)
	# Enable profile options in profile builds.
	CFLAGS += -pg
	CFLAGS += -no-pie

	# Enable debug symbols and assertions in profile builds.
	CFLAGS += -g
	CFLAGS += -DRX_DEBUG

	# Use slightly less aggressive optimizations in profile builds.
	CFLAGS += -O2
	CFLAGS += -fno-inline-functions
	CFLAGS += -fno-inline-functions-called-once
	CFLAGS += -fno-optimize-sibling-calls
else
	# Enable assertions in release temporarily.
	CFLAGS += -DRX_DEBUG

	# Remotery
	CFLAGS += -DRMT_ENABLED=1
	CFLAGS += -DRMT_USE_OPENGL=1

	# Disable default C assertions.
	CFLAGS += -DNDEBUG

	# Highest optimization flag in release builds.
	CFLAGS += -O3

	# Disable all the stack protection features in release builds.
	CFLAGS += -fno-stack-protector
	CFLAGS += -fno-stack-check

	ifeq ($(CC),gcc)
		CFLAGS += -fno-stack-clash-protection
	endif

	# Disable frame pointer in release builds when AddressSanitizer isn't present.
	ifeq ($(ASAN),1)
		CFLAGS += -fno-omit-frame-pointer
	else
		DFLAGS += -fomit-frame-pointer
	endif
endif

# Sanitizer selection.
ifeq ($(ASAN),1)
	CFLAGS += -fsanitize=address
endif
ifeq ($(TSAN),1)
	CFLAGS += -fsanitize=thread -DRX_TSAN
endif
ifeq ($(UBSAN),1)
	CFLAGS += -fsanitize=undefined
endif

#
# C compilation flags.
#
CCFLAGS := $(CFLAGS)
CCFLAGS += -std=c11 -D_DEFAULT_SOURCE

#
# C++ compilation flags.
#
CXXFLAGS := -std=c++17

# Disable some unneeded features.
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti

#
# Linker flags.
#
LDFLAGS := -lpthread
LDFLAGS += `sdl2-config --libs`

# Enable profiling if requested.
ifeq ($(PROFILE),1)
	LDFLAGS += -pg
	LDFLAGS += -no-pie
endif

# Enable link-time optimizations if requested.
ifeq ($(LTO),1)
	LDFLAGS += -flto
endif

# Sanitizer selection.
ifeq ($(ASAN),1)
	LDFLAGS += -fsanitize=address
endif
ifeq ($(TSAN),1)
	LDFLAGS += -fsanitize=thread
endif
ifeq ($(UBSAN),1)
	LDFLAGS += -fsanitize=undefined
endif

ifneq (,$(findstring RX_DEBUG,$(CFLAGS)))
	STRIP := true
else
	STRIP := strip
endif

BIN := rex

all: $(BIN)

%.o: %.cpp
	$(CXX) -xc++ $(CFLAGS) $(CXXFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) $(CCFLAGS) -c -o $@ $<

$(BIN): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	$(STRIP) $@

clean:
	rm -rf $(OBJS) $(DEPS) $(BIN)

.PHONY: clean

-include $(DEPS)
