# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Clean-room multi-module host build.
#
# Each module ships its own build fragment in mk/<module>.mk defining a
# $(BUILD)/test_<module> target.  This top-level Makefile only wires the
# fragments together, so modules can be added or edited independently.

CC       ?= gcc
# -std=gnu99 -D_GNU_SOURCE: reconciled with cleanimpl-isp-r1/impl/Makefile
# (unit F/H's isp_dev_uapi.c uses readlink/O_CLOEXEC, which need a GNU/POSIX
# feature-test macro; strict c99 hides them).
CFLAGS   ?= -std=gnu99 -D_GNU_SOURCE -O2 -g -Wall -Wextra -fwrapv
CPPFLAGS ?= -Iinclude
LDFLAGS  ?= -lm
BUILD    := build

-include $(wildcard mk/*.mk)

TESTS := $(patsubst mk/%.mk,$(BUILD)/test_%,$(wildcard mk/*.mk))
# mk/reg.mk ships the register-writer tier, whose binary is test_reg_writers.
TESTS := $(patsubst $(BUILD)/test_reg,$(BUILD)/test_reg_writers,$(TESTS))

# The runtime table locator: no SDK, no vendor material.
TABLE_OBJ := $(BUILD)/rmm_tables.o

.PHONY: all check clean
all: $(TESTS) $(TABLE_OBJ)

$(TABLE_OBJ): src/tables/rmm_tables.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check: all
	@rc=0; for t in $(TESTS); do \
		echo "== $$t =="; \
		$$t || rc=1; \
	done; exit $$rc

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
