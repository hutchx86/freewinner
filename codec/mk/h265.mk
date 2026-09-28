# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.265 unit sources (spec h265/): the command-block image builder, the
# VPS/SPS/PPS + slice-header generator and the rate-control model. The VE driver
# and the encoder-internal ISP are the shared H.264 units (mk/h264.mk).
#
# Cross: `make arm-h265 TC=<toolchain prefix>` compiles these for armhf.

H265_HDRS := include/freecodec/h265_regs.h include/freecodec/h265_headers.h \
    include/freecodec/h265_rc.h include/freecodec/venc_types.h \
    include/freecodec/h264_bufs.h

TC              ?=
ARM_H265_OUT    ?= build_arm_h265
ARM_H265_CFLAGS ?= -march=armv7ve -mfloat-abi=hard -fPIC -O2 -std=gnu99 \
    -Wall -Wextra -pthread

H265_ARM_SRCS := \
    src/h265/regs/h265_regs.c \
    src/h265/headers/h265_headers.c \
    src/h265/rc/h265_rc.c \
    src/h265/enc/freecodec_h265_enc.c \
    src/h265/enc/freecodec_h265_enc_regs.c
H265_ARM_OBJS := $(patsubst src/h265/%.c,$(ARM_H265_OUT)/%.o,$(H265_ARM_SRCS))

$(ARM_H265_OUT)/%.o: src/h265/%.c $(H265_HDRS)
	@mkdir -p $(dir $@)
	@test -n "$(TC)" || { echo "TC (toolchain prefix) is required for arm-h265"; exit 1; }
	$(TC)-gcc $(ARM_H265_CFLAGS) -Iinclude -Isrc/h265/enc -c $< -o $@

.PHONY: arm-h265
arm-h265: $(H265_ARM_OBJS)

# The codec cross target compiles the H.265 device units too.
arm-h264: $(H265_ARM_OBJS)

# Host unit objects.
H265_UNIT_OBJS := $(BUILD)/h265/regs/h265_regs.o $(BUILD)/h265/headers/h265_headers.o
H265_RC_OBJS   := $(BUILD)/h265/rc/h265_rc.o

$(BUILD)/h265/regs/h265_regs.o: src/h265/regs/h265_regs.c $(H265_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/h265/headers/h265_headers.o: src/h265/headers/h265_headers.c $(H265_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/h265/rc/h265_rc.o: src/h265/rc/h265_rc.c $(H265_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Vector-driven unit tests. test_headers pins the 28/51/11-byte parameter sets
# and the slice-header syntax; test_regs pins the Config A/B command block;
# test_rc pins the exact ratio/λ/QP relations.
$(BUILD)/h265/test_headers: tests/h265/test_headers.c $(BUILD)/h265/headers/h265_headers.o $(H265_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/h265/headers/h265_headers.o $(LDFLAGS) -o $@

$(BUILD)/h265/test_regs: tests/h265/test_regs.c $(BUILD)/h265/regs/h265_regs.o $(H265_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/h265/regs/h265_regs.o $(LDFLAGS) -o $@

$(BUILD)/h265/test_rc: tests/h265/test_rc.c $(BUILD)/h265/rc/h265_rc.o $(H265_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(BUILD)/h265/rc/h265_rc.o $(LDFLAGS) -o $@

H265_UNIT_TESTS := $(BUILD)/h265/test_headers $(BUILD)/h265/test_regs $(BUILD)/h265/test_rc
