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
     accumulator in `src/awb/awb_clean.c`, and the zeroed `z[256]` scratch
     buffers in the shim tail dumpers). A table declared *inside* a function but
     carrying recovered or interface values is **listed**, not excluded (the
     AF `iir_g`/`fir_g`, the colour-effect matrices, `src_bit`/`hw_bit` and the
     Q-menu arrays are all of this kind and appear below).
   - **This project's own non-data constants** — objects that are not tables of
     recovered or interface values: the locator anchor label string
     `"Outlier Light"` (`src/tables/rmm_tables.c`, a label, not a table); the
     AE tail-dump framing magic `AE_TAIL_MAGIC` (`shim/ae/ae_shim.c`, our own
     `"AETL"` file signature) and the `".tail"` dump-suffix string in the same
     file; runtime pointer variables and seams
     (`isp_uapi_sys`, `isp_ctx_save_test_mount_check` and
     `isp_ctx_save_test_mount_prefix`, and the shim
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
   | Platform default ISP configuration seed (`isp_ini_cfg`, including the 9-entry AWB illuminant roster `awb_light_info`) | `src/framework/isp.c` | **Observed**, and compiled in as the platform's default configuration seed: the roster reproduces the default 3A settings the deployed library starts from before any tuning image is read (the per-illuminant colour temperature, gains and thresholds). It is not sensor-specific; the device-specific calibration is read from the camera's own `rmm` image at first boot and scales this roster at runtime — see "Runtime data". | Platform default config |
   | PLTM preset parameterisation, 19 rows (`k_presets`) | `src/pltm/pltm_clean.c` | **Observed** as a platform default: the deployed library is run under a private differential harness and its output recorded for each preset index. The harness and its output are private and are **not shipped** with this repository; the observed values are reproduced in full in this tree (`src/pltm/pltm_clean.c`, spec `spec/pltm.md` §7.8). | Observed platform default |
   | AF filter coefficients `iir_g[6]` and `fir_g[5]` (two separate `static const` arrays in the AF-configuration block) | `src/iso/iso_clean.c` | **Observed** the same way, from the deployed AF-configuration path's output. The harness and output are private and are **not shipped**; the observed values are reproduced in full in this tree (`src/iso/iso_clean.c`, spec `spec/iso.md` §6.12). | Observed platform default |
   | Colour-effect matrices (`c_none`, `c_gray`, `c_neg`, `c_antique`, `c_r`, `c_g`, `c_b`), their dispatch arrays (`c_tbl`, `d_tbl`) and offsets (`d_neg`, `d_zero`) | `src/reg/base.c` | **Re-derived** in code from standard published colour maths: the identity (none); the canonical BT.601 luma-weighted grey (0.3/0.6/0.1); the canonical sepia matrix (antique); signed inversion with a +2.0 offset (negative); and per-channel scale maps of 1.5 and 2/3 (R/G/B). | Re-derived |
   | `src_bit[12]` / `hw_bit[12]` | `src/reg/reg_writers.c` | **Interface facts**: the API's bit order and the set bits of the AF register mask. | Interface fact |
   | `g_af_filter_off[19]` | `src/reg/reg_writers.c` | **Interface facts**: the AF filter register offsets. | Interface fact |
   | `isp_module_attrs[31]` | `src/reg/module_cfg.c` | **Dispatch table**: the module name strings and our config/enable handler pointers. The names and feature bits are interface facts; the handlers are ours. | Dispatch/lookup |
   | `isp_cid_array[44]` | `src/framework/isp_dev_uapi.c` | **Interface facts**: the V4L2 control ids and the literal macro-name strings, matching the deployed control table byte for byte. | Interface fact |
   | `cid_routes[]` | `src/framework/isp.c` | **Dispatch table**: control id to setter plus argument mode. The control ids and argument modes reproduce the deployed handling; the setters are ours. | Dispatch/lookup |
   | `iso_qmenu[]`, `exp_bias_qmenu[]` | `src/framework/isp.c` | **Interface facts**: the enumerated V4L2 control menus for ISO sensitivity and exposure bias. | Interface fact |
   | `v4l2_to_pixel[]`, `pixel_to_v4l2[]` | `src/utils/pixel_format.c` | **Lookup tables**: V4L2 fourcc to `PIXEL_FORMAT_E` and back, including the reproduced SBGGR10/12 sibling mapping. The fourcc and enum values are interface facts. | Lookup |
   | `freeisp_table_locs[]` | `include/freeisp/tables_layout.h` | **Locator metadata**: our own offset, size and name rows for the runtime table contract; no table bytes. | Locator metadata |
   | CM colour-temperature interpolation knots, 3 ints (`g_cm_color_temp_default`) | `shim/base/base_shim.c` | **Recovered**: the deployed base object carries the 12-byte `{2700,4000,6500}` breakpoint list as a compiled-in const (the byte pattern is present in `isp_base.o`), so the shim falls back to this recovered literal when the runtime feed supplies none. Whether the stock tuning image also carries a copy was not checked. | Vendor-recovered constant |
   | `isp_ctx_ops`, `isp_dev_ops`, `host_sys`, `g_ops` | `src/framework/isp.c`, `src/framework/isp_dev_uapi.c`, `src/afs/afs_clean.c` | **Dispatch tables**: file-scope `const` structs of our own handler pointers, plus the injectable syscall seam (`host_sys`). No vendor data. | Dispatch/lookup |

   The observed tables are platform defaults: they are not sensor-specific, they
   are not present in any camera tuning image we searched, and they are needed for
   the algorithms to reproduce the deployed library's output. The observed rows
   are determined by black-box execution of the deployed object code, not
   transcription of its source.

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

The library does carry the platform's *default configuration seed*: the 3A
enable and interval values and the 9-entry AWB illuminant roster
`awb_light_info` (colour temperature, gains and thresholds per illuminant) in
`isp_ctx[0].isp_ini_cfg`. Every other byte of that seed is zero (spec section 6).
This is configuration data the ISP needs in order to come up, not algorithm
expression. The device-specific part is the tuning itself, which is located in
the camera's own `rmm` image at runtime and cached (see "Runtime data").

The OTP golden-ratio path is a separate matter and is **not** populated here or
in the deployed image: `isp_sensor_otp_init`, which would fill `otp_enable`,
`pmsc_table` and the white-balance `pwb_table`, is dead in the deployed object
(r1 `20-framework.md`, OTP), and the `rmm` feed maps a different field
(`otp_msc_golden`, `shim/base/base_shim.c`). In this tree `otp_enable` and
`pwb_table` have no writer at all; `pmsc_table` is written only by the
neutral-value path in `src/framework/tuning.c:242-247`, which fills all 768
words with `1024` gated on `otp_enable == -1` — a value no production path
produces. Consequently `otp_enable` stays at its initial value (0), so the OTP
scaling branches in `src/framework/manage.c` are never taken.
`isp_get_debug_msg` exposes the three fields to the caller's blob and
`isp_get_info_length` sizes it (`src/framework/isp.c`).

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
