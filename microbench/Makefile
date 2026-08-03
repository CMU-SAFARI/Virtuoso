# Microbenchmark suite for Virtuoso/MimicOS live-spawn (Stage B).
#
# Every .c file under a family subdirectory (fault/, stream/, stride/, vma/,
# cow/, thp/) becomes an executable of the same basename next to its source.
# Each bench is self-contained (includes common/bench.h) — no shared .o.
#
# Usage:
#   make              # build all benches
#   make clean        # remove all built binaries
#   make FAMILIES="fault stream"   # restrict to listed families

FAMILIES ?= fault stream stride vma cow thp thread
SNIPER_INCLUDE ?= ../simulator/sniper/include

CC       := g++
CFLAGS   := -O2 -Wall -Wextra -no-pie -I$(SNIPER_INCLUDE) -x c++
LDFLAGS  := -pthread

# Collect source/binary pairs across all requested families.
SRCS := $(foreach d,$(FAMILIES),$(wildcard $(d)/*.c))
BINS := $(SRCS:.c=)

.PHONY: all clean list
all: $(BINS)

%: %.c common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

list:
	@echo "Families: $(FAMILIES)"
	@echo "Sources:"
	@for s in $(SRCS); do echo "  $$s"; done
	@echo "Binaries:"
	@for b in $(BINS); do echo "  $$b"; done

clean:
	rm -f $(BINS)
