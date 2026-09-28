# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# H.264 frame-level rate control (spec 10), pure computation. test_rc checks it
# against the RC_VECTOR_DIR vectors (not shipped; SKIP without them).

H264_RC_SRC  := src/h264/rc/h264_rc.c
H264_RC_OBJS := $(BUILD)/h264/rc/h264_rc.o
H264_RC_HDRS := include/freecodec/h264_rc.h

$(BUILD)/h264/rc/h264_rc.o: $(H264_RC_SRC) $(H264_RC_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Black-box vectors: private, not in this repository (test skips without them).
RC_VECTOR_DIR ?= $(CURDIR)/../spec/vectors

$(BUILD)/test_rc: tests/h264/test_rc.c $(H264_RC_OBJS) $(H264_RC_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DRC_VECTOR_DIR='"$(RC_VECTOR_DIR)"' \
	    $< $(H264_RC_OBJS) $(LDFLAGS) -o $@

H264_RC_TEST := $(BUILD)/test_rc
