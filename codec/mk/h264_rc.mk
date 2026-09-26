# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.264 frame-level rate-control unit (spec/10-rate-control.md, r4). Pure
# computation, no device dependency: the host test drives the spec's own
# closed-loop plant (section 6) against the unit and checks section 3's
# contract, and checks the first-picture QP ladder exactly against
# spec/vectors/rc_first_qp*.csv.

H264_RC_SRC  := src/h264/rc/h264_rc.c
H264_RC_OBJS := $(BUILD)/h264/rc/h264_rc.o
H264_RC_HDRS := include/freecodec/h264_rc.h

$(BUILD)/h264/rc/h264_rc.o: $(H264_RC_SRC) $(H264_RC_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/test_rc: tests/h264/test_rc.c $(H264_RC_OBJS) $(H264_RC_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DRC_VECTOR_DIR='"$(CURDIR)/../spec/vectors"' \
	    $< $(H264_RC_OBJS) $(LDFLAGS) -o $@

H264_RC_TEST := $(BUILD)/test_rc
