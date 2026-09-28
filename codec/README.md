<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# freecodec

An independent, clean implementation of the Allwinner proprietary codec
libraries linked into the Yi-camera media daemon (`mediad`), for
sun8iw19p1 / V833-class hardware:

- **AAC-LC encoder** — replacement for `libaacenc.a`, built on upstream FAAC
  (LGPL-2.1-or-later).
- **H.264 hardware encoder** — replacement for `libvenc_codec.a` + `libVE.a`,
  a clean-room driver/encoder that talks to `/dev/cedar_dev` directly.
- **Encoder support library and API framework** — replacements for
  `libvenc_base` and the `VideoEnc*` entry points the daemon calls.

This is the codec subproject of `freewinner`; the sibling `isp/` subproject
holds the clean-room libisp 3A replacement. The consumer of both is the
camera's media daemon (`mediad`).

Licence: **AGPL-3.0-only** (see `LICENSE`). FAAC is LGPL-2.1-or-later and is kept
as a separate library; see `CREDITS.md`.

## Status

| Unit | Source | State |
|---|---|---|
| AAC-LC wrapper (`libaacenc` replacement) | `src/aac/` | implemented, host-tested (needs a FAAC tree), link-accepted |
| VE engine driver (`libVE` replacement, `GetVeOpsS`) | `src/h264/ve/` | implemented, mock-port tested |
| H.264 headers, register layer, rate control, encoder-internal ISP, GOP | `src/h264/{headers,regs,rc,isp,gop}/` | implemented; verified against differential vectors that are not shipped |
| H.264 encoder device (`fwm_venc_device_t`, `libvenc_codec` replacement) | `src/h264/enc/` | implemented; host-tested against a fake engine |
| Encoder support library (`libvenc_base` replacement) | `src/base/` | implemented; static archive + shared library, host-tested |
| Encoder API framework (`VideoEnc*` entry points) | `src/fenc/` | implemented; host-tested; omissions in `src/fenc/NOT-IMPLEMENTED.md` |
| On-device | | the fully vendor-free media daemon streams both H.264 channels (high 2304x1296 and low 640x360) and both decode with zero macroblock errors |

## Layout

- `include/freecodec/` - interoperability headers (the ABI we implement).
- `src/aac/`, `src/h264/`, `src/base/`, `src/fenc/` - the units above.
- `tests/` - behavioural host tests; `tests/h264/data/` holds the one shipped
  data file (see `docs/provenance.md`, "Shipped test data").
- `mk/` - per-module build fragments; `Makefile` wires them together.
- `docs/` - provenance (`docs/provenance.md`), integration notes and prior art.
- The behaviour specs, the differential harnesses and their vectors are kept
  in the private clean-room workspace, not in this repository; source comments
  cite the specs by name and section.

## Build and test (host)

```
make check
```

`make check` builds and runs every host test: the VE driver, overlay packer,
support library, API framework, encoder device, MB-RC and aux-plane buffer
units, and rate control. It needs no SDK and no vendor material.

The AAC unit needs a FAAC source tree and is skipped without one:

```
make check FAAC_DIR=/path/to/faac
```

Tests driven by vectors that are not shipped print `SKIP` and pass without
them. To run them against a private copy:

```
make check RC_VECTOR_DIR=/path/to/vectors      # test_rc
make check MBRC_VECTORS=/path/to/mbrc_table.csv # test_mbrc
make check ORACLE_TESTS=/path/to/tests GOLDEN_DIR=/path/to/golden ALLOW_GOLDENS=1
```

The last form builds the private differential suites (headers, register layer,
encoder ISP, GOP) against recorded golden vectors. Paths are compiled into the
test binaries, so run `make clean` after changing them.

## Provenance

Written by reverse engineering the *deployed* on-camera binaries for
interoperability with hardware we own; no Allwinner source is included, and the
implementation is verified against the deployed objects. See `CREDITS.md`. The
compiled-in tables and literals, and how each was obtained, are inventoried in
`docs/provenance.md`.
