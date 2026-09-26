<!-- SPDX-License-Identifier: AGPL-3.0-only -->

Provenance and deviations — freewinner
===============================

An independently written implementation of the Allwinner `libisp` 3A algorithms
(AE / AWB / AFS anti-flicker / ISO / PLTM / GTM) for sun8iw19p1 / V833-class Yi IP
cameras, produced for interoperability with hardware we own.

Copyright (C) 2026 freewinner contributors.

**RELEASE STATUS (owner decision, 2026-09-20).** An engineering audit exists in
the private project workspace; **no independent legal review has been completed,
and none is required** — Hutch removed the counsel prerequisite for publication.
The provenance caveats below stand as the risk record: accepted by the owner, not
resolved. Before publishing, run the publish guard (private workspace tooling) on
a clean export of this repository and require it to report `RESULT: PASS`.

Provenance
----------

The algorithms were reverse engineered from the *deployed* on-camera binary
(`libisp_algo_rtos.a`, ARMv7). No Allwinner source code for the target version is
included. Interface facts needed for interoperability (entry-point names, ABI
layouts, feature bits) are reproduced; those are facts, not expression.

Two provenance caveats, stated rather than hidden:

1. **Register tier — re-produced and verified 2026-09-16.** An earlier
   `src/reg/{base,module_cfg,reg_writers}.c` was written with direct access to the
   vendor binary in the same working sessions, so the strict
   spec-team/implementation-team separation described in
   `../reimplementation/README.md` was not
   maintained for that tier. That record was superseded: a behaviour-only spec
   (`spec/reglayer2.md`) was written, and all three units (`base.c`, `module_cfg.c`,
   `reg_writers.c`) were then re-produced by implementation-role agents with no
   binary access and independently re-verified. See the private engineering
   audit §3 and `reimplementation/README.md`.

2. **Numeric data — classified, and disclosed in full.** The former `comp_ref`
   literal table was found to be a geometric radial-distance table and is now
   re-derived in code (`src/reg/comp_ref.c`); the temperature-trigger defaults and
   the AE aperture ladder were moved into the runtime-injected table contract.
   **The claim, stated so it can be checked.** *Every `const` object carrying a
   literal initialiser in `src/`, `include/` and `shim/` outside test fixtures is
   either listed in the table below or excluded by one of the three rules that
   follow* — whether it has file scope or is declared inside a function. To
   verify: take every such definition (not a prototype, function parameter or
   computed local), skip the test fixtures covered by the third rule, and confirm
   each remaining one appears either as a row below or under an exclusion below.
   The three exclusions are:

   - **Function-local scratch** — arrays declared inside a function, computed at
     runtime, holding no recovered or interface data (the intermediate colour
     buffers in `src/reg/base.c`, the base pair in `src/tables/rmm_tables.c`, the
     accumulator in `src/awb/awb_clean.c`, `raw[3]` in `src/reg/base.c`
     (initialised from runtime values), and the zeroed `z[256]` scratch
     buffers in the shim tail dumpers). A table declared *inside* a function but
     carrying recovered or interface values is **listed**, not excluded (the
     AF `iir_g`/`fir_g`, the colour-effect matrices, `src_bit`/`hw_bit`, the
     ISO/exposure-bias menus, the library-default compensation tables and
     `bits[3]` are all of this kind and appear below).
   - **This project's own non-data constants** — objects that are not tables of
     recovered or interface values: the locator anchor label string
     `"Outlier Light"` (`src/tables/rmm_tables.c`, a label, not a table); the
     AE tail-dump framing magic `AE_TAIL_MAGIC` (`shim/ae/ae_shim.c`, our own
     `"AETL"` file signature) and the `".tail"` dump-suffix string in the same
     file; runtime pointer variables and seams (`isp_uapi_sys`, and the shim
     `g_tables`/`g_trig`/`g_override`/`g_loc_*` pointers); and the shim
     aggregation structs `g_default_<mod>` / `g_default_tables` /
     `g_default_trig`, which are pointer bundles over runtime-injected tables
     (their members are filled by the locator, not compiled in).
   - **Test fixtures** — every `const` object that lives in a test file (any
     `tests/` directory, or a file named `test_*.c`), e.g. the AWB
     `warm`/`mid`/`cool` colour cases (`shim/awb/test_awb_shim.c:86-88`) and the
     GTM `shapes` list (`shim/gtm/test_gtm_shim.c:426`). These are small expected
     values used to verify the modules in isolation, not data compiled into the
     library; the rule excludes the whole class, so no individual fixture needs
     to be named.

   An earlier draft of this document claimed there was only one such array; that
   was wrong and is corrected here. See the private engineering audit §4.

   | Compiled-in table | Where | How it was obtained | Class |
   |---|---|---|---|
   | PLTM preset parameterisation, 19 rows (`k_presets`) | `src/pltm/pltm_clean.c` | **Observed** as a platform default: the deployed library is run under a private differential harness and its output recorded for each preset index. The harness and its output are private and are **not shipped** with this repository; the observed values are reproduced in full in this tree (`src/pltm/pltm_clean.c`, spec `spec/pltm.md` §7.8). **Open item**: still vendor-ordered; the private r1 spec (unit T, tables) proposes an owned monotone mapping in its place, pending owner A/B captures on scene 2 (backlit window) and scene 3 (night IR). | Observed platform default |
   | AF filter coefficients `iir_g[6]` and `fir_g[5]` (two separate `static const` arrays in the AF-configuration block) | `src/iso/iso_clean.c` | **Observed** the same way, from the deployed AF-configuration path's output. The harness and output are private and are **not shipped**; the observed values are reproduced in full in this tree (`src/iso/iso_clean.c`, spec `spec/iso.md` §6.12). The private r1 spec (unit T, tables, item T1) recommends removing this block entirely, since no stock tuning we hold turns AF on; not yet acted on. | Observed platform default |
   | Colour-effect matrices (`c_none`, `c_gray`, `c_neg`, `c_antique`, `c_r`, `c_g`, `c_b`), their dispatch arrays (`c_tbl`, `d_tbl`) and offsets (`d_neg`, `d_zero`) | `src/reg/base.c` | **Re-derived** in code from standard published colour maths: the identity (none); the canonical BT.601 luma-weighted grey (0.3/0.6/0.1); the canonical sepia matrix (antique); signed inversion with a +2.0 offset (negative); and per-channel scale maps of 1.5 and 2/3 (R/G/B). | Re-derived |
   | `src_bit[12]` / `hw_bit[12]` | `src/reg/reg_writers.c` | **Interface facts**: the API's bit order and the set bits of the AF register mask. | Interface fact |
   | `g_af_filter_off[19]` | `src/reg/reg_writers.c` | **Interface facts**: the AF filter register offsets. | Interface fact |
   | `isp_module_attrs[31]` | `src/reg/module_cfg.c` | **Dispatch table**: the module name strings and our config/enable handler pointers. The names and feature bits are interface facts; the handlers are ours. | Dispatch/lookup |
   | Pixel-format conversion (`map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E` / `map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT`) | `src/utils/pixel_format.c` | Not a `const` object; listed because it replaced the former `v4l2_to_pixel`/`pixel_to_v4l2` tables. Two `switch` statements, V4L2 fourcc to `fwm_pixel_format_e` and back, written from the unit U spec. The fourcc and enum values are interface facts. | Interface fact |
   | `freeisp_table_locs[]` | `include/freeisp/tables_layout.h` | **Locator metadata**: our own offset, size and name rows for the runtime table contract; no table bytes. | Locator metadata |
   | CM colour-temperature interpolation knots, 3 ints (`g_cm_color_temp_default`) | `shim/base/base_shim.c` | **Recovered**: the deployed base object carries the 12-byte `{2700,4000,6500}` breakpoint list as a compiled-in const (the byte pattern is present in `isp_base.o`), so the shim falls back to this recovered literal when the runtime feed supplies none. Whether the stock tuning image also carries a copy was not checked. | Vendor-recovered constant |
   | `default_sys`, `g_ops` | `src/framework/isp_dev_uapi.c`, `src/afs/afs_clean.c` | **Dispatch tables** of our own function pointers. `default_sys` is the default target of the injectable syscall seam `isp_uapi_sys` (our thin libc wrappers: open, close, ioctl, mmap, munmap, select, access, system, readlink, stat); the pointer itself is excluded above. `g_ops` is the AFS core's ops table. No vendor data. | Dispatch/lookup |
   | `ctrl_ids[]` (44 V4L2 control ids) | `src/framework/events.c` | **Interface facts**: the V4L2 control ids this ISP thread subscribes to for change events, as specified in unit F §4.7. Replaces the former `isp_cid_array`/`cid_routes` tables; control handling is now a `switch` in `helper.c`. | Interface fact |
   | `iso[7]` (ISO sensitivity menu), `bias[9]` (exposure-bias menu) | `src/framework/helper.c` | **Interface facts**: the menu-index-to-value mapping for the V4L2 ISO sensitivity (100..6400) and exposure-bias (-4..+4) controls. Replaces the former `iso_qmenu`/`exp_bias_qmenu` tables. | Interface fact |
   | `map[]` (two `struct fmt_scalar_map` arrays, one per direction) | `src/framework/isp_dev_uapi.c` | **Our own struct-offset copy table**: `offsetof` pairs between our own `struct isp_video_device` and `struct video_fmt`, consumed by `fmt_copy_scalars`. No vendor data. | Dispatch/lookup |
   | Library-default dynamic-stats thresholds and compensation tables: `k[6]` (movement threshold base), `tdnf_comp[4]`/`tdnf_diff[4]` (temporal denoise), `lp_ratio[4]` (low-pass threshold ratio), `sharp_hf[4]`/`sharp_edge[4]`/`sharp_us[4]` (sharpen high-frequency/edge/undershoot) | `src/framework/tuning.c`, `isp_library_defaults` | **Vendor reference values**: constants of the deployed framework's full-configuration reset, not tuning data from the camera. The analyst recorded them in the private r1 spec (unit F §8.5). Owner decision Q3 was to reproduce them, because changing them changes image output. | Vendor-recovered constant |
   | `bits[3]` (`HW_ISP_CFG_TUNING_CCM_LOW/MID/HIGH`, two local iteration arrays) | `src/framework/tuning.c` | **Interface facts**: the three CCM-tier feature-bit macros from the tuning-blob ABI, gathered into a local array purely so the three tiers can be decoded in a loop. The bit values are interface facts; the array is our own convenience. | Interface fact |

   The two "observed" rows (`k_presets`, `iir_g`/`fir_g`) are platform
   defaults: they are not sensor-specific, they are not present in any camera
   tuning image we searched, and the algorithms need them to reproduce the
   deployed library's output. Their values were determined by black-box
   execution of the deployed object code, not by transcribing its source.

Runtime data
------------

The large algorithm tables the modules need (`AeConverData`, `Ae_Log2`,
`AwbProbData`, `TBL2GAIN`, `gd_curve_*`, the WDR/anti-gamma tables, ...) are not
compiled in. They are located at runtime inside the camera's own stock `rmm`
image and read from there, cache-first (`shim/integration/README.md`). The
locator finds two anchors in that image — an `Ae_Log2` table re-derived in code,
and the `"Outlier Light"` class-label row string — and slices every other table
at a fixed offset from one of those two bases; the recorded table names are used
as validator dispatch keys and in the layout checksum, not as image lookup keys.
`Ae_Log2` is additionally re-derived in code (`ae_log2_formula`,
`src/tables/rmm_tables.c`) and checked against that re-derivation; `AeGammaPre`
has structural checks only (`src/tables/rmm_tables.c`, monotone rows
`0..4095`), with no re-derivation.

The two vendor tuning-image class labels `"AH Light"` and `"Outlier Light"` are
used as lookup keys and anchors (`"Outlier Light"` is compiled in as the locator
anchor in `src/tables/rmm_tables.c`, and `AwbLightClassName` carries them at
runtime); they are labels only, not tuning-table bytes.

**No AWB illuminant roster is compiled in.** Earlier versions of this
document listed a 9-entry roster (`awb_light_info`, inside `isp_ini_cfg`) as an
"observed" platform default seed. That label was wrong: the roster was the
vendor SDK's own default context data. It was deleted when the r1 framework
replaced the old one (private r1 spec, unit T item T2). The roster now comes
only from the camera's own tuning: the caller (mediad) imports the device 3A
section from the stock `rmm` image and passes `light_num`/`light_info` in
through the AWB parameters. If that import is skipped or fails, the roster
count is zero and there is no compiled fallback. The remaining framework
defaults (3A enables, stat selects, delays, AWB interval and speed) are set in
code by `isp_library_defaults` (`src/framework/tuning.c`).

The OTP golden-ratio path is a separate matter and is **not** populated here or
in the deployed image: `isp_sensor_otp_init`, which would fill `otp_enable`,
`pmsc_table` and the white-balance `pwb_table`, is dead in the deployed object
(private r1 spec, unit F, OTP), and the `rmm` feed maps a different field
(`otp_msc_golden`, `shim/base/base_shim.c`). In this tree `otp_enable` and
`pwb_table` have no writer at all; `pmsc_table` is written only by the
neutral-value path in `src/framework/tuning.c:242-247`, which fills all 768
words with `1024` gated on `otp_enable == -1` — a value no production path
produces. Consequently `otp_enable` stays at its initial value (0), so the OTP
scaling branches in `src/framework/manage.c` are never taken.
`isp_get_debug_msg` exposes the three fields to the caller's blob and
`isp_get_info_length` sizes it (`src/framework/isp.c`).

Framework and utils clean-room provenance (units F, U, H)
----------------------------------------------------------

The framework (`src/framework/`: `events`, `helper`, `manage`, `tuning`, `isp`,
`isp_dev_uapi`) and the utilities (`src/utils/`) are a second clean-room
production, separate from the register tier above. They were written from
behaviour-only specs (unit F: framework, unit U: utilities; private r1 spec
set) by implementation-role agents without vendor-binary access, tested in a
private workspace and against a private replay harness, and wired into this
tree on **2026-09-26**. They replace the earlier, audit-flagged
`src/framework/*.c`.

**Unit H: generated ABI headers.** `include/fwi_isp_api.h`, `fwi_isp_abi.h`,
`fwi_isp_enum.h` and the `mw_headers/` forwarding headers are generated by our
own tool from a fact file of names, offsets, sizes, types and dimensions. The
layout facts were taken from the vendor objects' DWARF debug info on the
analyst side and joined with our rename map. A private checker compares the
built layout with that fact file: every shared record matches member for
member, and every shared enum matches in size and values. The one intended
difference is a 64-byte lock slot holding a 24-byte musl mutex plus padding.
Member declarations still read much like the vendor's (same types, same order,
`type name;`); what was re-expressed is naming and presentation, not layout.

**Shim port and adapter removal (2026-09-26).** The eight module shims under
`shim/` now use the `fwi_*` types directly. The six 3A vtable adapters that
used to sit in `manage.c` and the `sdk_interop.h` bridge header were deleted.
Compiled ARM code is identical per function to the previous build, except for
the intended change in the shims' run path: AE now reads live per-frame
parameters instead of a cached copy. The private similarity scan fell from
101,282 to 21,127 vendor-matching tokens across these changes.

**F1 (legal finding, closed).** The AWB safe-gain quadruple and the four ISO AF
IIR feedback coefficients are byte-exact vendor values. They are not compiled
in: they are fields of the injected-table contracts
(`awb_clean_tables_t.safe_gain`, `iso_clean_tables_t.af_iir_s`). The shims
fill them with neutral values of our own: unity gains (256) and zero
coefficients (`shim/awb/awb_shim.c`, `shim/iso_shim.c`).

**Open items:**
- `k_presets` (PLTM) is still vendor-ordered; see its table row.
- `iir_g`/`fir_g` (AF) are still compiled in; unit T item T1 proposes removing
  them, since no stock tuning enables AF.
- `bitmap.c`, `frame_size.c` and `media_helpers.c` have not been re-produced
  through the clean-room process. The owner decision (Q-U1) was to keep and
  re-produce them; the re-production has not been done.
- `mw_headers/media/` and `mw_headers/vencoder.h`: whether their declaration
  text is sufficiently re-expressed is a judgement call that has not had an
  independent review.

Not distributable from this tree
--------------------------------

Vendor objects used by the private differential harness, the vendor-derived
`*_tables_gen.c` arrays, build output and the bundled GPL-2.0-or-later qemu tree
are **not part of this repository**: they live in the private analysis workspace,
outside this tree, and are rejected by the publish guard. They are development
references only; see the private engineering audit §8.

Trademarks and related names are the property of their respective owners and are
used only for identification and interoperability.

This statement is not legal advice.
