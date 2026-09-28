# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# ISP runtime (fisp_): the 25 AW_MPI_ISP_* entry points, a static archive that
# replaces the vendor mpi_isp.c/mpi_vi.c ISP objects in the mediad link.
# Host test: fisp_ objects + scripted seam (tests/fisp/fisp_seam.c), no device.

FISP_CFLAGS := -Isrc/fisp -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 \
    -fshort-enums -pthread
FISP_SRCS   := $(wildcard src/fisp/*.c)
FISP_OBJS   := $(patsubst src/fisp/%.c,$(BUILD)/fisp/%.o,$(FISP_SRCS))
FISP_HDRS   := $(wildcard src/fisp/*.h) include/framework_isp.h \
    include/isp_dev_uapi.h include/framework_events.h

$(BUILD)/fisp/%.o: src/fisp/%.c $(FISP_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FISP_CFLAGS) -c $< -o $@

$(BUILD)/libfisp.a: $(FISP_OBJS) | $(BUILD)
	rm -f $@
	$(AR) rcs $@ $^

FISP_TEST_SRCS := $(filter-out tests/fisp/test_fisp_abi.c, \
    $(wildcard tests/fisp/test_*.c))
FISP_TEST_SEAM := $(BUILD)/fisp/seam.o
FISP_TEST_ABI  := $(BUILD)/fisp/test_fisp_abi.o

# Target-ABI conformance TU: compiled with the 32-bit layout model.
$(BUILD)/fisp/test_fisp_abi.o: tests/fisp/test_fisp_abi.c $(FISP_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu99 -DISP_VERSION=521 \
	    -DISP_UAPI_ABI_MODEL -c $< -o $@

$(BUILD)/fisp/seam.o: tests/fisp/fisp_seam.c tests/fisp/fisp_seam.h \
		$(FISP_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FISP_CFLAGS) -c $< -o $@

$(BUILD)/test_fisp: $(FISP_TEST_SRCS) $(FISP_OBJS) $(FISP_TEST_SEAM) \
		$(FISP_TEST_ABI) $(FISP_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FISP_CFLAGS) $(FISP_TEST_SRCS) \
	    $(FISP_OBJS) $(FISP_TEST_SEAM) $(FISP_TEST_ABI) -o $@ \
	    $(LDFLAGS) -pthread

.PHONY: fisp
fisp: $(BUILD)/libfisp.a

all: fisp
