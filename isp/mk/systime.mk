# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Monotonic cond timeout + wall-clock helpers; 4-byte enums, never -fshort-enums.
$(BUILD)/test_systime: src/utils/systime.c tests/test_systime.c include/utils/systime.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread src/utils/systime.c tests/test_systime.c -o $@ $(LDFLAGS) -pthread
