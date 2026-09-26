# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# media-utils phase 3a: per-PIXEL_FORMAT_E frame sizes.  Built with the base
# CFLAGS, i.e. default 4-byte enums; never add -fshort-enums to this object.
$(BUILD)/test_frame_size: src/utils/frame_size.c tests/test_frame_size.c include/utils/frame_size.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/frame_size.c tests/test_frame_size.c -o $@ $(LDFLAGS)
