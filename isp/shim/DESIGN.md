<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# ISO pilot shim - design

Integration/glue layer only. It does not implement ISO behaviour; it presents the
clean-room ISO core (`src/iso/iso_clean.c`) to the Yi/mediad ISP
framework through the framework's `fwi_iso_cfg_core_ops_t` vtable (the same
shape as the SDK's `isp_iso_core_ops_t`).

Complete: the gain/luminance index path, the parameter/context plumbing, the
live tuning-corpus mapping into `iso_ctx_t`, and the full per-module IQ writeback
into `isp_lib_context.module_cfg` / `sensor_info` / `ae_settings` / `adjust` /
`stats_ctx` / `tune` are all wired. The SDK-boundary differential
(the private ISO differential harness) drives one scenario through the vendor
object and the clean-core-through-shim and compares the resulting SDK structs
field by field: 9 cases, 0 mismatches, sensitivity-checked.

## Sources of truth

| Fact | File |
| --- | --- |
| Clean API (`iso_entity_t`, `iso_params_t`, `iso_ctx_t`, `iso_result_t`, `freeisp_tables_t`) | `include/iso_clean.h` |
| SDK contract (`iso_param_t`, `iso_result_t`, `isp_iso_core_ops_t`, `iso_init/iso_exit`) | SDK `.../libisp/include/isp_iso_config.h` |
| SDK shared context (`struct isp_lib_context` and sub-structs) | SDK `.../libisp/include/isp_manage.h` + `isp_tuning/isp_tuning_priv.h` |
| Vendor entity layout (config at offset 0) | the deployed `isp_iso_config.o` (interface fact) |

The shim builds against the generated `fwi_*` headers (`include/fwi_isp_api.h`),
whose records are layout-identical to the SDK types listed above (the move off
the SDK-shaped header on 2026-09-26 left the ARM object code unchanged,
function by function). Every `isp_gen` field access therefore uses the framework's own
offsets. `-DISP_VERSION=521` is required so
`isp_lib_context`/`isp_dynamic_config` match the deployed v521 framework.

## Name collision handling

The clean and SDK interfaces collide on the typedef `iso_result_t` and on the
entry points `iso_init`/`iso_exit`. `iso_shim.c` renames the clean spellings for
its translation unit only:

```c
#define iso_result_t   clean_iso_result_t
#define iso_init       clean_iso_init
/* ... iso_exit/get/set/run ... */
#include "iso_clean.h"
#undef ...
#include "fwi_isp_api.h"         /* framework (fwi_*) names from here on */
```

`iso_clean.c` is compiled separately with `-Diso_init=clean_iso_init ...` so the
clean objects export the `clean_iso_*` symbols. No file in `src/`,
`include/`, `yi-mediad`, or the vendor tree is modified.

## Entity layout

`iso_shim_entity_t` is heap-allocated by `iso_init` and returned as `void *`:

```c
typedef struct iso_shim_entity {
    iso_param_t    config;   /* SDK mirror, offset 0 (vendor entity layout); get_params returns &config */
    iso_entity_t  *clean;    /* clean-room instance                                                      */
    iso_ctx_t      gen;      /* embedded clean shared context; clean params keep a pointer to it         */
} iso_shim_entity_t;
```

The framework never hard-codes an offset into the entity: it stores the
`iso_param_t *` returned by `get_params` and reads/writes it in place. The shim
therefore keeps `config` at entity offset 0; the vendor entity happens to place
it at +0xc (from the vendor ISO get-params step, confirmed against the deployed object). The offset
is opaque to the framework, so this difference is not observable. The shim
re-reads its mirror on set/run.

## Table provider

`iso_clean` does not compile the located tuning tables in; it calls `freeisp_get_tables()` at
`clean_iso_init` time. `iso_shim.c` defines that symbol and keeps a single
`freeisp_tables_t`. `iso_shim_set_tables(const void *t)` installs an
integration-supplied `iso_clean_tables_t` (opaque here to avoid pulling the
clean header into SDK-visible code); `NULL` restores built-in pilot defaults.
The setter must be called before `iso_init()` (the clean core latches `->tab` at
init).

Built-in pilot defaults (placeholders, not tuning data): `gain_index_table[k]=k`
(350, monotone), `gain_point_default[i]=24+2i`, `lum_point_default[i]=24(i+1)`,
`af_square_table[i]=0xa0+i`.

## Mapping: SDK -> clean

### `iso_param_t` -> `iso_params_t` (on `set_params`)

| clean `iso_params_t` | SDK source |
| --- | --- |
| `param_kind` | `ISO_PARAM_REBUILD` (0) |
| `frame_id` | `iso_frame_id` |
| `chroma_gray_th` | `cem_color2gray_th` |
| `chroma_gray_span_min` | `cem_color2gray_delta_min` |
| `chroma_gray_span_max` | `cem_color2gray_delta_max` |
| `cnr_on` | `cnr_adjust` |
| `sharp_on` | `sharpness_adjust` |
| `sat_on` | `saturation_adjust` |
| `contrast_on` | `contrast_adjust` |
| `brightness_on` | `brightness_adjust` |
| `cem_on` | `cem_ratio_adjust` |
| `denoise_on` | `denoise_adjust` |
| `sensor_offset_on` | `sensor_offset_adjust` |
| `black_level_on` | `black_level_adjust` |
| `dpc_on` | `dpc_adjust` |
| `defog_on` | `defog_value_adjust` |
| `pltm_dyn_on` | `pltm_dynamic_cfg_adjust` |
| `tdnr_on` | `tdnr_adjust` |
| `ae_cfg_on` | `ae_cfg_adjust` |
| `gtm_cfg_on` | `gtm_cfg_adjust` |
| `lca_cfg_on` | `lca_cfg_adjust` |
| `af_cfg_on` | `af_cfg_adjust` |
| `dn_core_ratio[4]` | `isp_denoise_lp0/1/2/3_np_core_retio` |
| `gen` | address of embedded clean `iso_ctx_t` |
| `cfg[14]` | `isp_gen->isp_ini_cfg.isp_iso_settings.isp_dynamic_cfg[14]` (byte copy, 592 B each) |
| `gain_point[14]` | `isp_gen->isp_ini_cfg.isp_iso_settings.isp_gain_mapping_point[14]` |
| `lum_point[14]` | `isp_gen->isp_ini_cfg.isp_iso_settings.isp_lum_mapping_point[14]` |

`struct isp_dynamic_config` (SDK, `HW_S32`, v521) and `iso_dyn_t` (clean, union of
`int32_t[148]`) have identical field order and size (148 x 4 = 592 B), so the
dynamic records are copied with `memcpy`; a compile-time size check guards this.

### `isp_lib_context` -> `iso_ctx_t` (on set and re-applied on every `run`)

Only input/state fields are written; fields the clean core owns as outputs are
left alone so cross-frame state (`module_enable_flag`) survives.

| clean `iso_ctx_t` | `isp_lib_context` source |
| --- | --- |
| `total_gain` | `sensor_info.total_gain` |
| `ae_pos` / `ae_pos_max` | `sensor_info.ae_tbl_idx` / `.ae_tbl_idx_max` |
| `ae_mode` | `ae_settings.ae_mode` |
| `tdf_enable` | `isp_ini_cfg.isp_test_settings.tdf_en` |
| `contrast_enable` | `...contrast_en` |
| `wb_enable` | `...wb_en` |
| `sat_enable` | `...satur_en` |
| `pltm_enable` | `...pltm_en` |
| `contrast_level` / `sat_level` / `sharp_level` | `tune.contrast_level` / `.saturation_level` / `.sharpness_level` |
| `brightness_level` / `denoise_level` | `tune.brightness_level` / `.denoise_level` |
| `pltm_level` / `tdf_level` | `tune.pltmwdr_level` / `.tdf_level` |
| `highlight_level` / `backlight_level` | `tune.highlight_level` / `.backlight_level` |
| `trig_*` (16) | `isp_ini_cfg.isp_iso_settings.triger.*` |
| `dynamic_enable` | `stats_ctx.dynamic_stats.enable` |
| comp targets + `comp[4]` arrays | `stats_ctx.dynamic_stats.*_target` / `.*_comp[4]` |
| `pltm_strength` | `pltm_entity_ctx.pltm_result.pltm_next_stren` |
| `wb_r_gain` / `wb_gb_gain` / `wb_b_gain` | `awb_entity_ctx.awb_result.wb_gain_output.r_gain` / `.gb_gain` / `.b_gain` |
| `color_matrix` | `module_cfg.rgb2rgb_cfg.color_matrix` |
| `rgb2yuv` | `module_cfg.rgb2yuv` |
| `gamma_table[3072]` | `module_cfg.gamma_cfg.gamma_tbl[3072]` |

### `isp_tunning_param` -> `iso_ctx_t` live tuning corpus

The vendor object reads its tuning corpus directly from
`isp_gen->isp_ini_cfg.isp_tunning_settings` at run time; the clean core reads the
same data from `iso_ctx_t`. The SDK and clean shapes differ, so `map_tuning()`
does a typed element-by-element copy. Offsets and adjacency are from the
deployed `isp_iso_config.o` layout and confirmed against the object's own
constant data (the source addresses `isp_gen+0x202f7` etc. are exactly
`isp_tunning_settings` + the field offsets).

| clean `iso_ctx_t` | SDK `isp_tunning_param` source | conversion |
| --- | --- | --- |
| `k3d_incre_curve[256]` | `isp_d3d_k3d_incre_curve[256]` | `HW_U8` -> `int32_t` |
| `tdnf_diff[256]` | `isp_tdnf_diff[256]` | `HW_U8` -> `int32_t` |
| `sharp_edge_lum[33]` | `isp_sharp_edge_lum[33]` | none (both `HW_U16`) |
| `sharp_hfrq_lum[33]` | `isp_sharp_hfrq_lum[33]` | none |
| `sharp_hsv[46]` | `isp_sharp_hsv[46]` | none |
| `sharp_s_map[33]` | `isp_sharp_s_map[33]` | none (both `HW_U8`) |
| `sharp_val[66]` | `isp_sharp_val[33]` **++** `isp_sharp_lum[33]` | `HW_U16` -> `int32_t`, two halves |
| `bdnf_th[33]` | `isp_bdnf_th[33]` | `HW_U16` -> `int32_t` |
| `tdnf_th[66]` | `isp_tdnf_th[33]` **++** `isp_tdnf_ref_noise[33]` | `HW_U16` -> `int32_t`, two halves |
| `tdnf_k[32]` | `isp_tdnf_k[32]` | `HW_U8` -> `int32_t` |
| `cem_table_a[5888]` | `isp_cem_table[5888]` | none |
| `cem_table_b[5888]` | `isp_cem_table1[5888]` | none |

The two two-half merges are the important shape facts. The vendor source pointer
is the first array and it indexes `[0x21 + k]` (`33 + k`), landing in the next
member; for `sharp_val` the next member is `isp_sharp_lum`, and for `tdnf_th` it
is `isp_tdnf_ref_noise`. The clean `iso_ctx_t`/`iso_result_t` model these as one
66-element array. `isp_cem_table` is the table weighted by the CEM ratio
(`cem_table_a`), `isp_cem_table1` its complement (`cem_table_b`).

## Result writeback

`map_result_to_sdk()` runs at the end of `shim_run` and copies:

- the clean index result -> `iso_result_t.gain_idx` / `.lum_idx`;
- the clean module result block -> `module_cfg` (CNR, sharpness incl.
  `sharp_val`+`sharp_lum`, saturation + `sat_curve` + `mode_cfg.saturation_mode`,
  2D denoise `bdnf_cfg`, DPC `otf_cfg`, LCA `lca_cfg`, AF `af_cfg` incl. the AF
  square LUT, CEM `cem_cfg.cem_table`, and the 3D denoise `tdf_cfg`);
- the clean shared context -> `module_cfg.module_enable_flag` /
  `.table_update`, `sensor_info.gain_offset` (and
  `module_cfg.gain_offset_cfg.sensor_offset`), `module_cfg.gain_offset_cfg.offset`
  (black level), `adjust.*`, `stats_ctx.dynamic_stats` (enable + the five
  compensation arrays), `ae_settings.exposure_cfg` / `.pltm_dynamic_cfg` /
  `.ae_hist_eq_cfg`, and `tune.sharpness_level`.

Every block is written only when its `*_adjust` flag is set, exactly as the
vendor object gates its own writes; a disabled block leaves the framework value
untouched.

`tdnf_table` is a framework-supplied `void *`, not a member. The shim writes the
256-byte increment curve then the 256-byte clamped diff curve into that buffer,
matching the vendor's two `memcpy`s. The clean result stores each byte in a
`uint16` slot, so the low byte is written.

The clean CEM image is 6656 bytes while `module_cfg.cem_cfg.cem_table` is 5888
bytes. This is intentional: the vendor's per-cell pass indexes into the following
`lut_cfg` member, so the shim copies the whole 6656-byte span.

## Pass-through only

`iso_param_t.type`, `.isp_platform_id`, `.test_cfg` are stored in the mirror but
not consumed. `trig_gtm_cfg` is mapped but unused (the clean spec selects GTM with
the AE-config trigger, as the vendor does).

## Build

- Host: `make` -> `build/test_iso_shim`, run directly.
- ARM: `make arm` -> `build_arm/test_iso_shim.arm`, static musl, then run it
  under the analysis workspace's `qemu-arm`.

The SDK include directory is added with `-isystem` (its headers are not ours to
lint).

## AFS shim

`shim/afs/` presents the clean AFS core (`src/afs/afs_clean.c`) to the
framework through `isp_afs_core_ops_t`. AFS is auto-flicker suppression (mains
ripple), not autofocus.

The vendor algorithm never sees `isp_lib_context`: the firmware's `isp_manage.c`
projects the tuning/config corpus onto `afs_param_t` before each frame, and the
algorithm reads only that block. `afs_shim_update_cfg()` reproduces the
projection (the `isp_manage.o` TU is not in `libisp_algo_rtos.a`):

| `afs_param_t` | SDK source (framework) |
| --- | --- |
| `flicker_ratio` | `isp_tunning_settings.flicker_ratio` |
| `flicker_type_ini` | `isp_tunning_settings.flicker_type` |
| `test_cfg.isp_test_mode` | `isp_test_settings.isp_test_mode` |
| `test_cfg.afs_en` | `isp_test_settings.afs_en` |
| `isp_platform_id` | `module_cfg.isp_platform_id` |
| `afs_frame_id` | `af_frame_cnt` |
| `afs_sensor_info` | `sensor_info` (whole descriptor) |

`map_params()` then maps that block into the clean `afs_clean_params_t`: mode,
seed, ratio, enable, AE-gain window (`afs_sensor_info.ae_gain`, offset 108 —
not `ae_tbl_idx`), image width (`sensor_width`, offset 48), frame index.

`flicker_mode` is **not** projected: the vendor AFS context-update step does
not assign it in either the deployed v521/v316 tree or v833, so it is left to
the caller's get-params mirror. This is an explicit gap, not a faked mapping.

The only algorithm output is `afs_result_t.flicker_type_output` (clean 0/50/60
-> SDK `FLICKER_NO/50HZ/60HZ`). `map_result_to_sdk()` writes it, and when a
context is linked it performs the `ae_settings.flicker_type` copy that
the vendor AFS run stage does, so the clean path is observable end to end. Frames < 3
and unknown modes leave the persistent result untouched, as the vendor does.

The SDK-boundary differential is self-contained in the private AFS differential
harness; see the private run plan §6.11.
