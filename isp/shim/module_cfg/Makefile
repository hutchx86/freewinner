# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# module_cfg SDK-ABI shim build. Targets: all (host test), check, arm, run-arm,
# clean. The clean core is built with isp_hardware_update/isp_map_addr renamed
# to clean_* so the shim can export the framework-facing spellings.

CC       ?= gcc
CLEAN_DIR := $(abspath $(CURDIR)/../..)
CLEAN_INC := $(CLEAN_DIR)/include
# The shims compile against the generated fwi_* ABI headers in include/.
SDK_INC  ?= $(CLEAN_INC)
SDK_CFLAGS := -I$(SDK_INC)

CLEAN_DEFS := -Disp_hardware_update=clean_isp_hardware_update \
              -Disp_map_addr=clean_isp_map_addr

HOST_CFLAGS := -std=gnu99 -O2 -Wall -Wextra -fwrapv -fshort-enums \
               -I$(CLEAN_INC) $(SDK_CFLAGS)

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

all: build/test_module_cfg_shim

check: build/test_module_cfg_shim
	./build/test_module_cfg_shim

build build_arm:
	mkdir -p $@

build/module_cfg_clean.o: $(CLEAN_DIR)/src/reg/module_cfg.c | build
	$(CC) $(CLEAN_DEFS) $(HOST_CFLAGS) -c $< -o $@

build/reg_writers_clean.o: $(CLEAN_DIR)/src/reg/reg_writers.c | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/module_cfg_shim.o: module_cfg_shim.c module_cfg_shim.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_module_cfg_shim.o: test_module_cfg_shim.c module_cfg_shim.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_module_cfg_shim: build/module_cfg_clean.o build/reg_writers_clean.o \
                            build/module_cfg_shim.o build/test_module_cfg_shim.o
	$(CC) $^ -o $@ -lm

build_arm/module_cfg_clean.o: $(CLEAN_DIR)/src/reg/module_cfg.c | build_arm
	$(ARM_CC) $(CLEAN_DEFS) $(ARM_CFLAGS) -c $< -o $@

build_arm/reg_writers_clean.o: $(CLEAN_DIR)/src/reg/reg_writers.c | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/module_cfg_shim.o: module_cfg_shim.c module_cfg_shim.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_module_cfg_shim.o: test_module_cfg_shim.c module_cfg_shim.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_module_cfg_shim.arm: build_arm/module_cfg_clean.o \
                                    build_arm/reg_writers_clean.o \
                                    build_arm/module_cfg_shim.o \
                                    build_arm/test_module_cfg_shim.o
	$(ARM_CC) $(ARM_CFLAGS) $^ -o $@ $(ARM_LDFLAGS) -lm

arm: build_arm/test_module_cfg_shim.arm

run-arm: build_arm/test_module_cfg_shim.arm
	cd build_arm && $(QEMU) test_module_cfg_shim.arm

clean:
	rm -rf build build_arm
