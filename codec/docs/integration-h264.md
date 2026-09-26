<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Integration proposal — drop `-lvenc_codec -lVE` from `media_daemon`

Target project: **yi-mediad**
File: `yi-mediad/work/media_daemon/Makefile`
Status: proposal only — **not applied** (freewinner never edits yi-mediad).

## Why

`mediad_rtos_v` links the Allwinner H.264 hardware-encoder blobs
`libvenc_codec.a` + `libVE.a` (`Makefile:521`, `CEDARC_LIBS := -lvenc_codec -lVE`).
The codec subproject provides a drop-in replacement: the `VENC_DEVICE` ver2 table
(`video_encoder_h264_ver2`), the VE driver (`GetVeOpsS`), the encoder-internal
ISP, the rate-control unit and the header writer — ten clean objects built from
`src/h264/`, with `src/base/` supplying the support library. The private
link harness already proves the consumer links without either vendor
archive and that no vendor codec implementation symbol survives, with
`GetVeOpsS` / `video_encoder_h264_ver2` present.

## Change

Add near the other prebuilt-lib variables (`Makefile` ~470-524):

```make
# Our H.264 hardware-encoder replacement (freewinner codec)
# substitutes the vendor libvenc_codec.a + libVE.a. Built out-of-tree with the
# same cross toolchain; point FREECODEC_DIR at the codec checkout
# (it has no built-in default, since the two trees are independent).
#
# ABI: the H.264 unit objects must keep the 4-byte-enum ABI of the prebuilt
# VENC/cedarc objects — do NOT add -fshort-enums to FREECODEC_H264_CFLAGS.
FREECODEC_DIR ?= /path/to/codec
FREECODEC_H264_DIR := $(FREECODEC_DIR)/src/h264
FREECODEC_H264_SRCS := \
    $(FREECODEC_H264_DIR)/regs/h264_regs.c \
    $(FREECODEC_H264_DIR)/headers/h264_headers.c \
    $(FREECODEC_H264_DIR)/isp/h264_isp.c \
    $(FREECODEC_H264_DIR)/gop/h264_gop.c \
    $(FREECODEC_H264_DIR)/rc/h264_rc.c \
    $(FREECODEC_H264_DIR)/enc/freecodec_enc.c \
    $(FREECODEC_H264_DIR)/enc/freecodec_enc_regs.c \
    $(FREECODEC_H264_DIR)/ve/ve_driver.c
# rc/h264_rc.c (spec 10) and enc/freecodec_enc.c (spec 12) are the clean-room
# r4 re-productions (r3's versions of both were abandoned as defective and
# replaced from scratch); both use pthread (the device's process lock).
FREECODEC_H264_OBJS := \
    $(patsubst $(FREECODEC_H264_DIR)/%.c,$(BUILD)/fc_h264/%.o,$(FREECODEC_H264_SRCS))
FREECODEC_H264_CFLAGS := -march=armv7ve -mfloat-abi=hard -fPIC -O2 \
    -std=gnu99 -Wall -pthread -I$(FREECODEC_DIR)/include

$(BUILD)/fc_h264/%.o: $(FREECODEC_H264_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FREECODEC_H264_CFLAGS) -c $< -o $@
```

Replace line 521:

```make
# before
CEDARC_LIBS := -lvenc_codec -lVE
# after
CEDARC_LIBS := $(FREECODEC_H264_OBJS)
```

and make the link depend on the objects (line 564):

```make
$(TARGET): $(OBJS) $(RMM_LAYOUT_H) $(FREECODEC_H264_OBJS)
```

`$(CEDARC_LIBDIR)` in `LDFLAGS` becomes unused for the codec and can be left
(harmless) or removed. `-lvenc_codec`/`-lVE` provided `GetVeOpsS`; ours is in
`ve_driver.o` with the identical symbol name and ABI, so the consumer's
`GetVeOpsS(NORMAL)` call is satisfied.

## Notes / caveats

- `FREECODEC_DIR` has no built-in default; point it at the codec subproject
  checkout (for example on the `make` command line or in the environment).
- Include paths: the seven sources need only `-I$(FREECODEC_DIR)/include`
  (`freecodec/vencoder.h`, which pulls in `freecodec/ve_iface.h` and
  `freecodec/venc_base_abi.h`); no cedarc root is needed. The consumer's
  existing `INC` may still carry them for other translation units.
- Every source includes only the clean `freecodec/*` headers; none sees the
  SDK `veInterface.h`/`vencoder.h`/`venc_device.h`/`EncAdapter.h`/
  `BitstreamManager.h` any more.
- `ve_driver.o` uses `pthread`; the consumer already links `-lpthread`.
- The encoder-internal ISP (`h264_isp.o`) is required: the consumer's
  per-frame encode entry runs it unconditionally (`spec/h264-output-path.md` §5).
- ALSA and the AAC replacement are unaffected; this is the H.264 half only.
- Device acceptance (both channels, ring decodes) is tracked in the parent
  project; the link criterion is met by the private link harness.
