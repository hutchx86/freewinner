# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.264 SPS/PPS header generator.
# Its differential test is private: built only when ORACLE_TESTS
# names the directory holding it (goldens under GOLDEN_DIR).

H264_HEADERS_SRC  := src/h264/headers/h264_headers.c
H264_HEADERS_OBJS := $(BUILD)/h264/headers/h264_headers.o

$(BUILD)/h264/headers/h264_headers.o: $(H264_HEADERS_SRC) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ifneq ($(ORACLE_TESTS),)
$(BUILD)/test_headers: $(ORACLE_TESTS)/test_headers.c $(H264_HEADERS_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DGOLDEN_PATH='"$(GOLDEN_DIR)/diff_sps.txt"' \
	    -DGOLDEN_SLICE_PATH='"$(GOLDEN_DIR)/diff_slice.txt"' \
	    $< $(H264_HEADERS_OBJS) $(LDFLAGS) -o $@

H264_HEADERS_TEST := $(BUILD)/test_headers
endif
