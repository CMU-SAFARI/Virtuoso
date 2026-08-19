# Compiler and flags
CC  = gcc
CXX = g++
# Optimisation level.  Apr 19 2026: was unset (= -O0), which meant every
# `static inline` helper in include/mm/linux_phase_work.h (the hot fault
# path) was emitted as a real call with stack-frame setup — Sniper then
# simulates the extra x86 instructions, inflating per-fault wall time by
# 2-4×.  Default to -O2 -DNDEBUG; override with OPT_LEVEL=-O0 for
# debug-friendly builds.
OPT_LEVEL ?= -O2 -DNDEBUG
# MIMICOS_CALIBRATION_O0 gates the #pragma GCC optimize("O0") in
# buddy.h / linux_buddy_anon.h so those allocator files stay at -O0
# when MimicOS includes them — preserving the simulated-cycle cost we
# calibrated against Linux (phys_alloc mean ~2 300 cyc) — while Sniper's
# sniperspace build of the same files keeps its default -O3.
OPT_FLAGS = $(OPT_LEVEL) -DMIMICOS_CALIBRATION_O0
CFLAGS    = -Wall -Wextra -g $(OPT_FLAGS)
CXXFLAGS  = -Wall -Wextra -g $(OPT_FLAGS)
LDFLAGS   =

# Directories
SRC_DIR = ./src

# SNIPER_INCLUDE points to the canonical MimicOS core headers (allocators, etc.)
# Override with: make SNIPER_INCLUDE=/path/to/sniper/include
SNIPER_INCLUDE ?= ../simulator/sniper/include

INCLUDE_DIR = -I./include/mm
INCLUDE_DIR += -I./include/misc
INCLUDE_DIR += -I./include/
INCLUDE_DIR += -I./src
INCLUDE_DIR += -I$(SNIPER_INCLUDE)

BUILD_DIR = ./build
OBJ_DIR = $(BUILD_DIR)/obj

# Source files (-L follows symlinks so this works when invoked via symlinked directory)
C_SRC_FILES   = $(shell find -L $(SRC_DIR) -type f -name '*.c')
CPP_SRC_FILES = $(shell find -L $(SRC_DIR) -type f -name '*.cc')

# Object lists
C_OBJ_FILES   = $(C_SRC_FILES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
CPP_OBJ_FILES = $(CPP_SRC_FILES:$(SRC_DIR)/%.cc=$(OBJ_DIR)/%.o)
OBJ_FILES     = $(C_OBJ_FILES) $(CPP_OBJ_FILES)

# Main binary
MAIN_BINARY = $(BUILD_DIR)/startup_mimicos

# ---------------------------------------------------------------------------
# libmimicos — the simulator-agnostic embedding library (include/mimicos_embed.h)
#
# Host simulators (gem5 syscall-emulation mode, ChampSim) link against this
# instead of running MimicOS as a separate SDE-instrumented process.  The
# library deliberately excludes src/mimicos/mimicos.cc, which is the only
# translation unit that speaks Sniper's magic-instruction protocol via
# sim_api.h; everything else — allocators, radix page table, fault-cost
# calibration — is shared verbatim with the standalone kernel.
#
# Note on optimisation: the library is compiled WITHOUT
# -DMIMICOS_CALIBRATION_O0.  That flag exists so the standalone kernel's
# allocator code keeps the instruction count we calibrated against Linux when
# SDE simulates it.  Under the library, MimicOS executes natively inside the
# host simulator and is never simulated, so fault cost comes from the
# analytical [fault_calibration] model instead; forcing -O0 there would only
# slow the host down.
LIB_STATIC = $(BUILD_DIR)/libmimicos.a
LIB_SHARED = $(BUILD_DIR)/libmimicos.so
LIB_OBJ_DIR = $(BUILD_DIR)/libobj

LIB_SRC_FILES  = $(filter-out $(SRC_DIR)/mimicos/mimicos.cc,$(CPP_SRC_FILES))
LIB_CPP_OBJS   = $(LIB_SRC_FILES:$(SRC_DIR)/%.cc=$(LIB_OBJ_DIR)/%.o)
LIB_C_OBJS     = $(C_SRC_FILES:$(SRC_DIR)/%.c=$(LIB_OBJ_DIR)/%.o)
LIB_OBJ_FILES  = $(LIB_C_OBJS) $(LIB_CPP_OBJS)

LIB_OPT_FLAGS  = $(OPT_LEVEL) -fPIC
# -MMD -MP: emit .d files so a header edit actually triggers a rebuild.  The
# legacy startup_mimicos rules have no dependency tracking, which makes stale
# objects easy to miss; the library build does not repeat that mistake.
LIB_DEPFLAGS   = -MMD -MP
LIB_CFLAGS     = -Wall -Wextra -g $(LIB_OPT_FLAGS) $(LIB_DEPFLAGS)
LIB_CXXFLAGS   = -Wall -Wextra -g $(LIB_OPT_FLAGS) $(LIB_DEPFLAGS)
LIB_DEP_FILES  = $(LIB_OBJ_FILES:.o=.d)

# Targets
.PHONY: all clean lib lib-static lib-shared test-embed check-embed replay fault-driver gem5-server

all: $(MAIN_BINARY)

lib: lib-static lib-shared
lib-static: $(LIB_STATIC)
lib-shared: $(LIB_SHARED)

# Create necessary directories
$(OBJ_DIR):
	mkdir -p $@

# Ensure all object subdirectories are created (mkdir on demand in compile rules)
# (OBJ_SUBDIRS block kept as-is; it's harmless even if empty)
OBJ_SUBDIRS :=
$(OBJ_FILES): | $(OBJ_SUBDIRS)

$(OBJ_SUBDIRS):
	mkdir -p $@

# Ensure the top-level build directory exists before linking outputs
$(BUILD_DIR):
	mkdir -p $@

# Build C object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@

# Build C++ object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(INCLUDE_DIR) $(CXXFLAGS) -c $< -o $@

# Link main binary (will include build/obj/virtuos/virtuos.o automatically)
$(MAIN_BINARY): $(OBJ_FILES) startup_mimicos.cc | $(BUILD_DIR)
	$(CXX) $(INCLUDE_DIR) $(CXXFLAGS) $^ -o $@ -lrt

# ---- libmimicos self-test (links only against the library) ----
EMBED_TEST = $(BUILD_DIR)/embed_smoke

test-embed: $(EMBED_TEST)

$(EMBED_TEST): tests/embed_smoke.cc include/mimicos_embed.h $(LIB_STATIC) | $(BUILD_DIR)
	$(CXX) -I./include $(LIB_OPT_FLAGS) -Wall -Wextra -g $< $(LIB_STATIC) -o $@ -lrt

check-embed: $(EMBED_TEST)
	$(EMBED_TEST)

# ---- cross-simulator equivalence reference driver ----
REPLAY_BINARY = $(BUILD_DIR)/mimicos_replay

replay: $(REPLAY_BINARY)

$(REPLAY_BINARY): tests/mimicos_replay.cc include/mimicos_embed.h $(LIB_STATIC) | $(BUILD_DIR)
	$(CXX) -I./include $(LIB_OPT_FLAGS) -Wall -Wextra -g $< $(LIB_STATIC) -o $@ -lrt

# ---- fault driver, recorded under a tracer to capture the handler ----
FAULT_DRIVER = $(BUILD_DIR)/fault_driver

fault-driver: $(FAULT_DRIVER)

$(FAULT_DRIVER): tests/fault_driver.cc include/mimicos_embed.h $(LIB_STATIC) | $(BUILD_DIR)
	$(CXX) -I./include $(LIB_OPT_FLAGS) -Wall -Wextra -g $< $(LIB_STATIC) -o $@ -lrt

# ---- MimicOS as a gem5 guest process ----
# Static: the guest runs under syscall emulation, where a dynamic
# loader is a lot of instructions to simulate before main() is reached.
GEM5_SERVER = $(BUILD_DIR)/mimicos_gem5_server

gem5-server: $(GEM5_SERVER)

$(GEM5_SERVER): tests/mimicos_gem5_server.cc include/mimicos_embed.h $(LIB_STATIC) | $(BUILD_DIR)
	$(CXX) -I./include $(OPT_LEVEL) -Wall -Wextra -g -static $< $(LIB_STATIC) -o $@ -lrt

# ---- libmimicos object rules (separate object dir: different flags) ----
$(LIB_OBJ_DIR):
	mkdir -p $@

$(LIB_OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(LIB_OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(INCLUDE_DIR) $(LIB_CFLAGS) -c $< -o $@

$(LIB_OBJ_DIR)/%.o: $(SRC_DIR)/%.cc | $(LIB_OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(INCLUDE_DIR) $(LIB_CXXFLAGS) -c $< -o $@

$(LIB_STATIC): $(LIB_OBJ_FILES) | $(BUILD_DIR)
	$(AR) rcs $@ $^

$(LIB_SHARED): $(LIB_OBJ_FILES) | $(BUILD_DIR)
	$(CXX) -shared $^ -o $@ -lrt

-include $(LIB_DEP_FILES)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
