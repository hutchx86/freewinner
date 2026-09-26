# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# On-device runtime table feed - host + ARM build.
#
#   make            host test  -> build/test_shim_tables
#   make check      build + run against caller-supplied `rmm` images
#   make arm        static ARM build -> build_arm/test_shim_tables.arm
#   make run-arm    run the ARM build under qemu-arm
#   make clean
#
# This layer needs no SDK: it compiles the table locator
# (src/tables/rmm_tables.c) plus the clean headers only.  Running `make check`
# needs stock `rmm` images, which are vendor firmware and are not shipped:
# pass them as arguments, or set FREEWINNER_RMM_IMAGES (colon-separated).
# With no image the test exercises nothing and exits 0.

CC       ?= gcc
ROOT     := $(abspath $(CURDIR)/../..)
INC      := $(ROOT)/include
FW_SRC   := $(ROOT)/src/tables/rmm_tables.c

HOST_CFLAGS := -std=gnu99 -O2 -Wall -Wextra -fwrapv \
               -I$(INC) -I.

# Set CROSS to the cross-toolchain prefix for the `arm` targets, e.g.
#   .../toolchain-sunxi-musl/toolchain/bin/arm-openwrt-linux-muslgnueabi-
CROSS      ?=
ARM_CC     = $(if $(CROSS),$(CROSS)gcc,$(error CROSS is not set - pass CROSS=<cross-toolchain prefix>))
ARM_CFLAGS := -march=armv7ve -mfloat-abi=hard -fshort-enums \
              -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 \
              -O2 -std=gnu99 -Wall -Wextra -fwrapv \
              -I$(INC) -I.
ARM_LDFLAGS := -static
# This musl libc is built with dynamic unwind tables, so strcmp()/memcmp()
# reference __aeabi_unwind_cpp_pr0, which only libgcc_eh provides.  Wrap the
# tail libraries in a group so that reference resolves.
ARM_TAIL_LIBS := -lm -Wl,--start-group,-lgcc_eh,-lc,--end-group

QEMU ?= qemu-arm

.PHONY: all check arm run-arm clean

all: build/test_shim_tables build/test_table_bundle

check: build/test_shim_tables build/test_table_bundle
	./build/test_shim_tables
	./build/test_table_bundle

build build_arm:
	mkdir -p $@

build/freeisp_shim_tables.o: freeisp_shim_tables.c freeisp_shim_tables.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/rmm_tables.o: $(FW_SRC) | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_shim_tables.o: test_shim_tables.c freeisp_shim_tables.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_table_bundle.o: test_table_bundle.c freeisp_shim_tables.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_shim_tables: build/freeisp_shim_tables.o build/rmm_tables.o build/test_shim_tables.o
	$(CC) $^ -o $@ -lm

build/test_table_bundle: build/freeisp_shim_tables.o build/rmm_tables.o build/test_table_bundle.o
	$(CC) $^ -o $@ -lm

build_arm/freeisp_shim_tables.o: freeisp_shim_tables.c freeisp_shim_tables.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/rmm_tables.o: $(FW_SRC) | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_shim_tables.o: test_shim_tables.c freeisp_shim_tables.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_table_bundle.o: test_table_bundle.c freeisp_shim_tables.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_shim_tables.arm: build_arm/freeisp_shim_tables.o build_arm/rmm_tables.o build_arm/test_shim_tables.o
	$(ARM_CC) $(ARM_CFLAGS) $^ -o $@ $(ARM_LDFLAGS) $(ARM_TAIL_LIBS)

build_arm/test_table_bundle.arm: build_arm/freeisp_shim_tables.o build_arm/rmm_tables.o build_arm/test_table_bundle.o
	$(ARM_CC) $(ARM_CFLAGS) $^ -o $@ $(ARM_LDFLAGS) $(ARM_TAIL_LIBS)

arm: build_arm/test_shim_tables.arm build_arm/test_table_bundle.arm

run-arm: arm
	cd build_arm && $(QEMU) test_shim_tables.arm
	cd build_arm && $(QEMU) test_table_bundle.arm

clean:
	rm -rf build build_arm
