# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Phase-2 UAPI binding.  The unit stays long-enum (no -fshort-enums).
#
# tests/test_isp_dev_uapi.c tested the OLD isp_dev_uapi.h's functional surface
# (struct isp_dev_operations, isp_dev_start/stop, actuator/flash wrappers, the
# ISP_CID table) -- none of which cleanimpl's isp_dev_uapi.h has (decision #5:
# no vtable; actuator/flash go through plain ioctls inline in the framework,
# 20 §4.6).  Replaced wholesale with cleanimpl's own test_device.c + its
# fake_kernel.c mock ioctl backend (already verified in the baseline: F-DEV
# 52 checks), which test the actual new API this header now exports.
$(BUILD)/test_isp_dev_uapi: src/framework/isp_dev_uapi.c tests/test_device.c tests/fake_kernel.c include/isp_dev_uapi.h tests/fake_kernel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu99 -DISP_VERSION=521 src/framework/isp_dev_uapi.c tests/test_device.c tests/fake_kernel.c -o $@ $(LDFLAGS)
