# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# The fd-mux/event-loop API (isp_loop_*, struct isp_event_loop) moved to the
# private src/framework/framework_internal.h (H task-1 step 1 / decision #2);
# events.c is a context-side (short-enum) framework object (20 §2.1).
$(BUILD)/test_framework_events: src/framework/events.c tests/test_framework_events.c src/framework/framework_internal.h include/framework_isp.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Isrc/framework $(CFLAGS) -fshort-enums -DISP521_RTOS_ALGO=1 -pthread \
	    src/framework/events.c tests/test_framework_events.c -o $@ $(LDFLAGS)
