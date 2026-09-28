# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# BITMAP_S payload byte count; default 4-byte enums, never -fshort-enums.
$(BUILD)/test_bitmap: src/utils/bitmap.c tests/test_bitmap.c include/utils/bitmap.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/bitmap.c tests/test_bitmap.c -o $@ $(LDFLAGS)
