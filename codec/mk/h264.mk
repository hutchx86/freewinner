# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.264 unit sources bound by the encoder device (the `VENC_DEVICE` ver2
# table). They resolve with the clean `-Iinclude` root alone
# (freecodec/vencoder.h + freecodec/ve_iface.h + freecodec/venc_base_abi.h);
# no vendor libcedarc include root is needed.
#
# The rate-control unit and the encoder-device assembly are quarantined
# (clean-room r3) and are re-authored by stream 1, which restores the
# device object rule and its tests here.
#
# Cross: `make arm-h264 TC=<toolchain prefix>` compiles the kept units for armhf.

H264_ENC_HDRS   := $(wildcard include/freecodec/*.h)

# Cross build (armhf musl): make arm-h264 TC=<prefix>. The prefix is the
# toolchain bin path without the trailing `-gcc`, e.g.
#   TC=repos/lindenis-v833-prebuilt/gcc/linux-x86/arm/toolchain-sunxi-musl/toolchain/bin/arm-openwrt-linux-muslgnueabi
TC              ?=
ARM_H264_OUT    ?= build_arm_h264
ARM_H264_CFLAGS ?= -march=armv7ve -mfloat-abi=hard -fPIC -O2 -std=gnu99 \
    -Wall -Wextra -pthread

H264_ARM_SRCS := \
    src/h264/regs/h264_regs.c \
    src/h264/headers/h264_headers.c \
    src/h264/isp/h264_isp.c \
    src/h264/gop/h264_gop.c \
    src/h264/ve/ve_driver.c \
    src/h264/rc/h264_rc.c \
    src/h264/enc/freecodec_enc.c \
    src/h264/enc/freecodec_enc_regs.c
H264_ARM_OBJS := $(patsubst src/h264/%.c,$(ARM_H264_OUT)/%.o,$(H264_ARM_SRCS))

$(ARM_H264_OUT)/%.o: src/h264/%.c $(H264_ENC_HDRS)
	@mkdir -p $(dir $@)
	@test -n "$(TC)" || { echo "TC (toolchain prefix) is required for arm-h264"; exit 1; }
	$(TC)-gcc $(ARM_H264_CFLAGS) -Iinclude -Isrc/h264/enc -c $< -o $@

.PHONY: arm-h264
arm-h264: $(H264_ARM_OBJS)

# Host tests of the device's vector-driven parts (no goldens needed):
#   test_mbrc    per-row MB-RC budget table vs tests/h264/data/mbrc_table.csv
#   test_bufsize auxiliary-plane size vs tests/h264/data/subpic_size.csv
H264_UNIT_OBJS := $(BUILD)/h264/regs/h264_regs.o $(BUILD)/h264/headers/h264_headers.o \
    $(BUILD)/h264/isp/h264_isp.o $(BUILD)/h264/gop/h264_gop.o

$(BUILD)/h264/test_mbrc: tests/h264/test_mbrc.c $(H264_ENC_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DMBRC_VECTORS='"$(CURDIR)/tests/h264/data/mbrc_table.csv"' $< -o $@

$(BUILD)/h264/test_bufsize: tests/h264/test_bufsize.c $(BUILD)/h264/regs/h264_regs.o \
		$(H264_ENC_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DBUFSIZE_VECTORS='"$(CURDIR)/tests/h264/data/subpic_size.csv"' \
	    $< $(BUILD)/h264/regs/h264_regs.o -o $@

H264_ENC_TESTS := $(BUILD)/h264/test_mbrc $(BUILD)/h264/test_bufsize
