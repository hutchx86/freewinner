# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# isp_base SDK-ABI shim build. Targets: all (host test), check, arm, run-arm,
# clean. The clean core is built with its twelve entry points renamed to
# clean_* so the shim can export the framework-facing spellings.

CC       ?= gcc
CLEAN_DIR := $(abspath $(CURDIR)/../..)
CLEAN_INC := $(CLEAN_DIR)/include
# The shims compile against the generated fwi_* ABI headers in include/.
SDK_INC  ?= $(CLEAN_INC)
SDK_CFLAGS := -I$(SDK_INC)

CLEAN_DEFS := \
  -Disp_handle_stats=clean_isp_handle_stats \
  -Disp_handle_stats_sync=clean_isp_handle_stats_sync \
  -Disp_apply_settings=clean_isp_apply_settings \
  -D__isp_stat_dynamic_judge=clean___isp_stat_dynamic_judge \
  -Disp_apply_colormatrix=clean_isp_apply_colormatrix \
  -Dconfig_band_step=clean_config_band_step \
  -Dconfig_dig_gain=clean_config_dig_gain \
  -Dconfig_gamma=clean_config_gamma \
  -Dconfig_lens_center=clean_config_lens_center \
  -Dconfig_lens_table=clean_config_lens_table \
  -Dconfig_msc_table=clean_config_msc_table \
  -Dconfig_wdr=clean_config_wdr

HOST_CFLAGS := -std=gnu99 -O2 -Wall -Wextra -fwrapv -fshort-enums \
               -I$(CLEAN_INC) $(SDK_CFLAGS)

CROSS      ?=
ARM_CC     = $(if $(CROSS),$(CROSS)gcc,$(error CROSS is not set - pass CROSS=<cross-toolchain prefix>))
ARM_CFLAGS := -march=armv7ve -mfloat-abi=hard -fshort-enums \
              -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 \
              -O2 -std=gnu99 -Wall -Wextra -fwrapv -I$(CLEAN_INC) \
              $(SDK_CFLAGS)
ARM_LDFLAGS := -static

QEMU ?= qemu-arm

.PHONY: all check arm run-arm clean

all: build/test_base_shim

check: build/test_base_shim
	./build/test_base_shim

build build_arm:
	mkdir -p $@

build/base_clean.o: $(CLEAN_DIR)/src/reg/base.c | build
	$(CC) $(CLEAN_DEFS) $(HOST_CFLAGS) -c $< -o $@

build/reg_writers_clean.o: $(CLEAN_DIR)/src/reg/reg_writers.c | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/comp_ref_clean.o: $(CLEAN_DIR)/src/reg/comp_ref.c | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/base_shim.o: base_shim.c base_shim.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_base_shim.o: test_base_shim.c base_shim.h | build
	$(CC) $(HOST_CFLAGS) -c $< -o $@

build/test_base_shim: build/base_clean.o build/reg_writers_clean.o \
                      build/comp_ref_clean.o build/base_shim.o \
                      build/test_base_shim.o
	$(CC) $^ -o $@ -lm

build_arm/base_clean.o: $(CLEAN_DIR)/src/reg/base.c | build_arm
	$(ARM_CC) $(CLEAN_DEFS) $(ARM_CFLAGS) -c $< -o $@

build_arm/reg_writers_clean.o: $(CLEAN_DIR)/src/reg/reg_writers.c | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/comp_ref_clean.o: $(CLEAN_DIR)/src/reg/comp_ref.c | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/base_shim.o: base_shim.c base_shim.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_base_shim.o: test_base_shim.c base_shim.h | build_arm
	$(ARM_CC) $(ARM_CFLAGS) -c $< -o $@

build_arm/test_base_shim.arm: build_arm/base_clean.o build_arm/reg_writers_clean.o \
                              build_arm/comp_ref_clean.o build_arm/base_shim.o \
                              build_arm/test_base_shim.o
	$(ARM_CC) $(ARM_CFLAGS) $^ -o $@ $(ARM_LDFLAGS) -lm

arm: build_arm/test_base_shim.arm

run-arm: build_arm/test_base_shim.arm
	cd build_arm && $(QEMU) test_base_shim.arm

clean:
	rm -rf build build_arm
