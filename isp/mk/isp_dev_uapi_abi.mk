# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# A-LAYOUT (decision #2): the old tests/test_isp_dev_uapi_abi.c compiled
# isp_dev_uapi.h with ISP_UAPI_ABI_MODEL, a 32-bit-pointer host emulation for
# the vendor-shaped device structs (osd_fmt/orl_fmt/actuator_ctrl/... and the
# `struct isp_dev_operations` vtable), none of which cleanimpl's isp_dev_uapi.h
# declares (H hazard H-1 is fixed at the header source instead: no enum-typed
# members, so the device-layer structs are byte-identical with and without
# -fshort-enums, 20 §2.1; decision #5 drops the vtable).  That test's role is
# replaced by cleanimpl's own proven mechanism, reused verbatim: build the
# project's device-layer probe (tests/layout_probe.c) in both enum ABIs and
# diff the reported sizes -- upstream's "A-LAYOUT" check
# (cleanimpl-isp-r1/impl/Makefile's `layout` target).
$(BUILD)/layout_a: tests/layout_probe.c include/isp_dev_uapi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@
$(BUILD)/layout_b: tests/layout_probe.c include/isp_dev_uapi.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fshort-enums -DISP521_RTOS_ALGO=1 $< -o $@

# Wrapped as a generated $(BUILD)/test_% binary so the top Makefile's checks
# loop (which just runs each $(BUILD)/test_% and checks its exit status)
# picks it up like any other unit test.
$(BUILD)/test_isp_dev_uapi_abi: $(BUILD)/layout_a $(BUILD)/layout_b
	@a=$$($(BUILD)/layout_a); b=$$($(BUILD)/layout_b); \
	if [ "$$a" = "$$b" ]; then \
	    printf '#!/bin/sh\necho "A-LAYOUT PASSED: %s"\necho "1 checks, 0 failures"\n' "$$a" > $@; \
	else \
	    printf '#!/bin/sh\necho "A-LAYOUT FAILED"\necho "normal: %s"\necho "short : %s"\necho "1 checks, 1 failures"\nexit 1\n' "$$a" "$$b" > $@; \
	fi
	@chmod +x $@
