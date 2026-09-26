# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# Encoder support library (libvenc_base replacement): every src/base/*.c, as a
# static archive (host tests) and a shared library exporting exactly the
# functions of include/freecodec/venc_base_abi.h. GetVeOpsS stays undefined in
# the .so (resolved from the loading executable).
#
# ARM build (use a separate BUILD so host objects are not reused):
#   make CC=arm-openwrt-linux-muslgnueabi-gcc BUILD=build/arm venc-base

VENC_BASE_CFLAGS := -pthread -Isrc/base

VENC_BASE_SRCS     := $(wildcard src/base/*.c)
VENC_BASE_OBJS     := $(patsubst src/base/%.c,$(BUILD)/base/%.o,$(VENC_BASE_SRCS))
VENC_BASE_PIC_OBJS := $(patsubst src/base/%.c,$(BUILD)/base/pic/%.o,$(VENC_BASE_SRCS))
VENC_BASE_HDRS     := $(wildcard src/base/*.h) include/freecodec/venc_base_abi.h
VENC_BASE_MAP      := $(BUILD)/base/libvenc_base.map
VENC_BASE_STUB     := $(BUILD)/base/test/ve_ops_stub.o

$(BUILD)/base/%.o: src/base/%.c $(VENC_BASE_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(VENC_BASE_CFLAGS) -c $< -o $@

$(BUILD)/base/pic/%.o: src/base/%.c $(VENC_BASE_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(VENC_BASE_CFLAGS) -fPIC -c $< -o $@

$(BUILD)/libvenc_base.a: $(VENC_BASE_OBJS) | $(BUILD)
	rm -f $@
	$(AR) rcs $@ $^

# Export list = every function declared after the "exported functions" marker
# of the ABI header; everything else is local.
$(VENC_BASE_MAP): include/freecodec/venc_base_abi.h | $(BUILD)
	@mkdir -p $(dir $@)
	awk '/exported functions/ { on = 1 } on' $< \
	  | grep -oE '[A-Za-z_][A-Za-z0-9_]*\(' | tr -d '(' | sort -u \
	  | awk 'BEGIN { print "{"; print "  global:" } { print "    " $$0 ";" } \
	         END { print "  local:"; print "    *;"; print "};" }' > $@

# -nostartfiles: the library has no C++/ctor needs, and musl's crti would
# otherwise export _init/_fini; -static-libgcc keeps libgcc_s out of NEEDED
# (SPEC §6: libc and libpthread only). Constructors, if ever added, still run
# through .init_array.
VENC_BASE_SO_LDFLAGS := -shared -pthread -nostartfiles -static-libgcc \
    -Wl,-soname,libvenc_base.so -Wl,--version-script=$(VENC_BASE_MAP)

$(BUILD)/libvenc_base.so: $(VENC_BASE_PIC_OBJS) $(VENC_BASE_MAP) mk/venc_base.mk | $(BUILD)
	$(CC) $(CFLAGS) -fPIC $(VENC_BASE_PIC_OBJS) $(VENC_BASE_SO_LDFLAGS) -o $@

$(VENC_BASE_STUB): tests/base/ve_ops_stub.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# One host test per tests/base/test_*.c, linked with all base objects.
VENC_BASE_TEST_SRCS := $(wildcard tests/base/test_*.c)
VENC_BASE_TEST      := $(patsubst tests/base/%.c,$(BUILD)/base/%,$(VENC_BASE_TEST_SRCS))

$(BUILD)/base/test_%: tests/base/test_%.c $(VENC_BASE_OBJS) $(VENC_BASE_STUB) $(VENC_BASE_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(VENC_BASE_CFLAGS) -Itests/base $< $(VENC_BASE_OBJS) \
	    $(VENC_BASE_STUB) $(LDFLAGS) -o $@

.PHONY: venc-base
venc-base: $(BUILD)/libvenc_base.a $(BUILD)/libvenc_base.so

all: venc-base
