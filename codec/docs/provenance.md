<!-- SPDX-License-Identifier: AGPL-3.0-only -->

Provenance and compiled-in data — freecodec
==========================================

An independently written replacement for the two Allwinner codec blobs the Yi
camera media daemon links — the AAC-LC encoder (`libaacenc.a`) and the H.264
hardware-encoder driver/encoder (`libvenc_codec.a` + `libVE.a`) — for
sun8iw19p1 / V831-class hardware, produced for interoperability with hardware
we own.

Copyright (C) 2026 freewinner contributors.

This is the codec subproject's own provenance record. The sibling `isp/`
subproject's provenance record is scoped to the libisp 3A library and
inventories none of the tables below; it must not be read as covering this
subproject. The reverse-engineering basis for both subprojects is stated in
`CREDITS.md`; the codec's licence and third-party position is in
`codec/CREDITS.md`.

Provenance
----------

The interface and behaviour were recovered by reverse engineering the
*deployed* on-camera binaries (`libaacenc.a`, `libvenc_codec.a`, `libVE.a`,
ARMv7 EABI5). No Allwinner source code is included. Interface facts needed for
interoperability (entry-point names, ABI layouts, register offsets, control
codes, the ops-table slot order) are reproduced; those are facts, not
expression.

All of the codec's own algorithm code is written from the behaviour specs in
`spec/`. The numeric data it still contains is enumerated in full below, with
how each item was obtained. The differential harnesses and their recorded
golden/capture vectors live in the private analysis workspace, are gitignored
(`codec/.gitignore`) and are **not shipped** with this repository: an item
marked **observed** was determined by black-box execution or instrumentation of
the deployed object under that private harness, not by reading Allwinner
source.

Compiled-in tables
------------------

This is every file-scope `const` array/struct and every fixed literal table
compiled into `src/**` and `include/**` (tests and the gitignored private
workspace excluded).

| Compiled-in table | Where | What it is | How obtained / class |
|---|---|---|---|
| `isp_pattern_in_mode[20]` | `src/h264/isp/h264_isp.c:25` | `fwm_venc_pixel_format_e` → the ISP control word's input-layout code `[31:27]`, full 20-entry map: `{0,4,0x18,0x1a,2,6,0x1c,0x1e,1,3,5,7,0x10,0x12,0x14,0x16,0x0a,0x0c,8,0x15}` | **Interface fact** (hardware register field): the mapping and the 20-entry width are reproduced in `spec/13-isp-and-picture-ring.md` §1.1, recovered from the deployed encoder object (2026-09-21). Index 1 (`YVU420SP`, the format the VI delivers) = 4 — omitting it made the ISP read NV21 as NV12 and swap U/V. |
| `group_off[]` | `src/h264/ve/ve_driver.c:378` | VE register-group base offsets `{0x000, 0x100, 0x200, 0x300, 0x400, 0x500, 0xe00}` for the register-group base | **Interface fact** (hardware register layout), matching `fc_ve_group` in `include/freecodec/ve_iface.h:30`. |
| `ENC_SCRIPT_WRITE_OFFSETS[]` (35 offsets) | `src/h264/enc/freecodec_enc.c:53` | The encoder-block registers the per-picture register script writes back (spec 12 §5.5 step 6) | **Derived from our own code**: exactly the offsets our register-configuration step (`freecodec_h264_config_registers`, `src/h264/regs/h264_regs.c`) computes, minus the bit-writer registers `0x18`/`0x20` (driven by the slice-header feed), plus `0x1c`, whose read-modify-write seed comes from the live register. The offsets themselves are encoder register addresses (interface facts). Restricting the write-back was a fix found on our own camera: a whole-window write also zeroed `0x18`. |

Dispatch tables of our own function pointers
--------------------------------------------

These are file-scope structs whose members are our own functions; only the
struct layout, the name strings and the slot order are interface facts.

| Table | Where | Slots |
|---|---|---|
| `g_default_port` | `src/h264/ve/ve_driver.c:132` | The nine libc/pthread port entries (open/close/ioctl/mmap/munmap + four mutex ops). Our injectable seam (`include/freecodec/ve_port.h`); no vendor data. |
| `g_ve_ops` | `src/h264/ve/ve_driver.c:573` | The `fc_ve_ops` implementation. The slot order is the consumer ABI (`include/freecodec/ve_iface.h:117`); the handlers are ours. |

Literal constants that are not tables
-------------------------------------

The remaining compiled literals are format or interface constants, not
recovered data tables:

- **ITU-T H.264 / bitstream constants** in `src/h264/headers/h264_headers.c`:
  the start-code prefix `0x00000001`, NAL headers `0x67`/`0x68`/`0x65`, and the
  `slice_type` I (`7`) / P (`5`) codewords.
- **Hardware/register or IC-version constants**: `FREECODEC_H264_IC_VERSION_A`
  `0x1639` / `_B` `0x1650` (`src/h264/gop/h264_gop.c:12-13`), the ic_version
  thresholds `0x2100f`/`0x2110f`/`0x21110`/`5734` in the `isp` and
  `regs` units, and the VETOP/ISP block bases `0xb00`/`0xa00` (spec 12 §2).
  These are interface facts. The reference additionally gates one path on
  `ic_version == 0x18744`; the clean code carries no such value, recorded as a
  reference-only condition (`spec/h264-idr-divergence.md`).
  A few register field positions/values in `src/h264/regs/h264_regs.c` were
  instead recovered from the withheld golden: the `fast_enc` ME-control bits 4
  and 20 (`:64-65`), `pic_var_chroma 0x1e` / `pic_var_luma 0x0c` (`:96-97`) and
  `maxcoef 0x20` (`:89`). Those few are **observed**; the golden is not
  shipped.
- **MPEG-4 / AAC standard values** in `src/aac/freecodec_aac.c`: the sampling-
  frequency index switch in `fc_sf_index` (lines 30-47; the ISO/IEC 14496-3
  sampling frequencies), the channel map in `fc_chan_config`, and the 7-byte
  no-CRC ADTS header bytes/layout in `fc_adts_header` (`0xFF 0xF1 … 0xFC`,
  profile 1 = AAC-LC; lines 56-69).
- **Interoperability interface facts** in `include/freecodec/`: the
  `/dev/cedar_dev` ioctl codes and register-group enum (`ve_iface.h:55,30`), the
  `fc_ve_ops` slot order (`ve_iface.h:117`), the `freecodec_ve_port`
  seam (`ve_port.h:17`), and the struct/enum layouts in `h264_regs.h`,
  `h264_isp.h`, `h264_headers.h`, `h264_gop.h` and `aac_iface.h`. These
  reproduce the deployed ABI; they contain no source expression.

------------
-----
Observed behaviour kept in code
-------------------------------

Two small rate-control tables are kept in the source as **observed
behaviour**: they were determined from the output of the stock encoder running
on our own camera (the bit budget and QP it chose, and the MB-RC table it left
in engine memory), not from vendor source. Neither is a large data table; both
are small enough to state in full.

- **First-QP ladder** (`first_qp_ladder`, `src/h264/rc/h264_rc.c:43`): the
  initial slice QP chosen from bits per pixel, ten steps from `bpp < 0.01`
  (QP 40) to `bpp >= 2.4` (QP 15). Used only for the first picture of a
  sequence; afterwards our own rate-control model sets the QP.
- **MB-RC tolerance weights** (`weight32[6]` inside `fc_mbrc_build_table`,
  `include/freecodec/h264_bufs.h:54`): `{38, 45, 51, 26, 19, 13}` in 1/32
  units, the three upper and three lower limits (1 +/- 6/32, 13/32, 19/32)
  applied to each row's cumulative share of the picture's activity.

The black-box vectors these were checked against are not shipped (see
"Shipped test data").

Shipped test data
-----------------

Only two data files are in the repository; both are small and their origin is
our own hardware or our own formula:

- **`spec/vectors/regs_observed_ref_high.csv`** (47 rows): encoder and ISP
  register values captured on our own camera while the stock encoder coded the
  high-resolution stream, reduced to the modal value per register for P and I
  pictures with the share of pictures that held it. `tests/h264/test_enc.c`
  (register-parity test) checks that our register unit reproduces the modal
  values of the address-independent registers.
- **`codec/tests/h264/data/subpic_size.csv`** (19 rows): auxiliary
  reference-plane sizes for common picture sizes. Every row is reproduced by
  our own size formula (`fc_aux_plane_bytes`, `include/freecodec/h264_bufs.h`,
  spec r2/03 §2); `tests/h264/test_bufsize.c` checks it.

**Not shipped**: the black-box and emulator vectors used to pin the rate
control and the MB-RC table (the former `spec/vectors/rc_*.csv` and
`codec/tests/h264/data/mbrc_table.csv`). They stay in the private analysis
workspace. The tests that use them print `SKIP` and pass when the files are
absent; point them at a copy with `make check RC_VECTOR_DIR=<dir>` (`test_rc`,
`mk/h264_rc.mk`) and `make check MBRC_VECTORS=<file>` (`test_mbrc`,
`mk/h264.mk`).

Re-derived in code
------------------
------------------

- The ISP bilinear scaler tap pair (`isp_scaler_pair`,
  `src/h264/isp/h264_isp.c:57`) is **re-derived from published maths**: two
  taps summing to `0x100`, packed `(256 - phase) << 16 | phase`. No table is
  compiled in.

OSD overlay: luma-adaptive inversion
------------------------------------

The overlay ("OSD") path (`set_overlay` / `ovl_invert_setup`,
`src/h264/enc/freecodec_enc.c`; header packer in `src/h264/isp/h264_isp.c`)
supports the input-ISP block's per-block luma inversion: the hardware redraws
an overlay block inverted when the background under it would make it
illegible. It was written from a behaviour-only clean-room specification
(private workspace, not shipped; facts tagged FACT/OBS/INFER) written by an
analyst role. The code depends on these facts and values:

- **`FC_OVL_REVERSE_LUMA` = `1u << 29`**: per-block enable bit in header word
  +8. **Interface fact** (hardware header layout).
- **IC-version gate**: `ovl_invert_setup` does nothing when
  `ic_version <= 0x2110f` (no bit 29, no register `0x54`). **Interface fact**:
  the 16-byte header with the reverse bits and the `0x54` invert buffer exist
  only above that version; the same cut-off appears in the ic_version list
  above.
- **Invert scratch buffer** at register `0x54`: one per encoder instance,
  `ceil(input_width / 16) * 32` bytes, 256-byte aligned, zeroed once and never
  touched by software. **Interface fact** (size rule and register).
- **`FC_OVL_INVERT_MODE` = `2`, `FC_OVL_INVERT_THRESH` = `96`**: invert when
  the video/overlay luma difference is below 96. **Observed**: these are the
  values the stock camera application programs (with inversion switched off
  on its own blocks, so stock never actually inverts); we reuse them and
  confirmed them on the device.
- **Reverse-unit nibbles** (header byte +15): passed through from the caller,
  clamped to 0..3 (16..64 px units). The vendor stack never writes them.

`FREECODEC_OVL_INVERT=0` clears bit 29 on every block (the buffer is still
registered). It is this project's own switch, not a vendor control.

Device results (y623, 2026-09-26): with bit 29 set, white text turns black
over bright backgrounds and stays white over dark ones. The decision is made
once per overlay *block* from the background under the whole block; setting
the unit nibbles did not make it finer. Per-letter inversion therefore needs
the caller to emit one block per glyph column, which is what `yi-mediad` does
in its hardware mode. That mode is opt-in (`MEDIAD_OSD_HWINV=1`); mediad's
default picks the text colour in software from AE statistics and sends plain
`NORMAL` blocks, so this path is idle unless enabled.

Release gate and patents: this feature is covered by the same private
publish guard as the rest of the repository (see the release-status note in
`isp/docs/provenance.md`). The royalty position for H.264/AVC and AAC is in
the root `CREDITS.md` and in "Third-party components" below.

Encoder 3D (temporal noise) filter
----------------------------------

The per-picture 3D-filter programming (`src/h264/regs/h264_regs.c`, the
`freecodec_h264_3d_*` helpers; plane handling in `src/h264/enc/`) was written
by an implementation-role agent from a behaviour-only clean-room specification
alone, with no binary access on the implementation side (2026-09-27). The
specification and its separate evidence note were written by an analyst role
and are kept in the private workspace, not shipped; the implementer never saw
the evidence note. The code depends on these facts and values:

- **Register `0x94`**: 9-bit threshold `T` at bits 31:23, written only while
  the filter is enabled, else left 0. `T` is derived from the level and the
  slice QP: `T = level` for QP 0..19 and 31..51, `T = 2 * level` for QP
  20..30; QP >= 52 keeps the previous `T`.
- **Register `0x08` bit 22**: filter enable, set only when the level is
  non-zero and the picture is not the instance's first; it survives the
  slice-header re-store of `0x08`.
- **Registers `0xa8` / `0xac`**: the two scratch-plane addresses; `0xac`
  (current slot) whenever the level is non-zero, `0xa8` (last slot) only
  while enabled. The last-slot plane is refilled with a level-dependent byte
  (0xFF, 0xEE, 0xDD, ... for levels 0, 1, 2, ...) and flushed every picture.
- **Parameter range**: level 0..6 accepted, above 6 clamped to 3; dynamic
  motion estimation is latched at init (on only when the level is 0).

`FWM_VENC_PARAM_FILTER_3D_STRENGTH` (0x7f000003, `venc_ext.h`) is this
project's own extension: a direct 9-bit `T` (1..511) that overrides the level
rules, used by yi-mediad's strength setting. It is not a vendor control.

Device results (r35gb, 2026-09-27): register readback matches the
specification; `T = 511` gives a clearly visible filter effect, levels 1..3
are subtle at the encoder's usual QP. The IDR picture now takes the latest P
QP (a separate change) so keyframes do not visibly jump.

Public prior art (independent implementations of the same VE block)
-------------------------------------------------------------------

The register interface in `spec/` is not only a vendor-observable fact: the
Allwinner Video Engine H.264 encoder block is implemented from scratch, in
public, by independent projects. They are independent confirmation of the
interface (offsets and field meanings), and none is vendor source. None is
copied into this repository.

- jemk/**cedrus** `h264enc` — the original community RE of the VE H.264
  encoder (`VE_AVC_*` at `0xb00`, `VE_ISP_*` at `0xa00`, the `BASIC_BITS` +
  `TRIGGER` header bit-writer, the `VLE_*` output path).
- **uboborov**`/h264_encoder_H3` — the H3 port (same register defines).
- **hataketsu**`/h3-cedar-h264-encode` — H3 packaging + validation on mainline
  6.1.63; also aggregates an Allwinner VPU-encode code catalog (`RESEARCH.md`).
- linux-sunxi.org `VE_Register_guide` — the community register map.
- libv/**cedarx_h264_encoder** `kernel/cedar_regs.h` — the most complete
  *named* public register map (encoder + ISP + scaler + ROI/thumbnail), plus a
  from-scratch A20 kernel driver; its userspace is jemk's.
- zebin-wu/**shome-camera-sunxi** `include/{veInterface.h,vencoder.h}` — public
  copies of Allwinner's 2016 ABI headers; the `VeOpsS` slot order and
  `VencBaseConfig` fields match `spec/ve-driver.md` / `spec/h264-enc-flow.md`.

Cross-checked 2026-09-22 against `spec/h264enc-codec-library.md` §4.1: the
encoder/ISP block bases, the `INT_ENABLE`/`STARTTRIG`/`STATUS`/`PUTBITSDATA`
offsets, the `REC`/`REF`/`MB_INFO` addresses and the `VLE_ADDR/OFFSET/LENGTH`
path all agree. Two deliberate, legal divergences: cedrus emits `slice_type`
`I=2`/`P=0` where the reference emits `I=7`/`P=5`, and cedrus uses
`pic_order_cnt_type=2` where the reference carries a POC lsb.

Licence note: jemk/cedrus `h264enc` is GPL-2.0-or-later and its `ve.h` is
LGPL-2.1-or-later; both are used here as **read-only prior art**, no code is
imported. Whether the clean-room implementer may read them is a clean-room
policy question, not a licence one, and belongs with the project's
clean-room/legal process.

A fuller survey — the same-block SoC lineage, per-source licensing, and the
public writing guides — is in `ve-encoder-sources.md`.

Excluded from this inventory
----------------------------

- **Function-local scratch arrays**, computed per call and holding no fixed
  data: the header bit-writer `h264_bw.buf[H264_BW_MAX_BYTES]` (256 bytes,
  `src/h264/headers/h264_headers.c:16`) and the allocated context arrays
  `shadow[13]`/`scaler_coeff[64]` (`src/h264/isp/h264_isp.c:18-19`).
- **Mutable process state**, not literal data: `g_env`
  (`src/h264/ve/ve_driver.c:82`) and `g_port` (`:144`).
- **Name strings**: the encoder device-table name strings and the debug strings;
  labels, not tables.

Not distributable from this tree
--------------------------------

The deployed vendor objects, the private differential harnesses and their
withheld golden/capture vectors, the on-camera captures, and build output are
**not part of this repository**: they live in the private analysis workspace,
outside this tree, and are gitignored (`codec/.gitignore`). They are
development references only.

Third-party components
----------------------

The AAC path is built on upstream **FAAC** (Freeware Advanced Audio Coder,
Copyright (C) 1999-2026 the FAAC authors), **LGPL-2.1-or-later**. FAAC is
**not vendored**: `codec/mk/aac.mk:6` takes a builder-supplied `FAAC_DIR`,
its comments state it is never vendored (`mk/aac.mk:3-4, 30`), and the AAC unit
is skipped when it is absent (`mk/aac.mk:32-33`); `docs/integration-aac.md` states that the archive this subproject
produces is the FAAC-backed replacement for `-laacenc`. FAAC's copyright and
licence must accompany any binary distribution; the full third-party statement
is in `codec/CREDITS.md`. The H.264 path links only libc/libm (and pthread for
the VE driver). Neither AAC nor H.264/AVC is royalty-free; that is a
distribution-time decision.

Trademarks and related names are the property of their respective owners and
are used only for identification and interoperability.

This statement is not legal advice.
