# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# media-utils phase 3c: stateful frame-header pool.  Built with the base CFLAGS,
# i.e. default 4-byte enums; never add -fshort-enums to this object.  pthread is
# required for the manager mutex/cond.
$(BUILD)/test_frame_pool: src/utils/frame_pool.c tests/test_frame_pool.c include/utils/frame_pool.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/frame_pool.c tests/test_frame_pool.c -o $@ $(LDFLAGS) -pthread
