# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
$(BUILD)/test_pltm_presets: src/tables/pltm_presets.c tests/test_pltm_presets.c include/freeisp/pltm_presets.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@ $(LDFLAGS)
