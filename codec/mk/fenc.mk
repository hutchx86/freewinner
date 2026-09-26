# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# Encoder API framework (package C, `fenc_`): the `VideoEnc*` entry points, a
# drop-in for the SDK's vencoder.c. Built as a static archive the mediad link
# consumes (link it in place of the vendor vencoder object); host tests replay
# the SPEC §9 black-box vectors against fake device, VE and adapter seams.
#
# The host test links only the real picture-queue manager from the support
# library (src/base/vb_frames.c) plus the seam; the VE, adapter and device
# tables are scripted, so no kernel or engine is touched.

FENC_CFLAGS := -Isrc/fenc -pthread
FENC_SRCS   := $(wildcard src/fenc/*.c)
FENC_OBJS   := $(patsubst src/fenc/%.c,$(BUILD)/fenc/%.o,$(FENC_SRCS))
FENC_HDRS   := $(wildcard src/fenc/*.h) include/freecodec/venc_base_abi.h \
    include/freecodec/ve_iface.h

$(BUILD)/fenc/%.o: src/fenc/%.c $(FENC_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FENC_CFLAGS) -c $< -o $@

$(BUILD)/libfenc.a: $(FENC_OBJS) | $(BUILD)
	rm -f $@
	$(AR) rcs $@ $^

# One host test per tests/fenc/test_*.c.
FENC_TEST_SRCS := $(wildcard tests/fenc/test_*.c)
FENC_TEST_SEAM := $(BUILD)/fenc/seam.o
FENC_TEST      := $(patsubst tests/fenc/%.c,$(BUILD)/fenc/%,$(FENC_TEST_SRCS))
FENC_TEST_LDFLAGS := -Wl,--wrap=ResetFrameBuffer \
    -Wl,--wrap=FrameBufferManagerDestroy

$(BUILD)/fenc/seam.o: tests/fenc/fenc_seam.c tests/fenc/fenc_seam.h $(FENC_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FENC_CFLAGS) -c $< -o $@

$(BUILD)/fenc/test_%: tests/fenc/test_%.c $(FENC_OBJS) $(FENC_TEST_SEAM) \
		$(BUILD)/base/vb_frames.o $(FENC_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FENC_CFLAGS) $< $(FENC_OBJS) \
	    $(BUILD)/base/vb_frames.o $(FENC_TEST_SEAM) $(FENC_TEST_LDFLAGS) \
	    $(LDFLAGS) -pthread -o $@

.PHONY: fenc
fenc: $(BUILD)/libfenc.a

all: fenc
