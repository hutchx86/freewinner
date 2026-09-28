# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# A-LAYOUT: the device-layer structs have no enum-typed members, so the probe
# must report identical sizes with and without -fshort-enums.
$(BUILD)/layout_a: tests/layout_probe.c include/isp_dev_uapi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
$(BUILD)/layout_b: tests/layout_probe.c include/isp_dev_uapi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fshort-enums -DISP521_RTOS_ALGO=1 $< -o $@

# Wrapped as a $(BUILD)/test_% script so the top-level check loop runs it.
$(BUILD)/test_isp_dev_uapi_abi: $(BUILD)/layout_a $(BUILD)/layout_b
	@a=$$($(BUILD)/layout_a); b=$$($(BUILD)/layout_b); \
	if [ "$$a" = "$$b" ]; then \
	    printf '#!/bin/sh\necho "A-LAYOUT PASSED: %s"\necho "1 checks, 0 failures"\n' "$$a" > $@; \
	else \
	    printf '#!/bin/sh\necho "A-LAYOUT FAILED"\necho "normal: %s"\necho "short : %s"\necho "1 checks, 1 failures"\nexit 1\n' "$$a" "$$b" > $@; \
	fi
	@chmod +x $@
