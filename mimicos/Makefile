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

# Targets
.PHONY: all clean

all: $(MAIN_BINARY)

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

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
