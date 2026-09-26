# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.264 encoder-internal ISP unit.
# Its differential test is private: built only when ORACLE_TESTS
# names the directory holding it (goldens under GOLDEN_DIR).

H264_ISP_SRC  := src/h264/isp/h264_isp.c
H264_ISP_OBJS := $(BUILD)/h264/isp/h264_isp.o

$(BUILD)/h264/isp/h264_isp.o: $(H264_ISP_SRC) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ifneq ($(ORACLE_TESTS),)
$(BUILD)/test_isp: $(ORACLE_TESTS)/test_isp.c $(H264_ISP_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DISP_GOLDEN='"$(GOLDEN_DIR)/isp_regs.txt"' \
	    $< $(H264_ISP_OBJS) $(LDFLAGS) -o $@

H264_ISP_TEST := $(BUILD)/test_isp
endif
