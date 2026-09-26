# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
$(BUILD)/test_afs: src/afs/afs_clean.c tests/test_afs.c include/afs_clean.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS)
