# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Counted condition-variable semaphore; default 4-byte enums, never -fshort-enums.
$(BUILD)/test_semaphore: src/utils/semaphore.c tests/test_semaphore.c include/utils/semaphore.h include/media_utils_abi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread src/utils/semaphore.c tests/test_semaphore.c -o $@ $(LDFLAGS) -pthread
