# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Multi-module host build: each mk/<module>.mk defines $(BUILD)/test_<module>;
# this file only wires the fragments together.

CC       ?= gcc
# gnu99 + _GNU_SOURCE: isp_dev_uapi.c needs readlink/O_CLOEXEC.
CFLAGS   ?= -std=gnu99 -D_GNU_SOURCE -O2 -g -Wall -Wextra -fwrapv
CPPFLAGS ?= -Iinclude
LDFLAGS  ?= -lm
BUILD    := build

-include $(wildcard mk/*.mk)

TESTS := $(patsubst mk/%.mk,$(BUILD)/test_%,$(wildcard mk/*.mk))
# mk/reg.mk ships the register-writer tier, whose binary is test_reg_writers.
TESTS := $(patsubst $(BUILD)/test_reg,$(BUILD)/test_reg_writers,$(TESTS))

# The runtime table locator: no SDK, no vendor material.
TABLE_OBJ := $(BUILD)/rmm_tables.o $(BUILD)/pltm_presets.o $(BUILD)/ae_out_bias.o

.PHONY: all check clean
all: $(TESTS) $(TABLE_OBJ)

$(BUILD)/rmm_tables.o: src/tables/rmm_tables.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/pltm_presets.o: src/tables/pltm_presets.c include/freeisp/pltm_presets.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/ae_out_bias.o: src/tables/ae_out_bias.c include/freeisp/ae_out_bias.h | $(BUILD)
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
