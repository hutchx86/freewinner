# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.264 ver2 register-shadow unit.
# Its differential test is private: built only when ORACLE_TESTS
# names the directory holding it (goldens under GOLDEN_DIR).

H264_REG_SRC  := src/h264/regs/h264_regs.c
H264_REG_OBJS := $(BUILD)/h264/regs/h264_regs.o

$(BUILD)/h264/regs/h264_regs.o: $(H264_REG_SRC) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

ifneq ($(ORACLE_TESTS),)
$(BUILD)/test_regs: $(ORACLE_TESTS)/test_regs.c $(H264_REG_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -DREGSHADOW_INFOREG='"$(GOLDEN_DIR)/regshadow_inforeg.txt"' \
	    -DREGSHADOW_HWSLICE='"$(GOLDEN_DIR)/regshadow_hwslice.txt"' \
	    -DREGSHADOW_SETREG='"$(GOLDEN_DIR)/regshadow_setreg.txt"' \
	    -DREGSHADOW_FULL='"$(GOLDEN_DIR)/regs_full.txt"' \
	    $< $(H264_REG_OBJS) $(LDFLAGS) -o $@

H264_REG_TEST := $(BUILD)/test_regs
endif
