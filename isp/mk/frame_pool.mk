# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Frame-header pool; default 4-byte enums, never -fshort-enums.
$(BUILD)/test_frame_pool: src/utils/frame_pool.c tests/test_frame_pool.c include/utils/frame_pool.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/frame_pool.c tests/test_frame_pool.c -o $@ $(LDFLAGS) -pthread
