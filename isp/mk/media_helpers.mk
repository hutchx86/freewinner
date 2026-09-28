# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# MPP_CHN_S descriptor copy; default 4-byte enums, never -fshort-enums.
$(BUILD)/test_media_helpers: src/utils/media_helpers.c tests/test_media_helpers.c include/utils/media_helpers.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/media_helpers.c tests/test_media_helpers.c -o $@ $(LDFLAGS)
