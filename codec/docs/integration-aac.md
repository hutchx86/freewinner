<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Integration proposal — drop `-laacenc` from `media_daemon`

Target project: **yi-mediad**
File: `yi-mediad/work/media_daemon/Makefile`
Status: proposal only — **not applied** (freewinner never edits yi-mediad).

## Why

`mediad_rtos_v` links the Allwinner AAC blob `libaacenc.a` (`Makefile:315`,
`AUDIO_CODEC_LIBS := -laacenc`). The codec subproject provides a drop-in replacement
(`AudioAACENCEncInit/Exit` + the `EncInit/EncFrame/EncExit` contract over
upstream FAAC, LGPL-2.1+). The private link harness already proves the consumer
links without `-laacenc` and that no vendor AAC implementation symbol survives.

## Change

Add near the other prebuilt-lib variables (`Makefile` ~289-315):

```make
# Our AAC-LC replacement (freewinner codec) substitutes the vendor
# libaacenc.a. Build it once with:
#   make -C <codec> arm-aac TC=<toolchain prefix> FAAC_DIR=<faac tree>
# FREECODEC_DIR is the codec checkout; it has no built-in default,
# so pass it on the make command line or in the environment.
# FREECODEC_AAC is a full path, so no -L is needed for it.
FREECODEC_DIR ?= /path/to/codec
FREECODEC_AAC ?= $(FREECODEC_DIR)/build_arm/libfreecodec_aac.a
```

Replace line 315:

```make
# before
AUDIO_CODEC_LIBS := -laacenc
# after
AUDIO_CODEC_LIBS := $(FREECODEC_AAC)
```

and make the daemon depend on it:

```make
$(TARGET): $(OBJS) $(RMM_LAYOUT_H) $(FREECODEC_AAC)
```

with a self-building rule:

```make
$(FREECODEC_AAC):
	$(MAKE) -C $(FREECODEC_DIR) arm-aac \
	    TC=$(TC)/bin/arm-openwrt-linux-muslgnueabi \
	    FAAC_DIR=$(FREECODEC_DIR)/third_party/faac
```

`$(AUDIO_CODEC_LIBDIR)` in `LDFLAGS` becomes unused for AAC and can be left (it
is harmless) or removed.

## Notes / caveats

- `FREECODEC_DIR` has no built-in default; point it at the codec subproject
  checkout explicitly. (A submodule under the consumer's `repos/`
  would be the tidier long-term layout and avoids the relative path.)
- FAAC is a separate LGPL-2.1+ library. Distributing a binary that links it
  requires conveying FAAC's source/licence; see `codec/CREDITS.md`.
- The two ABI entry points keep the vendor names by necessity (they are the
  consumer's import surface); everything else is gone.
