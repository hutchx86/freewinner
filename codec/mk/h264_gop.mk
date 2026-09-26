# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.264 normal-P GOP picture-ring unit.
# Its differential test is private: built only when ORACLE_TESTS
# names the directory holding it (goldens under GOLDEN_DIR).

H264_GOP_SRC  := src/h264/gop/h264_gop.c
H264_GOP_OBJS := $(BUILD)/h264/gop/h264_gop.o

$(BUILD)/h264/gop/h264_gop.o: $(H264_GOP_SRC) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ifneq ($(ORACLE_TESTS),)
$(BUILD)/test_gop: $(ORACLE_TESTS)/test_gop.c $(H264_GOP_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DGOP_GOLDEN='"$(GOLDEN_DIR)/gop_normal.txt"' \
	    $< $(H264_GOP_OBJS) $(LDFLAGS) -o $@

H264_GOP_TEST := $(BUILD)/test_gop
endif
