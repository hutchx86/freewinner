# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# AFS shim build.
#
#   make          host test  -> build/test_afs_shim
#   make check    build + run the host test
#   make arm      static ARM build -> build_arm/test_afs_shim.arm
#   make run-arm  run the ARM build under the extracted qemu-arm
#   make clean
#
# The clean core is compiled separately with its entry points renamed so that
# afs_init/afs_exit can coexist with the SDK names exported by the shim.

CC       ?= gcc
CLEAN_DIR := $(abspath $(CURDIR)/../..)
CLEAN_INC := $(CLEAN_DIR)/include
# The shims compile against the generated fwi_* ABI headers in include/.
SDK_INC  ?= $(CLEAN_INC)
SDK_CFLAGS := -I$(SDK_INC)
CLEAN_SRC := $(CLEAN_DIR)/src/afs/afs_clean.c

CLEAN_DEFS := -Dafs_clean_init=clean_afs_init \
              -Dafs_clean_exit=clean_afs_exit \
              -Dafs_clean_get_params=clean_afs_get_params \
              -Dafs_clean_set_params=clean_afs_set_params \
              -Dafs_clean_run=clean_afs_run \
              -Dafs_clean_isr=clean_afs_isr

HOST_CFLAGS := -std=gnu99 -O2 -Wall -Wextra -fwrapv -I$(CLEAN_INC) \
               $(SDK_CFLAGS)

# Set CROSS to the cross-toolchain prefix for the `arm` targets, e.g.
#   .../toolchain-sunxi-musl/toolchain/bin/arm-openwrt-linux-muslgnueabi-
CROSS      ?=
ARM_CC     = $(if $(CROSS),$(CROSS)gcc,$(error CROSS is not set - pass CROSS=<cross-toolchain prefix>))
ARM_CFLAGS := -march=armv7ve -mfloat-abi=hard -fshort-enums \
              -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 \
              -O2 -std=gnu99 -Wall -Wextra -fwrapv -I$(CLEAN_INC) \
              $(SDK_CFLAGS)
ARM_LDFLAGS := -static

QEMU ?= qemu-arm

.PHONY: all check arm run-arm clean

all: build/test_afs_shim

check: build/test_afs_shim
	./build/test_afs_shim

build build_arm:
	mkdir -p $@

build/afs_clean.o: $(CLEAN_SRC) $(CLEAN_INC)/afs_clean.h | build
	$(CC) $(CLEAN_DEFS) -std=c99 -O2 -Wall -Wextra -fwrapv -I$(CLEAN_INC) -c $< -o $@

build/afs_shim.o: afs_shim.c afs_shim.h $(CLEAN_INC)/afs_clean.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_afs_shim.o: test_afs_shim.c afs_shim.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_afs_shim: build/afs_clean.o build/afs_shim.o build/test_afs_shim.o
	$(CC) $^ -o $@ -lm

build_arm/afs_clean.o: $(CLEAN_SRC) $(CLEAN_INC)/afs_clean.h | build_arm
	$(ARM_CC) $(CLEAN_DEFS) $(ARM_CFLAGS) -std=c99 -c $< -o $@

build_arm/afs_shim.o: afs_shim.c afs_shim.h $(CLEAN_INC)/afs_clean.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_afs_shim.o: test_afs_shim.c afs_shim.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_afs_shim.arm: build_arm/afs_clean.o build_arm/afs_shim.o build_arm/test_afs_shim.o
	$(ARM_CC) $(ARM_CFLAGS) $^ -o $@ $(ARM_LDFLAGS) -lm

arm: build_arm/test_afs_shim.arm

run-arm: build_arm/test_afs_shim.arm
	cd build_arm && $(QEMU) test_afs_shim.arm

clean:
	rm -rf build build_arm
