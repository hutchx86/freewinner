<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# VE H.264 encoder — legal sources, prior art, and same-block SoCs

Status: research output, 2026-09-22. Question asked: for the V831
(`sun8iw19p1`) H.264 encoder — the AVC block at `0xB00` and the ISP-in-VE block
at `0xA00` inside the VETOP window of `/dev/cedar_dev` — (a) is there anything we
may legally use, (b) what documents *writing* one, (c) which other SoCs share the
block so their code transfers.

This is a bibliography/evidence document. It changes no code. See
`docs/provenance.md` for what is actually compiled in.

## 1. The register *map* is family-wide; the *bitfields* and the *driver* are not

The `0xB00` AVC encoder map (PICINFO 0x00, PARA0 0x04, PARA1 0x08, PARA2 0x0C,
MEPARA 0x10, INT_ENABLE 0x14, STARTTRIG 0x18, STATUS 0x1C, PUTBITSDATA 0x20,
RC_INIT 0x2C, RC_MAD_TH0–3 0x30–0x3C, MVBUFADDR 0x60, STM* 0x80–0x90,
REF/REC/SUBPIX/MBINFO 0xA0–0xC4) is the same across the A10…V3s…V831 family —
it is what Allwinner's own published header, the linux-sunxi register guide,
jemk/cedrus's `ve.h`, libv's `cedar_regs.h` and Bootlin's `cedrus_regs.h` all
describe. What differs:

- **Bitfield layout inside the words** (`PARA1` chiefly): a version-0 vs
  version-1 split at `ic_version == 0x1667` (neutral basis: the register-shadow
  golden, spec `h264-reg-shadow.md`, and public linux-sunxi/Bootlin material).
  **V831 uses version-1.**
- **Which H.264 engine class applies**: a split at `ic_version >= 0x1708`,
  above which the consumer requests the version-2 H.264 codec kind
  (spec `h264-enc-flow.md`, `00-vendor-interface-surface.md`).
  **V831 is in the ≥0x1708 ("ver2") class**; H3 is not.

The `VE_CTRL`/`VE_MODE` word (`0x00`) enables the encoder with **bits 7 and 6**
("Enable AVC encoder (1633 and newer)", "ISP enable") — Bootlin's
`VE_MODE_ENC_ENABLE BIT(7)` / `VE_MODE_ENC_ISP_ENABLE BIT(6)`, matching our
`VETOP[0]` bits `0x80|0x40`.

| SoC | VE version | Same `0xB00` block? |
|---|---|---|
| A10/A13/A10s/A20 | 0x1623 / 0x1625 | yes (map); `PARA1` v0 |
| A31/A31s | 0x1633 | yes; encoder-enable bit introduced |
| A23 | 0x1650 | yes |
| A33/R16 | 0x1667 | yes; v0/v1 bitfield boundary |
| A80 | 0x1639 | yes (special-cased; public linux-sunxi VE version table) |
| A83T | 0x1673 (SoC id) | yes |
| H3/H2+ | 0x1680 | yes (map); `PARA1` v1; ver1 driver |
| **V3/V3s/S3** | **0x1681** | **yes — best-attested (Bootlin)** |
| A64/H64 | 0x1689 | likely yes |
| R40/V40/T3/A40i | 0x1701 (SoC id) | unknown (below 0x1708) |
| H5 | 0x1718 | unknown; ≥0x1708 → ver2 class |
| H6 | 0x1728 (SoC id) | unknown |
| H616/H618/T507 | unknown | likely different (not RE'd publicly) |
| V536/V316 | not captured | **same code-level** (identical `libcedarc`, `0xB00`, ver2) |
| **V831/V833/V533** | **0x211x** | **reference** |
| V853/V851 | unknown | unknown |
| T527/A523 | unknown | unknown |
| D1/F133 | n/a | decode only in mainline |

Evidence: linux-sunxi `VE_Register_guide` (VE version table, AVC base
`0x01c0eb00`, engine `0xb`); mainline `cedrus.c` `cedrus_dt_match[]` and
`allwinner,sun4i-a10-video-engine.yaml` (11 compatibles, **decode only**);
Bootlin branch `cedrus/h264-encoding` `cedrus_regs.h`
(`VE_ENGINE_ENC_ISP_BASE 0xa00`, `VE_ENGINE_ENC_H264_BASE 0xb00`).

**Mainline has no H.264 encoder.** `drivers/staging/media/sunxi/cedrus/`
defines only `CEDRUS_CAPABILITY_*_DEC`. Bootlin's V3/V3s/S3 encoder is an
out-of-tree branch, blocked on a stateless-encoder uAPI. Community (Sep 2025,
Armbian forum, "sasa") tested T113/A40 via the stateful uAPI and notes
H616/T507 encoders are "updated and not yet reverse-engineered".

## 2. Licensing — what may be incorporated vs reference-only

Rule applied: AGPL-3.0-only can absorb permissive terms (MIT) and any
`-or-later` license that reaches GPLv3/LGPLv3 (AGPLv3 §13). It **cannot** absorb
`GPL-2.0-only`, LGPL-2.1-*only*, or proprietary/no-grant material.

| Source | License | Into AGPL-3.0-only? |
|---|---|---|
| jemk/cedrus `h264enc/*` | GPL-2.0-**or-later** | **yes** |
| jemk/cedrus `common/ve.c`,`ve.h` | LGPL-2.1-**or-later** | **yes** |
| libv `userspace/h264enc.c`,`thumb.c` | GPL-2.0-**or-later** | **yes** |
| libv `kernel/cedar.c` | `SPDX GPL-2.0` (only) | no — reference |
| uboborov `h264_encoder_H3`, `sunxi-cedar-mainline` `cedar_ve.c` | GPL-2.0-**or-later** | yes (their bundled Google `ion.h` = GPL-2.0-only: no) |
| Bootlin `cedrus/h264-encoding` (`cedrus_regs.h`, `cedrus_enc_h264.c`) | `SPDX GPL-2.0` | no — reference |
| mainline cedrus (decode) | mixed `GPL-2.0` / `-or-later` | no (GPL-2.0 files dominate) |
| matheusants `sunxi-venc` | `SPDX GPL-2.0` | no — reference |
| hataketsu `h3-cedar-h264-encode` | GPL-2.0, ambiguous only/later | no — reference |
| Allwinner-authored encoder sources and headers (various public mirrors) | proprietary / no grant | **no** — not used |
| zebin-wu / mashiqi own code | MIT | yes (author code only) |
| linux-sunxi wiki (VE_Register_guide etc.) | CC BY 3.0 | yes (text/tables, with attribution) |

Redistribution hazard independent of license: several of these repos bundle
prebuilt Allwinner `.so`s (`libVE.so`, `libvencoder.so`, `libcdc_vencoder.so`,
`libvenc_codec.so`) — proprietary binaries even where sibling *sources* are
LGPL.

**Net:** the only encoder sources we could *legally fold into this tree* are the
GPL-2.0-or-later / LGPL-2.1-or-later ones (jemk/cedrus `ve.*` + `h264enc`,
libv `userspace/`) and MIT author code. Everything Allwinner-authored, and the
`GPL-2.0`-only kernel/Bootlin work, is **read-only reference**.

## 3. Documentation on writing an encoder

| Doc | Covers | License |
|---|---|---|
| linux-sunxi `VE_Register_guide` | full register map incl. the AVC engine, field-level semantics (`MACC_AVC_H264_CTRL` EPTB/entropy/slice-type, `MACC_AVC_TRIG` function+nbits, `MACC_AVC_STATUS`, VLE addressing) | CC BY 3.0 |
| Bootlin `cedrus_enc_h264.c` + `cedrus_regs.h` | the concrete register-programming sequence (header packing into PUTBITSDATA, launch/VLE) for this exact block (V3/V3s/S3) | GPL-2.0 |
| jemk `h264enc`; danielkucera `FFmpeg@cedrus264` `ve.c`/`cedar_ve.h` | original register-level userspace programming + the `/dev/cedar_dev` ioctl ABI | GPL-2.0+ / GPL-2.0 |
| linux-sunxi `Cedrus`, `Sunxi-Cedrus`, `VE_Planning`, `CedarX/Reverse_Engineering` | architecture, uAPI rationale, history, buffer constraints (contiguous <256 MiB) | CC BY 3.0 |
| Bootlin blog (2023-11-15), Greg Davill S3 post, danman blog | architecture + build/CMA recipes | © authors |
| carroarmato0 `allwinner-cedar-tools` `docs/cedar-encoder.md` | API/ABI reference for the closed `libvencoder`/`libVE` (H618) | none declared |

**Gap:** no public source documents the *bitstream* side (SPS/PPS/slice packing).
The hardware consumes pre-packed header bits via `PUTBITSDATA`+TRIG and a slice
stream in the VLE buffer; that algorithm exists only in Allwinner's closed
userspace. This project solved it by RE (`spec/h264-*.md`); nothing public
replaces that.

## 4. Consequences for this project

1. **Cross-check value (highest):** Bootlin's `cedrus_enc_h264.c` is the most
   complete open register-programming sequence for the same block. It is
   GPL-2.0-only, so **reference-only** — diff it against
   `spec/h264enc-codec-library.md` §4 and `spec/middleware-ve-2ch.md`, do not
   copy.
2. **Legal incorporation (if ever wanted):** jemk/cedrus `ve.c`/`ve.h`
   (LGPL-2.1+) and `h264enc` (GPL-2.0+), and libv's `userspace/` (GPL-2.0+),
   could be incorporated under AGPLv3 §13 — unlike the vendor material. No such
   incorporation is proposed here.
3. **Same-block transfer:** V3/V3s/S3 (0x1681), V536/V316, and V831 share the
   block and the `PARA1` v1 bitfields; H3/A20 share the map but differ in
   bitfields; ≥0x1708 selects the "ver2" H.264 engine class (V831's class).
4. **The kernel is not the source of truth:** both V831 and V853 BSP
   `drivers/media/cedar-ve/` are decode-only; the encoder is driven from
   userspace via `/dev/cedar_dev` register read/write ioctls. The register
   sequence has to come from the blob/MPP userspace or be reconstructed — as
   this project did.
