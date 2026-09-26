# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# Video-engine driver unit: the driver plus its host test with a mock port.

VE_CFLAGS := -pthread

VE_OBJS := $(BUILD)/ve/ve_driver.o

$(BUILD)/ve/ve_driver.o: src/h264/ve/ve_driver.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(VE_CFLAGS) -c $< -o $@

$(BUILD)/test_ve: tests/h264/test_ve.c $(VE_OBJS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(VE_CFLAGS) $< $(VE_OBJS) $(LDFLAGS) -o $@

VE_TEST := $(BUILD)/test_ve
