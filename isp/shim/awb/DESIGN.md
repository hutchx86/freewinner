<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# AWB shim - design

Integration/glue layer only. It does not implement AWB behaviour; it presents
the clean-room AWB core (`src/awb/awb_clean.c`) to the Yi/mediad ISP
framework through the framework's `fwi_awb_core_ops_t` vtable (the same
shape as the SDK's `isp_awb_core_ops_t`).

## Sources of truth

| Fact | File |
| --- | --- |
| Clean API (`awb_params_t`, `awb_stats_t`, `awb_result_t`, `awb_clean_tables_t`) | `include/awb_clean.h` |
| SDK contract (`awb_param_t`, `awb_stats_t`, `awb_result_t`, `isp_awb_core_ops_t`, `awb_init/awb_exit`) | SDK `.../libisp/include/isp_3a_awb.h` |
| SDK stats grid (`struct isp_awb_stats_s`) | SDK `.../libisp/include/isp_manage.h` |
| Vendor entity/config layout | the deployed `isp_3a_awb.o` (interface fact) |

The shim builds against the generated `fwi_*` headers (`include/fwi_isp_api.h`),
whose records are layout-identical to the SDK types listed above (the move off
the SDK-shaped header on 2026-09-26 left the ARM object code unchanged,
function by function). It uses the deployed v521 short-enum
ABI (`-fshort-enums -DISP521_RTOS_ALGO=1 -DISP_VERSION=521`). Compile-time
guards in the (private) differential confirmed the layout against the object:
`sizeof(isp_awb_setting_t) == 0x24`, `awb_ini` at +0x30, `awb_sensor_info` at
+0x123c, `test_cfg` at +0x12cc, `sizeof(struct isp_awb_stats_s) == 32768`.

## Name collision handling

The clean `awb_stats_t`/`awb_result_t` collide with the SDK typedefs and the
entry points share names. `awb_shim.c` renames the clean spellings for its
translation unit only; `awb_clean.c` is compiled separately with
`-Dawb_init=clean_awb_init ...` so the clean object exports only `clean_awb_*`.
No SDK or vendor file is modified.

## Entity layout

`awb_shim_entity_t` is heap-allocated by `awb_init` and returned as `void *`:

```c
typedef struct awb_shim_entity {
    awb_param_t    config;   /* SDK mirror at offset 0; get_params returns it */
    awb_entity_t  *clean;    /* clean-room instance                           */
} awb_shim_entity_t;
```

The deployed vendor entity also embeds its `awb_param_t` at offset 0
(the vendor AWB get-params step returns the entity pointer itself), so the mirror sits at
the same offset. The clean entity embeds its own `awb_params_t`; the shim
reaches it through `clean_awb_get_params()` and rewrites the mapped fields on
every set and every run, because the framework writes the mirror in place.

## Mapping: SDK -> clean

`awb_param_t` is the tuning/config corpus. `map_params()` copies it element by
element into `awb_params_t` (control block, sensor descriptor, test config and
the light/skin/special reference records, preset gains and favourite gains).
`clean_awb_set_params(AWB_PARAM_INIT)` then rebuilds the reference hierarchy
from the mapped corpus. The SDK `set_params` only rebuilds for the
`ISP_AWB_INI_DATA` tag; any other tag returns `-1` without touching the entity,
exactly as the vendor object does.

Statistics: clean `win[i].avg[0..2]` <- `isp_awb_stats_s.awb_avg_r/g/b`
(row-major 32x32), clean `win[i].npix` <- `awb_sum_cnt`.

Result write-back: clean `gain_out.{r,gr,gb,b}` -> `wb_gain_output`,
`color_temp_out` -> `color_temp_output`. The clean result is seeded with the
framework's current result before the run, so the clean core's gated early-outs
(start-up frames, lock, interval gate, manual mode, outlier abort) leave the
caller's gains and temperature untouched, matching the vendor AWB ISR mutating the
result in place.

With no statistics the deployed wrapper writes a fixed sensor-specific safe-gain
quadruple (not unity) and 6500 K when any channel is zero. The clean core
reproduces this from the injected table's `safe_gain`; no vendor rmm table
carries the quadruple, so the framework caller installs it (the built-in pilot
default is unity).

## SDK-boundary differential

The private AWB differential harness is self-contained: its own Makefile,
`diff_awb_shim.c`, a `run.sh`, an AWB-only table generator and the deployed math
helper objects. It links the deployed `isp_3a_awb.o` and the clean core through
the shim into one ARM binary, drives 18 scenarios (adaptive, lock, manual, every
preset branch, fixed temperature, start-up, both interval gates, night,
zero/saturated stats, no skin/special, null stats), compares the `awb_result_t`
and the stored parameter mirror, and re-runs one scenario with a perturbed clean
side to prove the pass is non-vacuous.

```
# run from the private analysis workspace
run.sh     # writes the withheld golden
```

## Divergences found and fixed

The differential exposed several clean-core divergences, all fixed against the
object and re-verified:

- the run gate ignored the `frame_id < 33` and `interval == 0` arms;
- the colour temperature was sampled after the preference/constraint steps
  instead of after smoothing;
- an aborted estimate still wrote gains (the object leaves the result);
- preset mode 10 was left at the adaptive gains instead of the unity ratios;
- skin/special references skipped the preset-record defaults;
- the no-statistics safe gains were unity instead of the object's constants.
