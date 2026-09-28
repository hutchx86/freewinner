# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# The isp_loop_* event-loop API is private (src/framework/framework_internal.h);
# events.c is a context-side (short-enum) framework object.
$(BUILD)/test_framework_events: src/framework/events.c tests/test_framework_events.c src/framework/framework_internal.h include/framework_isp.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Isrc/framework $(CFLAGS) -fshort-enums -DISP521_RTOS_ALGO=1 -pthread \
	    src/framework/events.c tests/test_framework_events.c -o $@ $(LDFLAGS)
