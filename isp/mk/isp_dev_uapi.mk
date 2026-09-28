# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# UAPI binding; long enums (no -fshort-enums). Tested by test_device.c
# against the fake_kernel.c mock ioctl backend.
$(BUILD)/test_isp_dev_uapi: src/framework/isp_dev_uapi.c tests/test_device.c tests/fake_kernel.c include/isp_dev_uapi.h tests/fake_kernel.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu99 -DISP_VERSION=521 src/framework/isp_dev_uapi.c tests/test_device.c tests/fake_kernel.c -o $@ $(LDFLAGS)
