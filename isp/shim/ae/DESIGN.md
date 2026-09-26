<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# AE shim + SDK-boundary differential

Integration/glue layer only: it presents the clean-room AE core
(`src/ae/ae_clean.c`) to the Yi/mediad ISP framework through the
SDK's `isp_ae_core_ops_t` contract. It implements no algorithm.

The SDK-boundary differential (the private AE differential harness) links the
deployed `isp_3a_ae.o` and the clean core through this shim into one ARM binary
and compares the resulting SDK `ae_result_t` and the persisted `ae_param_t`
mirror field by field. Current result: **19 cases, 17 with 0 mapped-field
mismatches, sensitivity-checked**. The two remaining mismatches are clean-core
algorithm edges (not shim mapping); they are listed at the end.

## SDK -> clean mapping (`map_config`)

Everything the deployed core reads from the parameter block is mapped into
`ae_params_t`: `ae_ini` (scene tables, aperture ladder, ranges, delays,
`ae_iso2gain_ratio`, `wdr_cfg`, `gain_ratio`, …), `ae_setting` (exposure
compensation, modes, ROI, `exposure_cfg[14]`, `ae_target_comp`),
`ae_sensor_info` and `test_cfg`. `ae_target_comp` maps to the clean
`target_comp` parameter (section 11). The framework frame id is applied to the
clean core each call via `ae_set_frame_index()` so the clean counter tracks
`ae_frame_id` exactly.

## Write-back (`sync_mirror`)

The deployed core mutates its embedded SDK mirror in place; the clean core
performs the same mutations on its own parameter block, so the shim copies them
back at the end of every set/run:

| SDK mirror field | clean source | gate (same as vendor) |
| --- | --- | --- |
| `ae_ini.ae_tbl_scene[0..2]` | `scene_tables[0..2]` | INIT, slot 0 unset |
| `ae_ini.ae_fno_step[16]` | `aperture_ladder[16]` | INIT, `[0]==0` |
| `ae_ini.ae_analog_gain_range[2]` | `analog_gain_range` | line-table build, either endpoint 0 |
| `ae_ini.ae_digital_gain_range[2]` | `digital_gain_range` | ditto |
| `ae_ini.ae_total_gain_range[2]` | `total_gain_range` | line-table build |
| `ae_ini.exp_comp_step` | `ev_comp_step` | auto path, was 0 |
| `ae_setting.sensor_gain` / `iso_sensitivity` | `sensor_gain` / `iso_sensitivity` | manual-ISO path |
| `ae_target_comp` | `target_comp` | pass-through |

`cfg/init_seed`, `cfg/tables_ranges` and `cfg/gain_range_1z` verify the
write-back with zero mapped-field mismatches.

## Result translation (`map_result`)

Maps status, the three sensor settings, `ev_idx_max`/`ev_idx_expect`, the
backlight/flash/`wdr_ratio` block, histogram fractions and `gain_ratio` into
the SDK `ae_result_t`. Like the deployed core, `sensor_set_short` is only
written in WDR mode (or on the no-statistics default path); a linear run does
not clobber it.

## Intentionally unmapped

- `ev_sensor_true_exp_line` (and `ev_av`/`ev_tv`/`ev_sv`, always zero): the
  clean output contract (spec section 4) does not carry them. The differential
  reports these words explicitly as **UNMAPPED** and excludes them from the
  mismatch count.
- `flicker_mode` / `wdr_output_select`: no clean counterpart; only the detected
  `flicker_type` is consumed.
- `set_params()` does not populate the SDK result. The deployed core writes the
  selected line-table entry into the result on `UPDATE_AE_TABLE`; the clean
  set-param path is a pure state update. This only surfaces when the framework
  relies on the set result before the first run (see the report).

## Clean-core fixes made while building the differential

These are in `src/ae/ae_clean.c` and `include/ae_clean.h`, each
confirmed against the deployed object and justified by the spec:

1. The exposure line-table entry used integer `(idx*40)/1000` for the exponent;
   the object uses real 0.04·idx and truncates the per-mille value with a
   float-to-int convert. Fixed both.
2. Base line-table entries did not store `fno2`/`total_gain`.
3. Gain-range defaulting used `&&` where the object uses `||`.
4. The auto path defaulted `exp_comp_step` locally but did not write it back.
5. Manual exposure with automatic ISO forces the base analog gain (object
   warns); the clean path kept the manual gain.
6. The test ramp sets `ev_idx`/analog/digital gains and mirrors the setting into
   the last/current slots.
7. Flash bookkeeping accumulates the raw (pre-tolerance) error; `delta_idx`
   reports the raw error too (`ev_lv_adj` uses it), while the convergence step
   uses the post-tolerance value.
8. The short companion setting (`setting_short`) gained a `setting_short_curr`
   slot (delay-gated like the long setting) and the default-result initialiser
   now seeds it from the current setting, not the short one.

## Remaining divergences (clean-core, not shim mapping)

- `run/auto_bright`: at the low-index (maximum-exposure) saturation the final
  gain rebalance rounds total/digital gain 1 LSB differently (264/1056 vs
  263/1052); `ev_idx_expect` and `ev_lv_adj` sit at a negative derived index,
  whose `ev_lv_adj` read is before the line table (an out-of-bounds read whose
  value depends on the entity layout, like the warm-up convergence-row case).
- `run/wdr`: the WDR shutter quantisation produces a one-line different
  committed `sensor_exp_line` (33536 vs 33552) in the last/current slots and
  hence a short-companion difference; `wdr_ratio.sensor/tmp/last` also diverge.

These are the paths the private run plan §6 deferred (the expected-luminance and
exposure-compensation reads, full WDR and short-setting modelling); closing them
is an implementation-team task, not shim glue.

## Build / run

```
# shim host + ARM tests
make -C shim/ae check
make -C shim/ae run-arm

# SDK-boundary differential (self-contained; builds + runs + captures golden);
# run from the private analysis workspace.
```
