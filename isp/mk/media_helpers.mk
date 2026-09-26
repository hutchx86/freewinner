# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# media-utils phase 2: MPP_CHN_S descriptor copy.  Built with the base CFLAGS,
# i.e. default 4-byte enums; never add -fshort-enums to this object.
$(BUILD)/test_media_helpers: src/utils/media_helpers.c tests/test_media_helpers.c include/utils/media_helpers.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/utils/media_helpers.c tests/test_media_helpers.c -o $@ $(LDFLAGS)
