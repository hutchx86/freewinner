# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# Overlay (OSD) packer unit tests; reuses H264_ISP_OBJS from mk/h264_isp.mk and
# needs no golden (see tests/h264/test_overlay.c).

$(BUILD)/test_overlay: tests/h264/test_overlay.c $(H264_ISP_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(H264_ISP_OBJS) $(LDFLAGS) -o $@

H264_OVERLAY_TEST := $(BUILD)/test_overlay
