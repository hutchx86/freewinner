# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# media-utils phase 2: BITMAP_S payload byte count.  Built with the base CFLAGS,
# i.e. default 4-byte enums; never add -fshort-enums to this object.
$(BUILD)/test_bitmap: src/utils/bitmap.c tests/test_bitmap.c include/utils/bitmap.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/bitmap.c tests/test_bitmap.c -o $@ $(LDFLAGS)
