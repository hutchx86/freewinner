# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# V4L2 fourcc <-> PIXEL_FORMAT_E maps; default 4-byte enums, never -fshort-enums.
$(BUILD)/test_pixel_format: src/utils/pixel_format.c tests/test_pixel_format.c include/utils/pixel_format.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/pixel_format.c tests/test_pixel_format.c -o $@ $(LDFLAGS)
