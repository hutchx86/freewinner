# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Spec-based host test for the three live media-utility entry points (bitmap,
# frame_size, media_helpers). Default 4-byte enums; never -fshort-enums.
UTILS_LIVE_SRCS := src/utils/bitmap.c src/utils/frame_size.c src/utils/media_helpers.c
$(BUILD)/test_utils_live: $(UTILS_LIVE_SRCS) tests/test_utils_live.c include/utils/bitmap.h include/utils/frame_size.h include/utils/media_helpers.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTILS_LIVE_SRCS) tests/test_utils_live.c -o $@ $(LDFLAGS)
