# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# Capture runtime (fcap_): the AW_MPI_SYS_*/AW_MPI_VI_* entry points, a static
# archive replacing the vendor MPP capture objects. Default 4-byte enums.
# Host test links the real isp_dev_uapi.c driven by the seam's mock syscalls.

FCAP_CFLAGS := -Isrc/fcap -Isrc/fisp -DISP_VERSION=521 -pthread
FCAP_SRCS   := $(wildcard src/fcap/*.c)
FCAP_OBJS   := $(patsubst src/fcap/%.c,$(BUILD)/fcap/%.o,$(FCAP_SRCS))
FCAP_HDRS   := $(wildcard src/fcap/*.h) src/fisp/fisp_abi.h \
    include/isp_dev_uapi.h include/framework_events.h \
    include/media_utils_abi.h include/utils/frame_pool.h \
    include/utils/msgqueue.h include/utils/semaphore.h \
    include/utils/pixel_format.h

$(BUILD)/fcap/%.o: src/fcap/%.c $(FCAP_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FCAP_CFLAGS) -c $< -o $@

$(BUILD)/libfcap.a: $(FCAP_OBJS) | $(BUILD)
	rm -f $@
	$(AR) rcs $@ $^

# One host test binary, tests/fcap/test_fcap.c, plus the target-ABI conformance
# TU compiled with the 32-bit layout model.
FCAP_TEST_SRCS := $(filter-out tests/fcap/test_fcap_abi.c, \
    $(wildcard tests/fcap/test_*.c))
FCAP_TEST_SEAM := $(BUILD)/fcap/seam.o
FCAP_TEST_ABI  := $(BUILD)/fcap/test_fcap_abi.o
FCAP_TEST_SUPPORT := src/utils/semaphore.c src/utils/systime.c \
    src/utils/msgqueue.c src/utils/frame_pool.c src/utils/pixel_format.c \
    src/framework/isp_dev_uapi.c

$(BUILD)/fcap/test_fcap_abi.o: tests/fcap/test_fcap_abi.c $(FCAP_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FCAP_CFLAGS) -std=gnu99 \
	    -DFCAP_ABI_MODEL -c $< -o $@

$(BUILD)/fcap/seam.o: tests/fcap/fcap_seam.c tests/fcap/fcap_seam.h \
		$(FCAP_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FCAP_CFLAGS) -c $< -o $@

$(BUILD)/test_fcap: $(FCAP_TEST_SRCS) $(FCAP_OBJS) $(FCAP_TEST_SEAM) \
		$(FCAP_TEST_ABI) $(FCAP_TEST_SUPPORT) $(FCAP_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FCAP_CFLAGS) $(FCAP_TEST_SRCS) \
	    $(FCAP_OBJS) $(FCAP_TEST_SEAM) $(FCAP_TEST_ABI) $(FCAP_TEST_SUPPORT) \
	    -o $@ $(LDFLAGS) -pthread

.PHONY: fcap
fcap: $(BUILD)/libfcap.a

all: fcap
