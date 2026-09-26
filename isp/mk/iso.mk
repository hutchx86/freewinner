# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
$(BUILD)/test_iso: src/iso/iso_clean.c tests/test_iso.c include/iso_clean.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS)
