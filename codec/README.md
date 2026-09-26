<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# freecodec

An independent, clean implementation of the two remaining Allwinner proprietary
codec blobs linked into the Yi-camera media daemon (`mediad`), for
sun8iw19p1 / V833-class hardware:

- **AAC-LC encoder** — replacement for `libaacenc.a`, built on upstream FAAC
  (LGPL-2.1-or-later).
- **H.264 hardware encoder** — replacement for `libvenc_codec.a` + `libVE.a`,
  a clean-room driver/encoder that talks to `/dev/cedar_dev` directly.

This is the codec subproject of `freewinner`; the sibling `isp/` subproject
holds the clean-room libisp 3A replacement. The consumer of both is the Yi
camera's `media_daemon`.

Licence: **AGPL-3.0-only** (see `LICENSE`). FAAC is LGPL-2.1-or-later and is kept
as a separate library; see `CREDITS.md`.

## Status

| Unit | State |
|---|---|
| Vendor interface surface (both codecs) | documented — `spec/00-vendor-interface-surface.md` |
| AAC-LC wrapper (`libaacenc` replacement) | implemented, host-tested, decodes, link-accepted |
| `ve` driver (`libVE` replacement) | implemented, mock-port tested, ABI-verified vs SDK |
| H.264 header generation (SPS/PPS/IDR/P) | implemented; golden-verified against goldens withheld from this repository (see Build and test) |
| H.264 rate control (CBR) | implemented; golden-verified against goldens withheld from this repository (see Build and test) |
| H.264 register layer (shadow, slice regs, `freecodec_h264_config_registers`) | implemented; golden-verified against goldens withheld from this repository (see Build and test) |
| H.264 `fwm_venc_device_t` assembly (`enc`) | implemented over the units; encoder-internal ISP implemented, only advanced ISP features + advanced GOP stubbed (`src/h264/enc/README.md`) |
| Link substitution (no vendor codec blobs) | **passes** — link acceptance in the private analysis workspace |
| On-device validation | H.264 **validated on the y623**: the clean encoder emits a single contiguous SPS/PPS/IDR/P stream for both channels and ffmpeg decodes both extracted streams with zero macroblock errors (2304x1296 high + 640x360 low, Main L5.1). The 2-channel encoder-interrupt wait stall is root-caused and **fixed** (per-frame VE reset pulse + frame offset 0, the vendor's own envelope — see `spec/h264-two-channel.md`). AAC host-decode + link verified; device pending. Remaining: a non-CBR rate control that produces very large P frames, the `0x1c` status write-back / `0x1c & 2` re-encode path, and the resulting fshare-ring churn. See `src/h264/enc/README.md`. |

`make check` runs the SDK-free host units (aac, ve, headers, regs, rc, h264_isp,
h264_gop). The `enc` unit and the link acceptance need the consumer SDK + cross
toolchain, so they run in the private analysis workspace rather than from this
repo.

## Layout

- behaviour specs — kept in the private clean-room workspace, not in this
  repository; source comments cite them as `spec/<name>.md`.
- `src/aac/` — the AAC encoder wrapper.
- `src/h264/` — the H.264 driver/encoder (in progress).
- `include/freecodec/` — interoperability headers (the ABI we implement).
- `tests/` — behavioural tests.
- `mk/` — per-module build fragments; `Makefile` wires them together.
- the private analysis workspace (never published) holds the differential
  harnesses, the withheld golden vectors and link acceptance.

## Build and test (host)

```
make check
```

The AAC build needs a FAAC source tree. Point `FAAC_DIR` at one (the upstream
tree, or a sibling checkout); a host `faac` also works for the differential:

```
make check FAAC_DIR=/path/to/faac
```

The differential suites compare our output against recorded golden vectors,
which are withheld from this repository. Without them those suites report
`SKIP`. To run them, put the vectors in `$(GOLDEN_DIR)` (default
`$(CURDIR)/golden`) and ask for them explicitly:

```
make check ALLOW_GOLDENS=1
```

`GOLDEN_DIR` is compiled into each test binary, so run `make clean` after
changing it.

## Provenance

Written by reverse engineering the *deployed* on-camera binaries for
interoperability with hardware we own; no Allwinner source is included, and the
implementation is verified against the deployed objects. See `CREDITS.md`. The
compiled-in tables and literals, and how each was obtained, are inventoried in
`docs/provenance.md`.
