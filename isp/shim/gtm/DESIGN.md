<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# GTM shim - design

Integration/glue layer only. It presents the clean-room GTM core
(`src/gtm/gtm_clean.c`) to the Yi/mediad ISP framework through the
SDK's `isp_gtm_core_ops_t` contract (`gtm_init` / `gtm_exit` /
`isp_gtm_{get,set}_params` / `isp_gtm_run`). No algorithm is implemented here.
See the ISO pilot in `../DESIGN.md` for the shared approach.

## Sources of truth

| Fact | File |
| --- | --- |
| Clean API (`gtm_entity_t`, `gtm_clean_params_t`, `gtm_clean_stats_t`, `gtm_clean_result_t`, `freeisp_tables_t`) | `include/gtm_clean.h` |
| SDK contract (`gtm_param_t`, `gtm_stats_t`, `gtm_result_t`, `isp_gtm_core_ops_t`) | SDK `.../libisp/include/isp_tone_mapping.h` |
| Framework translation into `gtm_param_t` | SDK `.../libisp/isp_manage/isp_manage.c` (the GTM set-params and context-update steps) |
| Shared context (`struct isp_lib_context`, `isp_gtm_stats_s`, `isp_drc_config`, `isp_gamma_config`) | SDK `.../libisp/include/isp_manage.h`, `isp_module_cfg.h` |
| Vendor entity layout / run path | the deployed `isp_tone_mapping.o` (interface fact) |

The shim builds against `include/freeisp/sdk_interop.h`, which reproduces the required
`isp_tone_mapping.h`/`isp_manage.h` ABI surface inline, so every `isp_gen` field
access uses the framework's own offsets; `-DISP521_RTOS_ALGO=1 -DISP_VERSION=521`
match the deployed v521 ABI.

## Name collision handling

The clean entry points share names with the SDK ones (`gtm_init`/`gtm_exit`), so
the clean core is compiled with `-Dgtm_init=clean_gtm_init ...` and the shim's
own copies are renamed for the bench (`objcopy gtm_init=shim_gtm_init`).

## Entity layout

```c
typedef struct gtm_shim_entity {
    gtm_param_t   config;   /* SDK mirror at offset 0; get_params returns it */
    gtm_entity_t *clean;    /* clean-room instance                           */
} gtm_shim_entity_t;
```

The framework stores the `gtm_param_t *` handed back by `get_params` and writes
into it in place, then calls set/run with the same pointer; the vendor entity
also places its config at offset 0 (`GTMV_INI` at `+0x10`).

## Corpus mapping: `gtm_param_t` -> `gtm_clean_params_t`

The framework has already translated the SDK tuning corpus
(`isp_ini_cfg.isp_tunning_settings.gtm_type/gamma_type/auto_alpha_en/hist_pix_cnt/
dark_minval/bright_minval/plum_var` and `ae_settings.ae_hist_eq_cfg[9]`) into the
`gtm_param_t` the ops receive, so the shim's corpus boundary is `gtm_param_t`.

| clean `gtm_clean_params_t` | SDK source | conversion |
| --- | --- | --- |
| `mode` / `gamma_mode` | `gtm_ini.gtm_type` / `.gamma_type` | enum int |
| `frame_index` | `gtm_frame_id` | int |
| `enable` | `gtm_enable != HW_FALSE` | bool |
| `range_max` | `gtm_ini.gtm_cfg[GTM_HEQ_GAIN]` | int |
| `eq_gain` | `gtm_ini.gtm_cfg[GTM_HEQ_EQ_RATIO]` | int |
| `cover_bias` | `gtm_ini.gtm_cfg[GTM_HEQ_EQ_SMOOTH]` | int |
| `black_level` / `white_level` | `gtm_ini.gtm_cfg[GTM_HEQ_BLACK/WHITE]` | int |
| `white_slope` / `black_slope` | `gtm_ini.gtm_cfg[GTM_HEQ_BLACK_ALPHA/WHITE_ALPHA]` | int |
| `pre_gamma_offset` | `gtm_ini.gtm_cfg[GTM_HEQ_GAMMA_IND]` | int |
| `hist_pixel_count` / `dark_floor` / `bright_floor` | `gtm_ini.hist_pix_cnt` / `.dark_minval` / `.bright_minval` | int |
| `peak_map[9][9]` | `gtm_ini.plum_var[9][9]` | `HW_S16` -> int |
| `brightness` / `contrast` | `brightness` / `contrast` | int |
| `bit_offset` / `wdr_en` | `gtm_bit_offset` / `wdr_en` | int / bool |
| `gamma_lut[1024]` | first 1024 of the `gamma_tbl` buffer | `HW_U16` copy |
| `curve[256]` / `curve_prev[256]` | `drc_table` / `drc_table_last` buffers | seeded at set time |

`gtm_ini.AutoAlphaEn`, `BrightPixellValue` and `DarkPixelValue` are deliberately
**not** mapped: the deployed object clears `AutoAlphaEn = 0` at the top of its
histogram path before reading either pixel value, and the clean core's
auto-alpha ramp is correspondingly hard-disabled, so only the `AutoAlphaEn == 0`
behaviour is reachable. (An otherwise unmappable field is not faked.)

The injected guide/pre-gamma/equalisation/convergence tables are a separate
runtime concern; `gtm_shim_set_tables()` installs a `gtm_clean_tables_t`
supplied by the integration layer (`shim/integration/freeisp_shim_tables.c`),
which locates them in the camera's `rmm`. NULL restores pilot defaults.

## Statistics mapping

`gtm_stats_t` is a 4-byte wrapper holding a pointer to `isp_gtm_stats_s`; the
shim copies `->hist[256]` into `hist_raw` and `->avg[384]` into `win_avg`, and
leaves the wrapper untouched.

## Result writeback

- `gtm_result_t` scalars: `Hist_MaxVal`/`avg_lum`/`avg_var`/`hist_div`/
  `hratio_last`/`hdr_req` from the clean result.
- The clean persistent curve is copied back into the framework's
  `module_cfg.drc_cfg.drc_table` / `.drc_table_last` (the pointers the vendor
  object writes through).
- Gated frames (`frame_index <= 2` or disabled) leave both the result and the
  curves untouched, exactly as the vendor GTM control step does.

## SDK-boundary differential

The private GTM differential harness links the deployed `isp_tone_mapping.o` and
the clean core *through the shim* into one ARM binary. Each scenario is built
into two identical `struct isp_lib_context` images; the framework's corpus
translation is replayed for both sides, and these regions are compared:
`gtm_result`, `module_cfg.drc_cfg`, `module_cfg.gamma_cfg` and the stored
`gtm_param` mirror (buffer pointers excluded). Seven scenarios cover all four
modes, both gamma modes, four histogram shapes and WDR with `bit_offset`: 28
cases, 0 mismatches, sensitivity-checked.

The differential exposed two clean-core divergences (both fixed in
`gtm_clean.c` and the spec):

1. luma-hold smoothing used `2*(c[i-1]+c[i]+c[i+1])`; the object's luma-hold
   step computes
   `c[i-2]+c[i+2]+2*(c[i-1]+c[i+1])+4*c[i]` (centre weight 4, not 2).
2. the Otsu statistic used signed arithmetic; the object uses unsigned 32-bit,
   and the normalised histogram sums above 4096 (rounding), so the upper class
   weight wraps.

## Build

- `make check` / `make run-arm` -> `ok: GTM shim`.
- the private GTM differential harness builds and runs the SDK-boundary
  differential, writing the withheld golden.
