# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
$(BUILD)/test_reg_writers: src/reg/reg_writers.c tests/test_reg_writers.c include/reg_writers.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS)
