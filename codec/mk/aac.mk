# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# AAC-LC wrapper + FAAC backend. FAAC_DIR is a caller-supplied FAAC tree
# (include/faac.h, libfaac/*.c); never vendored.

FAAC_DIR      ?= third_party/faac
FAAC_VERSION  ?= 2.1.0
FAAC_SRC_DIR  := $(FAAC_DIR)/libfaac
FAAC_CFLAGS   := -I$(FAAC_DIR)/include -include $(BUILD)/faac_config.h

FAAC_SRCS := $(filter-out %/quantize_sse.c,$(wildcard $(FAAC_SRC_DIR)/*.c))

AAC_OBJS := $(BUILD)/aac/freecodec_aac.o \
            $(patsubst $(FAAC_SRC_DIR)/%.c,$(BUILD)/faac/%.o,$(FAAC_SRCS))

$(BUILD)/faac_config.h: mk/faac_config.h.in | $(BUILD)
	sed 's/@FAAC_VERSION@/$(FAAC_VERSION)/' $< > $@

$(BUILD)/aac/freecodec_aac.o: src/aac/freecodec_aac.c $(BUILD)/faac_config.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FAAC_CFLAGS) -c $< -o $@

$(BUILD)/faac/%.o: $(FAAC_SRC_DIR)/%.c $(BUILD)/faac_config.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(FAAC_CFLAGS) -w -c $< -o $@

$(BUILD)/test_aac: tests/aac/test_aac.c $(AAC_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(AAC_OBJS) $(LDFLAGS) -o $@

# FAAC is supplied by the builder and never vendored. When it is absent the AAC
# unit cannot be built, so it is skipped rather than failing `make check`.
ifeq ($(wildcard $(FAAC_DIR)/include/faac.h),)
$(info aac: FAAC not found under $(FAAC_DIR) - skipping the AAC unit (set FAAC_DIR to include it))
AAC_TEST :=
else
AAC_TEST := $(BUILD)/test_aac
endif

# Cross build (armhf musl): make arm-aac TC=<prefix> FAAC_DIR=<tree>. The archive
# replaces -laacenc in the consumer link.
TC         ?=
ARM_OUT    ?= build_arm
ARM_CFLAGS ?= -march=armv7ve -mfloat-abi=hard -std=gnu11 -Os -fwrapv

ARM_AAC_OBJS := $(ARM_OUT)/freecodec_aac.o \
                $(patsubst $(FAAC_SRC_DIR)/%.c,$(ARM_OUT)/faac/%.o,$(FAAC_SRCS))

$(ARM_OUT)/faac_config.h: mk/faac_config.h.in | $(BUILD)
	@mkdir -p $(ARM_OUT)
	sed 's/@FAAC_VERSION@/$(FAAC_VERSION)/' $< > $@

$(ARM_OUT)/freecodec_aac.o: src/aac/freecodec_aac.c $(ARM_OUT)/faac_config.h
	@mkdir -p $(ARM_OUT)
	@test -n "$(TC)" || { echo "TC (toolchain prefix) is required for arm-aac"; exit 1; }
	$(TC)-gcc $(ARM_CFLAGS) -Iinclude -I$(FAAC_DIR)/include \
	    -include $(ARM_OUT)/faac_config.h -c $< -o $@

$(ARM_OUT)/faac/%.o: $(FAAC_SRC_DIR)/%.c $(ARM_OUT)/faac_config.h
	@mkdir -p $(dir $@)
	@test -n "$(TC)" || { echo "TC (toolchain prefix) is required for arm-aac"; exit 1; }
	$(TC)-gcc $(ARM_CFLAGS) -I$(FAAC_DIR)/include \
	    -include $(ARM_OUT)/faac_config.h -w -c $< -o $@

$(ARM_OUT)/libfreecodec_aac.a: $(ARM_AAC_OBJS)
	$(TC)-gcc-ar rcs $@ $^

.PHONY: arm-aac
arm-aac: $(ARM_OUT)/libfreecodec_aac.a
