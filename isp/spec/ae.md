<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Auto-Exposure (AE) — Behaviour Specification

This document specifies observable behaviour only. It contains no source, no
vendor identifiers, and no reference string literals. Names used below are
neutral descriptions of the data and operations the module consumes and
produces. Exact ABI field names, offsets and the table-injection handle are
supplied by the compatibility layout header and by `interfaces.md`; the
implementation must use those names, not the neutral ones here.

---

## 1. Purpose and scope

The module drives a sensor's exposure and gain from per-frame image statistics
so that a chosen measure of scene brightness converges to a configured target.
On each frame it:

1. reduces the statistics block to a single weighted scene luminance,
2. compares that luminance with a target in the log2 domain to obtain an
   exposure error (in fractions of an EV),
3. maps the error through a per-scene convergence curve to a single exposure
   index step,
4. moves along a pre-built exposure index table ("line table") by that step and
   emits the resulting shutter time, total/analog/digital gains and aperture,
5. gates the emission of the new values according to a model of sensor latency.

It additionally performs backlight-scene classification (including a small
integer neural network), high-dynamic-range blend-ratio tracking, touch-region
metering, night-mode bias, flash exposure bookkeeping, and optional diagnostic
sweep tests. It does not perform white balance, autofocus, flicker detection or
tone mapping; those are supplied by sibling modules (the mains type it consumes
is produced by the auto-flicker module).

The module is stateful and per-frame. It produces at most one new exposure
setting per qualifying frame (subject to the interval and delay gates).

---

## 2. Conventions and symbols

- Arithmetic is 32-bit two's-complement integer unless a wider type or floating
  point is named. Signed overflow wraps; integer division truncates toward
  zero; right shifts of signed values are arithmetic.
- `|x|` is the absolute value. `clamp(x,lo,hi)` is `min(max(x,lo),hi)`.
- `Q4` means a fixed-point value scaled by 16; `Q8` by 256; `Q10` by 1024.
- `log2` below is the real base-2 logarithm; the module realises it through the
  injected log table (section 15.1).
- Dimension constants:
  - `NWIN = 384` metered windows in normal mode (16 rows x 24 columns),
  - `NWIN_WDR = 192` metered windows in HDR mode (8 x 24),
  - `NGRID = 64` cells in the 8x8 spatial weight grid,
  - `NHIST = 256` histogram bins and luma levels,
  - `NLUMA = 255` saturated luma level,
  - `EV_STEP = 40/1000` EV per exposure index (0.04 EV),
  - `IDX_MAX = 1024` entries per line table.
- Names in `monospace` are neutral names defined in this document.

---

## 3. Input contract

### 3.1 Invocation kinds

The module exposes init/exit, a get-parameters, a set-parameters, a per-frame
run and a per-frame interrupt entry point (names in `interfaces.md`). The
set-parameters entry point dispatches on a `param_kind` tag:

| Tag | Meaning | Action |
| --- | --- | --- |
| 0 | (re)initialise from the supplied configuration | Initialisation stage (section 6.0). |
| 1 | exposure tables were updated | Select a scene table and rebuild the line tables (section 14.3). |
| 2 | set an explicit exposure index | Configure the sensor for `line_index` (section 14). |
| 3 | touch region changed | Rebuild the touch metering grid (section 7.3). |
| other | — | Return `-1`, do nothing. |

The per-frame run entry point calls the interrupt handler when the object and
the statistics/result pointers are non-null; otherwise it returns `-1` and (if
a result pointer was given) applies the default-result initialiser.

### 3.2 Runtime settings

Read live each frame from the instance's parameter block.

| Neutral name | Meaning | Range / notes |
| --- | --- | --- |
| `ev_bias` | Requested exposure compensation | integers; effective range -4..+4, 0 = none (section 11). |
| `fixed_exposure` | Manual exposure time (microseconds) | used by manual and shutter-priority modes. |
| `sensor_gain` | Manual sensor gain (Q4) | used by manual/ISO-manual modes. |
| `iso_sensitivity` | Manual ISO value | 0..large; <100 triggers a default (section 14). |
| `aperture` | Selected aperture code | snapped to the aperture ladder (section 15.10). |
| `exposure_mode` | 0=auto, 1=manual, 2=shutter priority, 3=aperture priority | selects solver branch. |
| `iso_control` | 0=manual, 1=auto | manual branch adjusts gain from ISO. |
| `light_mode` | 0=normal, 1=high-light priority, 2=low-light priority | biases classification weights (section 8.6). |
| `metering_mode` | 0=average, 1=center, 2=spot, 3=matrix | selects the spatial weight grid (section 7.1). |
| `hdr_mode` | 0=normal, 1=WDR | selects the HDR luminance path and delay semantics. |
| `mains_detected` | 0=none, 1=50 Hz, 2=60 Hz | selects which line table is active (section 14.1). |
| `scene` | 0=preview, 1=capture, 2=video, 3..15 further scene modes | selects the scene table and the convergence row. |
| `flash_mode` | 0=off, 1=on, 2=torch, 3=auto, 4=red-eye, 5=none | ON/AUTO drive flash bookkeeping (section 12). |
| `meter_roi` | rectangle (x1,y1,x2,y2) in a 0..2000 coordinate space | touch/spot region (section 7.3). |
| `exposure_locked` | boolean | when non-zero, no exposure recomputation is performed. |
| `flash_open` | boolean | when 1, flash bookkeeping runs; else the cumulative EV is cleared. |
| `capture_stage` | integer stage counter | 1 and 3 latch pre/post flash luminance (section 12). |
| `exposure_cfg[14]` | integer tuning vector | fields 0..13 used as described below. |

`exposure_cfg` field map (all consumed by this module):

| Index | Role |
| --- | --- |
| 0 | Over-exposure luma ramp endpoint weight (section 7.1). |
| 1 | Under-exposure luma ramp endpoint weight (section 7.1). |
| 2 | Histogram high-bin weight for bright bins (section 7.2). |
| 3 | Histogram high-bin weight for dark bins (section 7.2). |
| 4 | Convergence row for scene modes other than capture/video. |
| 5 | Convergence row for capture scene. |
| 6 | Convergence row for video scene. |
| 7 | Convergence row for spot metering. |
| 8 | Error tolerance in exposure-index units (0 -> 3). |
| 9 | Target luminance (0 -> 128). |
| 10 | Dark-class bin weight low end (section 8.6). |
| 11 | Dark-class bin weight high end. |
| 12 | Neutral-class bin weight high end. |
| 13 | Neutral-class bin weight low end. |

Several settings fields present in the ABI (`mains_setting`, the
histogram-equalisation vector, the tone-mapping dynamic vector, the flash
switch flag and the capture flag) are **not consumed** by this module.

### 3.3 Static configuration

Read at initialisation and wherever noted.

| Neutral name | Meaning | Notes |
| --- | --- | --- |
| `table_source` | 0=fall back to the built-in descriptor, 1=use the scene tables (validated), 2=copy the last scene slot (index 15) into all | section 6.0. |
| `max_lv` | Highest light value (LV) for the exposure range | used for LV reporting (section 13.2). |
| `window_weight_seed[64]` | optional 8x8 metering grid | if it does not sum to zero it replaces the matrix grid (section 6.0). |
| `hist_metering_en` | non-zero = blend histogram + backlight centroid into luminance | section 7.4. |
| `kernel_index` | base row into the histogram kernel bank | selection for backlight peak finding (section 8.2). |
| `error_delay_frames` | hysteresis frames for acting on an error | section 10.2. |
| `exp_delay_frames` | sensor latency (frames) for shutter changes | section 6.3. |
| `gain_delay_frames` | sensor latency (frames) for analog-gain changes | section 6.3. |
| `ev_comp_step` | index step per unit of `ev_bias` (0 -> 4) | section 11. |
| `touch_distance_index` | row selector for the touch-distance weight table | clamped to 0..5 after absolute value (section 7.3). |
| `high_fps_handling_en` | non-zero enables the high-frame-rate table override | section 14.1. |
| `iso_to_gain_ratio` | ISO -> gain conversion factor | section 14.4. |
| `aperture_ladder[16]` | monotone ladder of aperture codes | folded to squared form by the solver (section 15.10). |
| `wdr_cfg[4]` | HDR configuration (base ratio, low/high thresholds, enable) | section 9. |
| `total_gain_range[2]` | [min,max] total gain | defaulted from the table descriptor. |
| `analog_gain_range[2]` | [min,max] analog gain (Q4) | defaulted from the sensor gain range. |
| `digital_gain_range[2]` | [min,max] digital gain (Q10) | defaulted to [1024,3072]. |
| `scene_tables[16]` | per-scene exposure descriptors | section 3.3.1. |
| `gain_split_ratio` | analog/digital balance (double) | <0.1 means "no split" (section 13.4). |

Fields such as `i_gain`, `blowout_pre_en`, `blowout_attr`, `gain_favor` and
`gain_range` are present but not consumed by this module.

#### 3.3.1 Exposure descriptor

A scene exposure descriptor consists of up to 10 segments, a segment count
`length`, an `ev_step` field and a `shutter_shift` field. `ev_step` is stored
but not consumed. Each segment has six fields:

```
segment := { min_exp, max_exp, min_gain, max_gain, min_iris, max_iris }
```

All are unsigned 32-bit. They are preprocessed in section 13.1.

### 3.4 Sensor descriptor

| Neutral name | Meaning | Notes |
| --- | --- | --- |
| `pclk` | Sensor pixel clock (Hz) | exposure/line conversion and frame-rate estimate. |
| `hts` | Horizontal total (pixels per line) | 0 is a fault; exposure conversion returns 0. |
| `vts` | Vertical total (lines per frame) | frame-rate estimate. |
| `frame_time` | Frame time used by the run-interval estimate | 0 handled specially. |
| `gain_min`, `gain_max` | Sensor gain limits (Q4) | defaults for the analog range; validation. |
| `sensor_width`, `sensor_height` | Active dimensions | per-block stat normalisation. |
| `hflip`, `vflip` | mirror flags | 1 mirrors the metering weight grid. |

All other sensor-descriptor fields are unused by this module.

### 3.5 Test/diagnostic configuration

| Neutral name | Meaning |
| --- | --- |
| `enable` | 0: the module runs an open-loop test ramp instead of AE. |
| `forced` | if <21, the initial exposure index is `ev_idx_max/2`; otherwise the initial index. |
| `test_gain`, `test_exp_line` | fixed values for the test branch. |
| `lum_forced` | forced target luminance when the forced flag is 1. |
| `test_exptime`, `exp_line_start/step/end`, `exp_change_interval` | exposure sweep parameters. |
| `test_gain_en`, `gain_start/step/end`, `gain_change_interval` | gain sweep parameters. |
| `delay_en` | non-zero runs the latency-measurement sweep (section 6.4). |
| `delay_type` | which physical quantity is swept (0..4). |

### 3.6 Statistics block

| Neutral name | Meaning | Shape |
| --- | --- | --- |
| `win_avg[]` | per-window average luma (8-bit) | 384 entries (used as 16x24 normal, 8x24 HDR). |
| `hist[]` | luma histogram | 256 bins. |
| `accum_r[]`, `accum_g[]`, `accum_b[]` | per-window colour sums for HDR long/short windows | 384 each: 0..191 long, 192..383 short. |
| `win_pix_n` | unused. | |

Values are hardware-dependent; `win_avg` and `hist` are treated as 0..255.
The backlight grid builder reads a flattened region beginning at `win_avg` and
continuing through `hist` with a fixed stride (section 8.4).

### 3.7 Injected tables

All constant data is supplied at runtime through the table-injection API
(section 15). A missing table handle is a programming error. See section 15 for
each table's shape, indexing and semantics.

---

## 4. Output contract

Written on every frame.

| Neutral name | Meaning | Units / range |
| --- | --- | --- |
| `status` | 0=idle, 1=busy, 2=done | busy/done compares last and current exposure index. |
| `setting` | the setting to be handed to the sensor for this frame | `{ exposure_time, sensor_exp_line, total_gain, analog_gain, digital_gain, f_number, fno2, lv, index }`. |
| `setting_last` | previous emitted setting | |
| `setting_curr` | the setting the sensor is actually using after delay gating | these are the fields the framework applies. |
| `setting_short` | short-exposure companion for HDR | same shape; exposure/line divided by the HDR ratio. |
| `idx_max` | highest valid exposure index for the active table | |
| `idx_expect` | ideal index if the full error were applied | may differ from the emitted index. |
| `bright_pos`, `dark_pos` | histogram peak positions from classification | 0..254. |
| `gain_ratio_out` | applied per-frame exposure ratio (Q8) | clamp 64..640. |
| `target` | effective target luminance after compensation | 0..255. |
| `avg_lum` | unweighted mean window luma | 0..255. |
| `weight_lum` | scene luminance used for the error | 0..255. |
| `delta_idx` | signed exposure error in index steps | section 10. |
| `lv_adj` | LV at an index offset from the current one | section 14.1. |
| `flash_ev_cumul` | cumulative EV applied while flash bookkeeping is open | section 12. |
| `wdr_ratio` | `{sensor, hw_ratio, tmp, last}` HDR blend ratio | 4 fields; section 9. |
| `wdr_hi_th`, `wdr_low_th` | HDR classification thresholds | section 9. |
| `hist_low`, `hist_mid`, `hist_hi` | histogram class fractions in parts per 10000 | section 9. |
| `backlight` | backlight classification value | 0..64 (0x40). |
| `gain_ratio` | copy of the configured gain split ratio | |

`status` is idle when the test/early-frame branches run, otherwise busy when
the last and current exposure index differ and done when they match.

---

## 5. Internal state and lifecycle

Allocated once per instance; opaque to the framework.

| Neutral name | Role |
| --- | --- |
| parameter block | the entire configuration above (also returned by get-parameters). |
| `line_base[1024]`, `line_50[1024]`, `line_60[1024]` | the unconstrained and two flicker-aligned exposure index tables. |
| `line_active` | pointer to the currently selected line table. |
| `tbl_preview`, `tbl_capture`, `tbl_video` | selected scene descriptors. |
| `interval_frame` | frames between exposure recomputations (section 6.2). |
| `latency_count` | sensor-latency countdown (section 6.3). |
| `lum_change_cnt` | consecutive frames with a significant error (section 10.2). |
| `wdr_change_cnt` | consecutive frames with a changing HDR ratio (section 9). |
| `hist_count[32]`, `hist_mean[32]` | collapsed histogram blocks (section 8.1). |
| `hist_conv[62]` | convolution output (section 8.2). |
| `peak_pos[8]`, `peak_val[8]` | found peak positions/values. |
| `bin_weight[3]` | dark/neutral/bright class weights (section 8.6). |
| `wgrid_matrix`, `wgrid_avg`, `wgrid_center`, `wgrid_touch[64]` | spatial metering grids (section 7.1). |
| `flash_before_lum`, `flash_after_lum`, `flash_expect_lum`, `flash_ev_accum` | flash bookkeeping (section 12). |
| `frame_rate_max` | estimated sensor frame rate (section 6.0). |
| `busy_flag` | set to 1 at allocation; otherwise unused. |
| diagnostic sweep state | frame counter, sweep value/direction, luma history (section 6.4). |

Lifecycle:

- **Allocation:** zeroed; `busy_flag=1`; `bin_weight={8,8,8}`; `latency_count=0`;
  flash accumulator zero; the matrix/average/center grids are seeded from the
  injected defaults; target compensation zero.
- **Initialisation (tag 0):** estimate frame rate, choose/validate/seed the
  scene descriptors, optionally replace the matrix grid, default the aperture
  ladder and scene tables (section 6.0).
- **Table update (tag 1):** rebuild the line tables and pick a starting index.
- **Per frame:** consume statistics, compute outputs, advance counters.
- **Teardown:** free the instance.

---

## 6. Per-frame control loop

### 6.0 Initialisation stage (tag 0)

1. `frame_rate_max = round(pclk / (hts * vts))` where `round(x) = (16x + 8)>>4`
   after scaling by 16 (equivalently, nearest integer to `pclk/(hts*vts)`).
2. Select scene descriptors:
   - `table_source == 1`: validate capture, then preview, then video against
     section 6.0.1; on success keep them.
   - `table_source == 2`: copy scene slot 15 into capture, preview and video;
     if its `length != 0`, keep them.
   - Otherwise (fallback): copy the injected default descriptor into preview,
     capture and video, then set preview = capture.
3. If `window_weight_seed` sums to non-zero, copy it over the matrix grid.
4. If the first scene table's first segment has `max_exp == 0`, seed scene
   slots 0=preview, 1=capture, 2=video from the selected descriptors.
5. If `aperture_ladder[0] == 0`, copy the injected default ladder.

#### 6.0.1 Descriptor validation

For the chosen scene slot: copy `length` and the segments. Reject if any
segment has `min_exp == 0`, `min_gain == 0` or `max_gain == 0`. Reject if, for
any segment, `max(min_gain, max_gain) > (gain_max << 8)`.

### 6.1 Entry dispatch (interrupt handler)

```
if test.enable == 0:
    apply_test_ramp()                 # open-loop test; status = idle
elif frame_index < 3:
    configure_sensor(setting_curr.index)   # warm-up; status = idle
    backlight = 0x20
else:
    run_mode()                       # section 6.2
    status = (setting_last.index == setting.index) ? done : busy
    if test.delay_en != 0:
        apply_test_ramp(); status = idle; run_delay_sweep()
finish_result()                      # section 6.3/6.4
```

Frames 0,1,2 only emit the current setting and churn the counters; they do not
compute a new exposure.

### 6.2 Run mode (recompute gate)

```
if latency_count >= 0: return            # a sensor change is still settling

ft = sensor.frame_time
if ft == 0:      n = 1
else:
    n = (ft/2 + 15) / ft
    if n < 1: n = 1
    if n > 2: n = 3
if n <= error_delay_frames: n = error_delay_frames
interval_frame = n

k = frame_index - 3
if (k == interval_frame * (k / interval_frame)) and not exposure_locked:
    if exposure_mode == manual:
        configure_manual()
    else:
        gain_ratio_out = compute_exposure_from_table()   # sections 7-14
```

So a new exposure is computed only when the frame counter is a multiple of the
interval after warm-up, no sensor change is pending, and exposure is unlocked.

### 6.3 Delay gating (`finish_result`)

Committed to `setting_curr` according to `latency_count`:

```
if test.delay_en != 0:
    commit exp_line, exposure_time, analog_gain (long and short) immediately

if latency_count == 2:            commit digital_gain (long and short)
if latency_count == gain_delay_frames: commit analog_gain (long and short)
if latency_count == exp_delay_frames:
    commit exp_line, exposure_time (long and short) and wdr_ratio.sensor
always: commit total_gain, index, lv
latency_count -= 1
```

`setting_last` is updated when a new exposure is configured (section 14).

### 6.4 Diagnostic delay sweep

When `test.delay_en != 0` the test ramp is applied every frame and a latency
sweep runs: the current frame's luma sum is recorded in a 10-entry ring; a
local extremum in the three-sample history records how many frames the sensor
took to respond; the swept quantity (short exposure, long exposure line, analog
gain, ISP HDR ratio, or digital gain, per `delay_type`) is moved up or down in
fixed steps and reversed at its bounds. This branch replaces normal AE and is
not part of the core exposure contract.

---

## 7. Luminance acquisition and metering

### 7.1 Spatial weighted average (`lum_avg_q8`)

Choose the 8x8 grid `G` by metering mode:

| Metering | Grid |
| --- | --- |
| spot | touch grid (section 7.3) |
| average | injected average grid |
| center | injected center grid |
| matrix (and any other) | injected matrix grid, possibly replaced by `window_weight_seed` |

The grid is row-major `row*8+col`. If `hflip == 1` reverse the 8 columns within
each row; if `vflip == 1` reverse the 8 rows.

The number of windows is `NWIN` (384) for normal mode and `NWIN_WDR` (192) for
WDR mode. For each window `i`:

```
w   = G[ (i mod 24)/3 + ((i/24)/row_expand)*8 ]     row_expand = 2 (normal) or 1 (WDR)
lum = win_avg[i]
lum_sum_q8 += lum * 256
```

Then weight contributions:

```
if metering == spot  or  (9 < lum < 236):            # "normal" window
    term = lum * w * 256
    weight_sum += w
elif lum <= 9:                                        # deep shadow
    iso = WGHT_UNDER[ (i mod 24)/3 + ((i/24)/row_expand)*8 ]
    f   = interp(lum, 0, 10, exposure_cfg[1], 256)
    term = lum * f * iso * w
    n   = iso * exposure_cfg[1] * w
    if n < 0: n += 255
    weight_sum += n >> 8
else:                                                 # highlight (lum >= 236)
    iso = WGHT_OVER[ (i mod 24)/3 + ((i/24)/row_expand)*8 ]
    f   = interp(lum, 235, 255, 256, exposure_cfg[0])
    term = lum * f * iso * w
    n   = iso * exposure_cfg[0] * w
    if n < 0: n += 255
    weight_sum += n >> 8

term_sum_q8 += term
```

where `interp(x,x0,x1,y0,y1) = y0 + (y1-y0)*(x-x0)/(x1-x0)` and returns `y0`
when `x1 == x0`. Finally:

```
lum_avg_q8      = (lum_sum_q8 + total/2) / total      # Q8, unweighted; total = NWIN or NWIN_WDR
avg_lum         = lum_avg_q8 >> 8                     # written to the result
weighted_lum_q8 = (term_sum_q8 + weight_sum/2) / weight_sum   # Q8, or 0 if weight_sum==0
```

`avg_lum` is written to the result; `weighted_lum_q8` is returned to the
caller.

### 7.2 Histogram weighted luminance (`hist_lum_q8`)

Per-bin weights:

```
w[i] = interp(i, 0,   5,   exposure_cfg[3], 256)   for i < 5
w[i] = 256                                          for 5 <= i <= 230
w[i] = interp(i, 230, 255, 256, exposure_cfg[2])   for i > 230
```

Then `hist_lum_q8 = (sum_i i*hist[i]*w[i] * 256) / sum_i hist[i]*w[i]`, or 0
when the denominator is 0.

### 7.3 Touch (spot) metering grid

From `meter_roi` (x1,y1,x2,y2) in a 0..2000 space:

```
cx = ((x1+x2)/2 * 7 + 8000) / 2000        # centre column, Q3
cy = ((y1+y2)/2 * 7 + 8000) / 2000        # centre row
rx = ((x2-x1)/2 * 7 + 1000) / 2000        # half-width
ry = ((y2-y1)/2 * 7 + 1000) / 2000        # half-height
cx,cy = clamp(cx,0,7), clamp(cy,0,7)
rx,ry = clamp(rx,0,3), clamp(ry,0,3)
```

For each grid cell `(row=j, col=i)` the distance outside the ellipse is:

```
ax = |i - cx|; ay = |j - cy|; dx = ax - rx; dy = ay - ry
if ax > rx:
    d = ax - rx
    if ay > ry: d = isqrt(d*d + dy*dy)     # Euclidean outside corner
else:
    d = (ay > ry) ? |dy| : 0
d = clamp(d, 0, 11)
k = clamp(|touch_distance_index|, 0, 5)     # values >4 map to 5
grid[row*8 + col] = TOUCHPROB[k*12 + d]
```

The touch grid is rebuilt only on set-parameters tag 3. It is used as the
spatial grid when metering is spot (and spot bypasses the shadow/highlight
re-weighting of section 7.1).

### 7.4 Scene luminance used for the error

```
tolerance = exposure_cfg[8]; if 0 -> 3                 # section 10.1

if hdr_mode == WDR:
    update_hdr_ratio()                                 # section 9
    weight_lum = (lum_avg_q8 + (lum_avg_q8<0 ? 255 : 0)) >> 8
elif hist_metering_en != 0 and metering != spot:
    h  = hist_lum_q8
    bl = backlight_centroid()                          # section 8.7
    t  = h + bl + 2*lum_avg_q8
    if t < 0: t += 1023
    weight_lum = t >> 10
else:
    weight_lum = (lum_avg_q8 + (lum_avg_q8<0 ? 255 : 0)) >> 8

result.weight_lum = weight_lum
```

---

## 8. Backlight scene classification

Runs only in the non-WDR histogram-blend path. It produces a `backlight` value
(0..64), the peak positions `dark_pos`/`bright_pos`, and the class bin weights,
and returns the probability-weighted histogram centroid in Q8.

### 8.1 Histogram collapse to 32 blocks

For `i = 0..31`, over bins `i*8 .. i*8+7`:

```
count[i] = sum h
mean[i]  = (count[i] == 0) ? i*8 + 4 : (sum bin*h) / count[i]
```

### 8.2 Adaptive convolution and peak search

Repeat for `attempt = 1, 2, ...`:

```
row = clamp(attempt + kernel_index, 0, 19)
conv = correlate(count[0..31], KERNEL[row][0..30])    # length 32+31-1 = 62, Q10
```

`correlate(u[m], v[n])`: output `w` has `m+n-1` entries; `w[k]` accumulates
`(u[j]*v[k-j])>>10` for `j` in the valid overlap, starting from the existing
value (the caller zeroes it). Then scan output indices and record local maxima:
a peak at output index `k` when `conv[k-1] <= conv[k]` and `conv[k+1] < conv[k]`.
Up to 8 peaks are recorded; each peak's block position is `mean[k-1-14]` when
`k-1-14` is in 0..31, else 0.

```
if peaks == 2:
    pa = clamp(pos0 - 1, 0, 255); pb = clamp(pos1 - 1, 0, 255)
    order so dark_pos <= bright_pos, with matching values
    break
if peaks == 0: dark_pos = bright_pos = 0; dark_val = bright_val = 0; break
if peaks == 1 or (19 - kernel_index) <= attempt:
    dark_pos = bright_pos = clamp(pos0 - 1, 0, 255)
    dark_val = bright_val = val0
    break
```

`bright_pos` and `dark_pos` are written to the result.

### 8.3 Class thresholds and midpoint

```
neutral_th = clamp(|253 - bright_pos| / 2, 10, 95)
if dark_val == bright_val:
    dark_th = bright_th = 35
    mid = (dark_pos + bright_pos) >> 1
else:
    mid     = (dark_pos*dark_val + bright_pos*bright_val) / (dark_val + bright_val)
    dark_th   = clamp(|mid - dark_pos| / 2, 10, 95)
    bright_th = clamp(|bright_pos - mid| / 2, 10, 95)
```

### 8.4 Spatial grid for classification

64 cells (8x8). Each cell averages 24 samples read from a flattened region
starting at `win_avg`, using `cell(row,col)`:

```
acc = 0
for j in 0..3: for p in 0..5:
    idx = (i>>3)*96 + j*24 + (i&7)*6 + p
    acc += luma_at(idx)          # win_avg[idx] for idx<384,
                                 # hist[idx-384] for 384<=idx<640, else 0
win_avg_grid[i] = acc / 24
```

This fixed addressing spans into the histogram region and past it; it must be
reproduced exactly.

### 8.5 Classification

```
if (unsigned)(bright_pos - dark_pos) < 101:
    if (unsigned)(bright_pos - dark_pos) < 21:
        backlight = 32
    else:
        # pattern match against the three reference masks
        mask[i] = (mid <= grid[i]) ? 1 : 0
        for p in 0..2:
            dist[p] = sum_{i=1..63} |mask[i] - BLMASK[p][i]|
        lo = argmin(dist); hi = argmax(dist)
        comp   = |64 - dist[lo]|
        picked = dist[lo]
        if dist[hi] <= comp: picked = dist[hi]
        picked -= night_mode_weight()
        backlight = max(picked, 0)
else:
    # two-layer integer network
    x[i] = (grid[i] < mid) ? -1 : 1            # 64 inputs
    for j in 0..9:
        h[j] = (sum_i x[i]*NET_IN[j*64+i] + NET_BIAS[j]) / 100
    s0 = (sum_j h[j]*NET_OUT[j])      / 100 + 3561
    s1 = (sum_j h[j]*NET_OUT[10+j])   / 100 + 6438
    if s0 < s1:
        backlight = clamp(40 - s1/800, 0, 32)
    else:
        backlight = clamp(20 + s0/800, 32, 64)

backlight = min(backlight, 64)
result.backlight = backlight
```

Note the pattern-match distance loop starts at cell 1 (cell 0 is excluded).
Division truncates toward zero.

### 8.6 Class bin weights

With `e10..e13 = exposure_cfg[10..13]`:

```
t = backlight * (e11 - e10);     if t < 0: t += 63
w0 = e10 + (t >> 6)
w1 = (dark_pos != bright_pos) ? 10 : 0
t = backlight * (e13 - e12);     if t < 0: t += 63
w2 = e13 - (t >> 6)
if light_mode == high_light_priority: w1 += 16
elif light_mode == low_light_priority: w0 += 16
```

### 8.7 Probability-weighted centroid

For each histogram bin `i`:

```
dd = |i - dark_pos|; db = |i - bright_pos|; dn = |i - 253|
pd = AUXPROB[dark_th   * 256 + dd]
pb = AUXPROB[bright_th * 256 + db]
pn = AUXPROB[neutral_th* 256 + dn]
term = hist[i] * (pd*w0 + pb*w1 + pn*w2)
num += i*term; den += term
return den == 0 ? 0 : (num * 256) / den
```

`AUXPROB` is the 96x256 probability table shared with the white-balance module
(see `interfaces.md`); it is indexed by threshold row (10..95) and pixel
distance column (0..255).

---

## 9. HDR / WDR blend ratio

Called first in the WDR luminance path.

```
ratio = wdr_ratio.tmp; if ratio == 0: ratio = 256
blk = (sensor_height * sensor_width) / 192 + 1
for i in 0..191:  long[i]  = clamp(((r[i]+g[i])*3 + b[i]) / blk, 0, 255)
for i in 0..191:  short[i] = clamp(((r[192+i]+g[192+i])*3 + b[192+i]) / blk, 0, 255)
for i in 0..191:  h[clamp(long[i]+short[i],0,255)] += 1
hist_low = count(h[0..9]); hist_mid = count(h[10..245]); hist_hi = count(h[246..255])

f_low = clamp(hist_low / 192, 0.01, 1.0); result.hist_low = (u16)(f_low*10000)
f_mid = clamp(hist_mid / 192, 0.01, 1.0); result.hist_mid = (u16)(f_mid*10000)
f_hi  = clamp(hist_hi  / 192, 0.01, 1.0); result.hist_hi  = (u16)(f_hi *10000)
```

Target ratio from the bright-histogram fraction:

```
tgt  = clamp(interp(result.hist_hi, 100, 9300, 160, 304), 160, 304)
adiff = |ratio - tgt|
wdr_ratio.tmp = ratio                           # no adjust: hold the incoming ratio
if adiff < 5:
    wdr_change_cnt = 0
else:
    wdr_change_cnt += 1
    if wdr_change_cnt > 3:
        d   = min(|ratio - tgt|, 127)
        adj = CONV[15*128 + d]                  # last convergence row
        wdr_ratio.tmp = ratio + (tgt < ratio ? -adj : adj)

wdr_ratio.last = ratio
if wdr_cfg[3] == 0:                             # manual HDR ratio
    wdr_ratio.tmp = wdr_cfg[0] << 4
    wdr_change_cnt = 0
```

Post-processing (in `finish_result`):

```
if hdr_mode == WDR:
    if latency_count == 2 and test.delay_en == 0: wdr_ratio.hw_ratio = tmp
    if frame_index < 11:
        v = wdr_cfg[0] << 4
        wdr_ratio.tmp = wdr_ratio.hw_ratio = wdr_ratio.sensor = v
    wdr_hi_th  = wdr_cfg[2] << 8
    wdr_low_th = wdr_cfg[1] << 8
    setting_short = setting
    div = max(wdr_ratio.sensor >> 4, 1)
    setting_short.exposure_time  /= div
    setting_short.sensor_exp_line /= div
```

The WDR output-selection setting is not consumed by this module.

---

## 10. Error signal, tolerance and convergence

### 10.1 Log2-domain error

Clamp the measured luminance and target to 1..255:

```
L = clamp(weight_lum, 1, 255)
T = clamp(target,     1, 255)
delta_idx = (20 - LOG2[L] + LOG2[T]) / 40
```

With `LOG2[x] = round(1000*log2(x+1))`, this is (to within integer rounding)
25 steps per EV, i.e. `delta_idx ~= 25*log2((T+1)/(L+1)) + 0.5`. Positive means
the scene is darker than target and exposure must increase.

### 10.2 Tolerance and hysteresis

```
tolerance = exposure_cfg[8]; if 0 -> 3
if frame_index < 80:
    tolerance = 2*tolerance - (tolerance*frame_index)/80       # looser at start
if |delta_idx| < tolerance:
    delta_idx = 0; lum_change_cnt = 0
else:
    lum_change_cnt += 1
    if lum_change_cnt < error_delay_frames: delta_idx = 0
```

So small errors are ignored entirely (and reset the persistence counter), and a
larger error must persist for `error_delay_frames` consecutive computations before
it is acted upon. The tolerance starts larger (up to 2x) and tightens over the
first 80 frames.

### 10.3 Convergence curve row and step

```
if metering == spot:
    row = exposure_cfg[7]
else:
    row = scene == capture ? exposure_cfg[5]
        : scene == video   ? exposure_cfg[6]
        :                    exposure_cfg[4]
    row = clamp(row, 0, 31)                     # 5-bit saturation
if frame_index < 80:
    row += (frame_index*8)/80 - 8               # up to -8 during warm-up
k   = clamp(|delta_idx|, 0, 127)
step = CONV[row*128 + k]
if delta_idx < 0: step = -step
```

Row 0 of the convergence table is the identity (step = error). Higher rows
compress the step more (slower convergence). The warm-up offset makes the
response faster at start. The row is used without a lower clamp after the
warm-up offset (see Open questions).

### 10.4 Applied index and reported ratio

```
ev_idx       = setting.index
maxidx       = idx_max - 1
idx_expect   = sat_unsigned_clamp(ev_idx + delta_idx, maxidx)
new_idx      = clamp(ev_idx + step, 0, maxidx)
configure_sensor(new_idx)                        # section 14

if new_idx == 0 or new_idx == maxidx:
    gain_ratio_out = 256
elif step < 0:
    gain_ratio_out = 256000 / EVTAB[-step]
else:
    gain_ratio_out = (EVTAB[step] << 8) / 1000
gain_ratio_out = clamp(gain_ratio_out, 64, 640)
```

`sat_unsigned_clamp(x, hi)` compares `x` and `hi` as unsigned 32-bit values and
returns `hi` when `x` is greater. The ideal index and `maxidx` are therefore
both treated as unsigned, so an ideal index that would be negative is "greater"
than `maxidx` and reports `maxidx`; only an in-range ideal index survives.

`EVTAB` is the per-index EV scale table written during line-table construction
(section 15.2); for a valid index it equals `round(1000*2^(0.04*index))`, so the
ratio is the Q8 factor by which exposure changed this frame.

---

## 11. Targets and compensation

```
target = exposure_cfg[9]
if ev_bias in {-4..4} and ev_bias != 0:
    s = ev_comp_step; if s == 0: s = 4
    if ev_bias > 0: target = (EVTAB[ ev_bias*s] * target) / 1000
    else:           target = (target * 1000) / EVTAB[-ev_bias*s]
if test.forced == 1: target = lum_forced
if target == 0: target = 128
```

`EVTAB[k] = round(1000*2^(0.04*k))`, so each compensation unit (with the
default step 4) scales the target by `2^(0.16*ev_bias)`.

Target compensation subtracts a runtime offset, but only downward:

```
if target_comp > 0: target -= target_comp
target = clamp(target, 0, original_target)
result.target = target
```

---

## 12. Flash tracking

Active when the flash mode (with the torch bit removed) equals "on", i.e. modes
1 and 3:

```
if exposure_mode == manual: (no flash bookkeeping on this path)
if capture_stage == 1: flash_before_lum = avg_lum
if capture_stage == 3: flash_after_lum  = avg_lum
flash_ev_accum += delta_idx
result.flash_ev_cumul = flash_ev_accum
```

When flash bookkeeping is not active, `flash_ev_accum` is cleared each frame.
A separate externally callable helper computes the flash strength ratio:

```
d = flash_after_lum - flash_before_lum
ratio = (d < 0) ? 10000 : (flash_expect_lum - flash_before_lum) * 100 / d
```

`flash_expect_lum` is provided by the caller (it is not derived from the
statistics). This helper is not invoked by the per-frame entry point.

---

## 13. Exposure line-table construction

`build_line_tables(descriptor)` fills the three 1024-entry line tables
(`line_base`, `line_50`, `line_60`) and the EV scale table, and records
`idx_max`. It also rewrites the analog/digital gain ranges if they are zero.

### 13.1 Segment preprocessing

Copy the descriptor's segments. Let `scale = 1000000 * (shutter_shift > 1 ?
shutter_shift : 1)`. For each segment `i < length`:

```
max_exp  = scale / max_exp          # reciprocal exposure
min_exp  = scale / min_exp          # (division by zero is a caller fault)
min_iris = min_iris * min_iris
max_iris = max_iris * max_iris
ev_lo[i] = (min_exp * min_gain * 10000) / min_iris
ev_hi[i] = (max_exp * max_gain * 10000) / max_iris
```

Gain defaults if `analog_gain_range` is {0,0}:
`analog = {gain_min<<4, gain_max<<4}`. If `digital_gain_range` is {0,0}:
`digital = {1024, 3072}`. Record
`total_gain_range = {segments[0].min_gain, segments[length-1].max_gain}` and put
the configured `gain_split_ratio` into the result.

### 13.2 EV curve and index map

For `index = 0..1023`:

```
dv   = 1000 * 2^(index * 40 / 1000)
EVTAB[index] = min(round(dv), 0xffffffff)         # per-mille EV scale
ev   = ev_lo[0] * EVTAB[index] / 1000             # EV at this index
if length > 0 and ev > ev_hi[length-1]: break     # past the top segment
```

`EVTAB[index]` is the factor `2^(0.04*index)`; the index step is 0.04 EV.
Because `EVTAB` is written in place, later users (compensation, gain ratio)
read the values produced here.

### 13.3 Segment solve

Find the first segment `i` with `ev_lo[i] <= ev < ev_hi[i]`. Using the
preprocessed fields, derive exposure time, total gain and squared aperture
(`fno2`) per the segment's pinned fields:

```
if min_exp == max_exp:                       # exposure pinned
    if min_iris == max_iris:                 # iris pinned
        if min_gain == max_gain:             # everything pinned
            exp_time   = (min_iris * ev) / 10000 / min_gain
            total_gain = min_gain
            fno2       = min_iris
        else:                                # gain free
            exp_time   = min_exp
            total_gain = (max_iris * ev) / 10000 / min_exp
            fno2       = min_iris
    else:                                    # iris free
        exp_time   = min_exp
        total_gain = min_gain
        fno2       = (min_gain * min_exp * 10000) / ev
else:                                        # exposure free
    exp_time   = (min_iris * ev) / 10000 / min_gain
    total_gain = min_gain
    fno2       = min_iris
```

If no segment matches, the entry is left zero and skipped.

### 13.4 Analog/digital gain split

```
if gain_split_ratio < 0.1:
    ag = ((fno2 * ev / 10000) / exp_time) & ~0xf
    analog_gain = clamp(ag, analog_lo, analog_hi)
else:
    dg = isqrt((total_gain << 10) / gain_split_ratio)
    digital_gain = clamp(dg, digital_lo, digital_hi)
    ag = ((total_gain << 10) / digital_gain) & ~0xf
    analog_gain = clamp(ag, analog_lo, analog_hi)

# always recompute digital from the analog result
q  = ev / exp_time
dg = (((q * fno2) << 10) / 10000) / analog_gain
digital_gain = clamp(dg, digital_lo, digital_hi)
```

Analog gain is Q4 (low nibble cleared); digital gain is Q10.

### 13.5 Per-entry scalars, rounded line count, final gain rebalance

```
f_number          = isqrt(fno2)
sensor_exp_line   = time2line(exp_time, round_up=false)     # section 13.8
this_ev           = ev
true_exp_line     = (((exp_time * pclk) << 4) / 1000000 + hts/2) / hts  # Q4 lines, rounded
if true_exp_line == 0: true_exp_line = 1
if true_exp_line > 160000000: true_exp_line = 160000000

total_gain = (total_gain * true_exp_line) / sensor_exp_line
t = total_gain << 10
ag = (t / digital_gain) & ~0xf
analog_gain  = clamp(ag, analog_lo, analog_hi)
digital_gain = clamp(t / analog_gain, digital_lo, digital_hi)
```

`sensor_exp_line` rounds the line count to the nearest 1/16 line and then masks
to a whole line; `true_exp_line` rounds to the nearest whole line without the
mask. Their difference is folded back into the gains by the final rebalance.
Reproducing the `hts/2` rounding in `true_exp_line` matters at low indices,
where it changes the rebalanced total gain by one.

### 13.6 Flicker-aligned tables

If the entry's exposure time is non-zero, build a 50 Hz entry and a 60 Hz entry
from the unconstrained entry:

```
align(ceiling_us, target_table):
    dst = target_table[index]
    dst.ev = src.ev
    exp = src.exposure_time
    if ceiling_us < exp: exp = ceiling_us * (exp / ceiling_us)   # floor to multiple
    dst.sensor_exp_line = time2line(exp, false)
    dst.exposure_time   = line2time(dst.sensor_exp_line)
    dst.f_number = src.f_number
    dst.fno2     = nearest_aperture(dst)          # section 15.10
    g = (dst.fno2 * dst.ev) / 10000 / dst.exposure_time
    g = clamp(g, total_gain_lo, total_gain_hi)
    dst.total_gain = g
    # analog/digital split exactly as 13.4 (using g)
    # then recompute digital: fe = fno2*(ev/exposure_time); dg = (fe<<10)/10000/analog;
    #   digital = clamp(dg, digital_lo, digital_hi)

  50 Hz table: ceiling = 10000 us
  60 Hz table: ceiling = 8333 us
```

Both tables store `index` in their `ev_idx` field, and all three tables receive
`lv = clamp(max_lv - index*4, 0, 3000)`. `idx_max` is set to the last index that
had a non-zero exposure time. The smallest digital gain seen across all three
tables is tracked.

### 13.7 Digital-gain normalisation

After the sweep, for every index `0 .. idx_max-1` in each table:

```
digital_gain = (digital_gain << 10) / min_digital_gain_seen
```

### 13.8 Exposure/line/time conversions

```
time2line(time, round_up):
    if hts == 0: return 0
    l = ((time * pclk) << 4) / 1000000
    b = (l + hts/2) / hts
    if b == 0: b = 1
    if b > 160000000: b = 160000000
    if b > 16:                        # 32-bit high word non-zero or low word >16
        if round_up: b += 8
        return b & 0xfffffff0         # round to a multiple of 16
    return b                          # Q4 line count

line2time(line):
    return (int)(round( (line/16.0) * 1000000 * hts / pclk ))   # 0 if pclk==0
```

`time2line` returns a Q4 line count (line = value/16): the mask clears the low
4 bits, and values above 16 lines are rounded to whole lines. `line2time` is
its inverse in microseconds.

### 13.9 Aperture snapping

```
nearest_aperture(setting):
    f = setting.f_number
    for i in 0..14:
        if ladder[i] <= f < ladder[i+1]:
            setting.f_number = ladder[i]
            return ladder[i]*ladder[i]
    return setting.fno2        # unchanged when out of ladder range
```

### 13.10 Table selection

`set_line_table` selects the descriptor by scene:

```
scene < 16 ? scene_tables[scene] : scene_tables[2]     # video fallback
build_line_tables(selected)
```

Starting setting:

```
if setting_curr.sensor_exp_line == 0:                  # first time
    i = test.forced; if i < 21: i = idx_max / 2
    setting = setting_curr = setting_last = line_base[i]
else:
    i = setting_curr.index
    setting = setting_curr = setting_last = line_base[i]
```

---

## 14. Per-mode exposure configuration

`configure_sensor(index)` selects the active line table, snapshots the previous
setting, loads the new entry, and then applies mode-specific overrides.

### 14.1 Active table

```
if mains_detected == 0:   active = line_base
elif mains_detected == 2: active = line_60
else:                     active = line_50
if high_fps_handling_en == 1 and frame_rate_max > 58
       and (idx_max - setting_last.index) < 50:
    active = line_base                       # flicker tables unusable at high fps

setting_last = setting
setting      = active[index]
j = delta_idx/3 + index                 # arithmetic; may be negative
if j >= idx_max: j = idx_max            # only the upper bound is clamped
lv_adj       = (j < 0) ? 0 : active[j].lv
```

There is no lower clamp on `j`. When it is negative the deployed object reads an
entry before the active table; in that layout the preceding word is zero, and
the observed boundary value is `lv_adj = 0`. A conforming implementation reports
zero for that case rather than substituting row 0's LV.

`delta_idx/3` truncates toward zero.

### 14.2 Shutter priority (exposure_mode == 2)

```
fe = fno2 * ev
et = fe / 2560000
et = max(et, active[0].exposure_time)        # never below the shortest
et = min(et, fixed_exposure)                 # not above the manual cap
setting.exposure_time  = et
setting.sensor_exp_line = time2line(et, round_up=true)
setting.exposure_time  = line2time(setting.sensor_exp_line)
tg = clamp((fe / 10000) / setting.exposure_time, total_gain_lo, total_gain_hi)
setting.total_gain = tg
dg = clamp(isqrt((tg << 10) * gain_split_ratio), digital_lo, digital_hi)
setting.digital_gain = dg
ag = clamp(((tg << 10) / dg) & ~0xf, analog_lo, analog_hi)
setting.analog_gain = ag
dg = (((fno2 * (ev / setting.exposure_time)) * 256) / 10000) / ag
setting.digital_gain = clamp(dg, digital_lo, digital_hi)
```

### 14.3 Aperture priority (exposure_mode == 3)

```
setting.f_number = aperture
setting.fno2     = nearest_aperture(setting)
fe  = ev * fno2
tmp = fe / 10000
expo = tmp / setting.analog_gain
setting.sensor_exp_line = time2line(expo, round_up=true)
setting.exposure_time   = line2time(setting.sensor_exp_line)
tg = clamp(tmp / setting.exposure_time, total_gain_lo, total_gain_hi)
setting.total_gain = tg
dg = clamp(isqrt((tg << 10) * gain_split_ratio), digital_lo, digital_hi)
setting.digital_gain = dg
ag = clamp(((tg << 10) / dg) & ~0xf, analog_lo, analog_hi)
setting.analog_gain = ag
dg = (((fno2 * (ev / setting.exposure_time)) * 1024) / 10000) / ag
setting.digital_gain = clamp(dg, digital_lo, digital_hi)
```

(Note the different numerator scale, 256 vs 1024, between the two priority
branches; reproduce exactly.)

### 14.4 Manual ISO (iso_control == manual)

```
iso = iso_sensitivity
if iso < 100:
    if sensor_gain == 0: iso_sensitivity = 100; sensor_gain = 16
else:
    v = (iso_to_gain_ratio * iso) / 100
    if v < 16: v = 16
    sensor_gain = (v < gain_max) ? v : gain_max

v = (ev * 256) / (active[0].ev ? active[0].ev : 1)
if v < 256: v = 256
analog_gain = min(sensor_gain << 4, v)

denom = (fno2 * ev / 10000) / analog_gain
denom = max(denom, active[0].exposure_time)
denom = min(denom, active[idx_max-1].exposure_time)
setting.exposure_time   = denom
setting.sensor_exp_line = time2line(denom, round_up=true)
setting.digital_gain    = 1024
```

### 14.5 WDR exposure alignment and delay arming

```
delay = max(exp_delay_frames, gain_delay_frames)
arm   = max(delay, 2)

if hdr_mode == WDR:
    w = wdr_cfg[0] * 16
    if w == 0: w = 16
    old_exp = setting.exposure_time
    v = w * (setting.sensor_exp_line / w)
    if v == 0: v = w
    setting.sensor_exp_line = v
    setting.exposure_time = line2time(v)
    setting.digital_gain = (setting.exposure_time == 0) ? 1024
                         : (setting.digital_gain * old_exp) / setting.exposure_time
    if setting.digital_gain < 1024: setting.digital_gain = 1024

if setting_last.sensor_exp_line != setting.sensor_exp_line
   or setting_last.total_gain  != setting.total_gain
   or wdr_ratio.last != wdr_ratio.tmp:
    latency_count = arm
```

WDR quantises the shutter to a multiple of `w` lines and keeps the ordinary
latency arming `arm = max(delay, 2)`. It does **not** extend the countdown to
`w`: a large `w` would otherwise stall the HDR-ratio update (the ratio is only
recomputed while the countdown is settled), so the ratio must converge on the
normal cadence. The ratio's own convergence row is section 9's.

So the countdown is armed only when the emitted setting actually changes; WDR
also arms on a change of the HDR ratio. In WDR, shutter is quantised to
multiples of `wdr_cfg[0]*16` lines and digital gain compensates for the
shortening.

### 14.6 Manual exposure configuration

`configure_manual` (exposure_mode == manual):

```
setting_last = setting
setting.total_gain = test.test_gain
if gain_split_ratio < 0.1:
    analog_gain = ((fno2 * ev / 160000) / exposure_time) << 4
else:
    digital_gain = clamp(isqrt((total_gain<<10)*gain_split_ratio), digital_lo, digital_hi)
    analog_gain  = ((total_gain<<10)/digital_gain) & ~0xf

analog_gain = clamp(sensor_gain << 4, 256, 1536000)
setting.sensor_exp_line = time2line(fixed_exposure, round_up=true)
setting.exposure_time   = fixed_exposure
setting.f_number        = aperture
setting.fno2            = nearest_aperture(setting)
setting.digital_gain    = 1024

if hdr_mode == WDR:
    w = wdr_cfg[0]*16; if w==0: w=16
    v = w * (setting.sensor_exp_line / w)
    setting.sensor_exp_line = (v==0) ? w : v
    setting.exposure_time   = line2time(setting.sensor_exp_line)
    v = (total_gain << 10) / setting.exposure_time
    if v < 1024: v = 1024
    setting.digital_gain = min(v, 0x4000)

# arm the countdown
if setting_last.sensor_exp_line == setting.sensor_exp_line:
    latency_count = (setting_last.total_gain == setting.total_gain) ? 2 : gain_delay_frames
else:
    latency_count = exp_delay_frames
```

### 14.7 Initial setting from an explicit index

When set-parameters tag 2 is used, `configure_sensor(line_index)` is applied
directly, but with a subtlety: unlike the frame path, this call does **not**
first update `setting_last` from `setting`, so the delay-arming comparison uses
the previous frame's setting. This is the documented behaviour.

---

## 15. Table semantics

All tables are runtime-injected (`interfaces.md`). None may be compiled in.
`EVTAB` is the only writable table; the line-table builder rewrites it.

### 15.1 Log-luminance table (`LOG2`, 256 x signed int)

`LOG2[i] = round(1000 * log2(i+1))`, so `LOG2[0] = 0`, each doubling adds 1000,
and `LOG2[255] = 8000`. Used only to form the exposure error (section 10.1).

### 15.2 EV scale table (`EVTAB`, 1024 x unsigned int, writable)

`EVTAB[i] = round(1000 * 2^(0.04*i))`, clamped to 0xffffffff; `EVTAB[0]=1000`.
Written by the line-table builder, and read by compensation (section 11), the
applied-ratio computation (section 10.4), and the line-table builder itself.
After construction, entries beyond `idx_max` are zero.

### 15.3 Convergence table (`CONV`, 32 rows x 128 unsigned bytes)

Rows select a convergence speed; columns are the absolute error in index steps
(0..127); the value is the applied step magnitude for that error. Row 0 is the
identity (`CONV[0][k] = k`, capped); higher rows compress the step (e.g. row 15
is roughly `k/2`), giving progressively slower, more damped convergence. Row 15
is also used to converge the HDR ratio (section 9). Rows are selected from
`exposure_cfg[4..7]` (section 10.3).

### 15.4 Touch-distance weight table (`TOUCHPROB`, 6 rows x 12 unsigned int)

Rows index a "distance index" selector (0..5); columns index the outside-ellipse
distance (0..11). The value is a weight in the range 0..32 (Q5), largest at
distance 0 and decreasing outward. Consumed by section 7.3 to build the touch
metering grid.

### 15.5 Histogram kernel bank (`KERNEL`, 20 rows x 31 signed int)

Row `r` is a 31-tap kernel used to correlate the 32 collapsed histogram blocks,
producing a 62-sample response in Q10 (a `>>10` is applied per tap product). The
rows are smoothed/differentiated variants used to locate histogram peaks; the
row is selected by `kernel_index + attempt` clamped to 0..19 (section 8.2).

### 15.6 Pre-gamma curve (`PREGAMMA`, 11 rows x 256 signed short)

11 curves of 256 points, values 0..4095 (12-bit). Low-index rows are strongly
non-linear, high-index rows approach linear. This table is part of the injected
AE set but is **not consumed** by this module's deployed code (it originates
from the tone-mapping object); a conforming implementation must expose the
handle but may ignore it in AE.

### 15.7 Backlight reference masks (`BLMASK`, 3 rows x 64 signed int)

Three 8x8 binary reference patterns (values 0/1) used by the pattern-match
branch of the classifier (section 8.5). Observed rows: a centred-bright mask,
an all-zero mask and an all-one mask; the implementation must use the injected
values.

### 15.8 Spatial metering grids (4 tables, each 64 signed int)

`WGHT_MATRIX`, `WGHT_AVG`, `WGHT_CENTER` are the matrix/average/center 8x8
weight grids (values 1..64; average is uniform, center is peak-centred). The
matrix grid may be replaced at init by `window_weight_seed`. `WGHT_OVER` and
`WGHT_UNDER` (also 64 each) are additional isolation grids applied to
highlight/shadow windows (section 7.1). All are row-major `row*8+col`.

### 15.9 Backlight network weights (`NET_IN`, `NET_BIAS`, `NET_OUT`)

A two-layer integer network with 64 inputs, 10 hidden units and 2 output
scores:

| Table | Shape | Role |
| --- | --- | --- |
| `NET_IN` | 10 x 64 signed int | input weights, hidden unit `j` at `j*64 + i`. |
| `NET_BIAS` | 10 signed int | hidden biases, added before the `/100`. |
| `NET_OUT` | 20 signed int | output weights: `[0..9]` score 0, `[10..19]` score 1. |

Exact use in section 8.5. The output biases 3561 and 6438 are folded into the
score constants, not stored in the tables.

### 15.10 Default aperture ladder (`FNOLADDER`, 16 signed int)

A monotonically increasing ladder of aperture codes (observed values
`141,145,152,163,175,190,209,233,266,311,379,487,657,971,1825,3794`). Used to
snap a continuous aperture to the largest ladder value not exceeding it, and to
produce the squared aperture value `fno2`. Copied into configuration only when
`aperture_ladder[0] == 0`.

### 15.11 Default exposure descriptor (`TABLEDEF`)

An exposure descriptor used when no valid scene table is configured. Observed
content: `length=2`, `ev_step=40`, `shutter_shift=0`, two segments
(`{min_exp=8000,max_exp=30,min_gain=max_gain=256,min_iris=max_iris=266}` and
`{min_exp=30,max_exp=30,min_gain=256,max_gain=6000,min_iris=max_iris=266}`),
the remaining segments zero. These values are injected data; do not hardcode.

### 15.12 Auxiliary probability table (`AUXPROB`, 96 x 256 unsigned char)

A distance-probability table indexed by a threshold row (10..95) and a pixel
distance column (0..255). Values decrease with distance; the row selects how
quickly. Shared with the white-balance module and used for the backlight
histogram centroid (section 8.7). Not to be confused with `TOUCHPROB`.

---

## 16. Constants

| Constant | Value | Role |
| --- | --- | --- |
| exposure index step | 0.04 EV | `EVTAB` ratio per index. |
| `IDX_MAX` | 1024 | line-table length. |
| index saturation | 0x3ff | line-table loop bound. |
| convergence-row saturation | 31 (5 bits) | section 10.3. |
| luma clamp | 1..255 | error inputs. |
| startup frame ramp | 80 | tolerance and convergence-row offsets. |
| WDR early-frame reset | 11 | section 9. |
| histogram blend divisor | 1024 | section 7.4. |
| histogram blocks | 32 x 8 | section 8.1. |
| convolution length | 62 (32+31-1) | section 8.2. |
| peak limit | 8 | section 8.2. |
| peak-valid bars | 0x19, 0x32 | night-mode weight (below). |
| night-mode weight | 64 at <=25, 0 at >=50, linear between | section 8.5. |
| neutral histogram bin | 253 | section 8.3. |
| classification scale | /800 | network output (section 8.5). |
| network scores bases | 3561, 6438 | section 8.5. |
| output offsets | 0x14=20, 0x28=40 | section 8.5. |
| backlight clamps | 0x20=32, 0x40=64 | sections 8.5/8.6. |
| HDR ratio range | 160..304 (0xa0..0x130) | section 9. |
| HDR ratio default | 256 | section 9. |
| HDR history convergence row | 15 | section 9. |
| flicker ceilings | 10000 us (50 Hz), 8333 us (60 Hz) | section 13.6. |
| digital gain default range | 1024..3072 | section 13.1. |
| analog gain low clamp (manual) | 256 | section 14.6. |
| digital gain high clamp (manual WDR) | 0x4000 | section 14.6. |
| high-fps threshold | 58 fps, 50 index steps | section 14.1. |
| delay minimum | 2 frames | section 14.5. |
| max line count | 160000000 | section 13.8. |
| line rounding granularity | 16 (Q4) | section 13.8. |

### 16.1 Night-mode weight

```
d = idx_max - setting_last.index
ad = min(|d|, 256)
if d < 25:      w = 64
elif d < 50:    w = (50 - ad) * 64 / 25
else:           w = 0
```

Subtracted from the pattern-match distance (section 8.5); a lower value raises
the effective backlight fraction in low light.

---

## 17. Edge cases and failure behaviour

- **Warm-up frames 0..2:** only the current setting is emitted; `backlight` is
  set to 32. No exposure computation, no statistics use.
- **Null object / stats in run:** return `-1`; if a result pointer is provided,
  the default-result initialiser runs.
- **Default result:** if `setting_curr.analog_gain == 0`, seed
  `analog_gain=256`, `sensor_exp_line=100`, `digital_gain=1024`, HDR ratios 96,
  copy the short setting and divide its exposure by 96.
- **`hts == 0`:** `time2line` returns 0; `frame_rate_max` and exposure math
  degrade. Fault.
- **`pclk == 0`:** `line2time` returns 0.
- **Descriptor divide-by-zero:** a segment with `min_exp == 0` (or `max_exp`,
  `min_iris`) divides by zero in preprocessing; `check_tbl` is expected to have
  rejected such tables when `table_source == 1`, but `table_source == 0/2` does
  not validate.
- **No matching segment:** the entry stays zero (`exposure_time == 0`) and is
  skipped; `idx_max` may therefore be lower than expected if the EV range has
  holes.
- **Convergence row below zero:** the warm-up offset (`-8`) is applied after
  the 5-bit saturation with no lower clamp, so rows below 0 are possible when
  the base row is small. The deployed object reads adjacent memory; the
  boundary vectors match a lower clamp to row 0, which is what a conforming
  implementation uses (see Open questions).
- **`weight_sum == 0`** or zero histogram weight: the weighted luminance is 0;
  `avg_lum` is still reported.
- **`AUXPROB` denominator zero:** centroid returns 0.
- **HDR `wdr_ratio.tmp == 0`:** treated as 256.
- **HDR short division:** divisor is `max(sensor_ratio>>4, 1)`.
- **Digital normalisation:** if the tracked minimum digital gain is 0, the
  scaling in section 13.7 divides by zero; the minimum is initialised to 1024
  and the digital range is clamped to at least 1024.
- **Out-of-ladder aperture:** `nearest_aperture` leaves the existing `fno2`.
- **High-frame-rate override:** used only when frame rate exceeds 58 fps and
  the current index is within 50 steps of maximum.
- **Integer overflow / wraparound:** `EVTAB` is capped; `ev` products use
  64-bit intermediates in several places, but the per-frame error, gain-ratio
  and classification arithmetic is 32-bit and wraps as written.
- **Signed/unsigned semantics:** negative corrections (e.g. `t < 0: t += 63`)
  and the `(unsigned)(bright_pos - dark_pos)` range tests must be implemented
  literally.

---

## 18. Open questions / ambiguities

1. **Negative convergence rows.** The startup offset can push the selected
   convergence row below zero during the first 80 frames. The boundary vectors
   match a lower clamp to row 0, which is what a conforming implementation uses;
   the deployed object's pre-table read is not a reproducible interface value.
2. **Physical units.** The segment fields are processed as reciprocals of
   exposure and a squared aperture; the absolute scale is anchored only by the
   first segment's low EV. The relation to physical EV/LV values is not
   derivable here and does not affect relative behaviour.
3. **Total-gain units.** Total gain from the descriptor (e.g. 256) is treated
   interchangeably with a Q4 analog gain in some branches and as the input to
   the split (`total_gain<<10 / gain_split_ratio`) in others. The intended
   physical meaning of `gain_split_ratio` (analog vs digital preference) is
   inferred from behaviour only.
4. **High-fps interval formula.** `(frame_time/2 + 15)/frame_time` clamped to
   {1,2,3} does not match a simple frame-count interpretation; the `15`
   constant may be a units artefact. Reproduce the literal expression.
5. **Spatial grid addressing.** The backlight grid reads a flattened region
   that continues into the histogram and past it, using a stride that does not
   match the declared statistics layout. This appears to be a fixed addressing
   artefact; reproduce it rather than "correcting" it.
6. **Pattern-match loop origin.** The per-mask distance loop skips cell 0. It
   is unclear whether this is intended or an off-by-one; reproduce it.
7. **`ev_lv_adj` index.** It uses `delta_idx/3` (truncated) and clamps only the
   upper bound, to `idx_max`. When the result is negative the deployed object
   reads before the active table and the observed value at that layout position
   is zero, so the clean core reports zero (section 14.1). The rationale (an LV
   probe a third of the error away) is still undocumented.
8. **Two almost-identical priority branches.** The shutter- and
   aperture-priority digital-gain recomputations use 256 and 1024 scale factors
   respectively. Whether this asymmetry is intentional is unknown.
9. **Unused settings.** `mains_setting`, the histogram-equalisation and
   tone-mapping dynamic vectors, the flash switch/capture flags and several
   sensor fields are present but never read. Their intended role is elsewhere
   in the framework.
10. **Pre-gamma provenance.** The pre-gamma table is injected alongside the AE
    tables but originates from the tone-mapping object and is not used by AE;
    whether AE was ever meant to consume it is unknown.
11. **`ev_step`.** The descriptor's `ev_step` field is copied but never used;
    the effective step is hardwired to 0.04 EV.
12. **Flash strength helper.** `flash_expect_lum` is never computed inside the
    module; the caller must supply it. The formula's sign convention for a
    pre-flash brighter than post-flash is untested.

## WDR-flow findings (append)

The real per-frame pipeline runs the AE, then the WDR configuration step, then
the next frame's AE, all sharing one result block that lives in the framework's
memory.  The WDR configuration step republishes the blend-ratio slots
(`sensor` / `hardware` / `tmp` / `last`) and the high/low thresholds in that
shared block, and the following AE run observes them:

- the exposure-configuration path arms the sensor delay whenever the published
  blend-ratio `last` and `tmp` differ (in addition to an exposure-line or
  total-gain change);
- it also consumes the ratio when aligning the short/long exposure and gains.

An integration that gives the AE a private result block (the clean shim) must
therefore forward the framework's between-run writes to the blend-ratio slots
into the core's WDR state before each run, or the delay arming and the WDR
exposure alignment diverge from the deployed core.  This is integration glue,
not an algorithm change: the standalone entity harnesses, which never run the
framework WDR step, are unaffected because an all-zero ratio block means "no
framework ratio published yet" and the core keeps its own seed.

See the private differential harness's WDR-flow suite for the differential that
reproduces the full
per-frame flow (commanding and DOL WDR, zero and tuned WDR tables) and pins
this contract.

## Real-input replay findings (append)

Running the deployed and clean AE on the captured on-camera frames
(`qemu-arm diff_wdr_flow.bin <ae_dump> <freeisp_tables.bin>`) exposed two
contract points that the synthetic campaigns do not reach.

### Init-time derived state is part of the input

The per-frame run consumes state that is derived once at init from the tuning
block, not from the per-frame settings:

- the matrix metering grid is replaced by the injected window-weight seed when
  the seed's sum is non-zero, else the table default is kept;
- the three scene slots are seeded from the selected descriptor when unset;
- the aperture ladder is filled from the default ladder when unset.

The framework sends the init command once, so the deployed core derives this
state from the tuning block as it stands at that moment; the per-frame refresh
only carries the live settings.  A clean shim that refreshes the whole
parameter block before every run must be compared against a deployed entity
that was initialised from the **same** tuning block.  A replay that initialises
the deployed entity from a synthetic (e.g. zero) seed and only then loads the
captured block makes the two sides derive different metering grids, and every
subsequent luminance/exposure field drifts.  The replay fixture must therefore
seed the init-time inputs from the first captured record before initialising
either entity.

### Metering-grid out-of-block tail

The spatial metering grid flattens the window averages and the histogram and
then keeps indexing a fixed number of entries past the end of the statistics
block (the last 8 of its 64 blocks).  The deployed core reads whatever memory
follows the statistics block; the clean core treats the tail as zero.  The
statistics block's tail is not part of the injected table/statistics contract,
so the replay fixture gives each statistics block an explicit zeroed tail to
make the two sides agree.  A strict match to the deployed read is not possible
without reproducing the framework's memory layout, and the tail value does not
affect any documented output once the surrounding scene classification is
stable.

With both points applied the real-input replay reports 0 of 64 frames with
mismatches.

## Metering-grid tail: measured impact (append, supersedes the note above)

The zero-tail agreement above is a fixture artefact, not evidence that the tail
is inert.  Replaying the same 64-frame y623 capture with a synthetic non-zero
tail placed immediately after the statistics block (the words the deployed core
over-reads) diverges on 61 of 64 frames.  Exactly three fields differ, and all
three carry the same classified backlight value:

| field | location | vendor vs clean values seen |
| --- | --- | --- |
| `ae_result.backlight` | result + 0x1b2 | 17 vs 25, 18 vs 26, 34 vs 32, 35 vs 33 |
| `sensor_info.backlight` | sensor_info + 0x71 (compared word +0x70) | same set |
| `ae_param.ae_sensor_info.backlight` | ae_param + 0x12d9 (compared word +0x12d8) | same set |

No module-configuration field diverges.  The sensitivity is not a simple
magnitude: an all-ones tail (`0xffffffff` / `0x7fffffff`) diverges on zero
frames, while moderate words (e.g. `0x00ff00ff`) diverge on most.  The real
on-camera tail has not yet been captured, so this establishes only that the
out-of-block read is observable and can move the classifier; the deployed and
clean cores cannot be assumed equivalent until the real tail is replayed.

Two fix directions, to be chosen after the real-tail replay:

1. **Widen the statistics contract (faithful).**  Give the clean statistics
   block an explicit out-of-block tail (e.g. `uint32_t tail[152]` after the
   histogram) and have `luma_at` return `tail[idx - 640]` for `640 <= idx <
   792`.  The SDK-boundary shim copies the captured post-struct words into that
   field.  This reproduces the deployed read exactly on camera, at the cost of
   making the framework's adjacent memory part of the input contract (it must be
   captured and replayed; the clean core is no longer self-contained).
2. **Clamp at the histogram (deterministic).**  Keep `luma_at` returning zero
   (or fold the region into the last histogram bin) so the clean core never
   depends on adjacent memory.  This is self-contained and reproducible, but
   deliberately differs from the deployed core wherever the real tail is
   non-zero; it is only acceptable if the real-tail replay shows the classifier
   converges to the same class, or if matching the deployed over-read is out of
   scope.

Measurement tooling: the AE shim captures the post-struct bytes with
`FREEISP_AE_TAIL=<nbytes>` into a `<dump>.tail` file (the 12116-byte `<dump>`
record layout is unchanged), and the wdr_flow replay applies them after both
statistics blocks (`diff_wdr_flow.bin <dump> <bundle> [<tail>]`), with
`--tail-selftest` demonstrating the placement and detection.

## Real-tail result on the y623 green capture (append)

The real out-of-block tail was captured on the y623 green build and replayed
(45 frames, `greencap_ae.bin` + `greencap_ae.bin.tail`, 1024 bytes/frame).  The
deployed-vs-clean replay reports **0 of 45 frames with mismatches**: the real
tail does **not** change the AE output on this capture.  A control run with an
extreme synthetic tail (`0x00ff00ff` words) on the same capture is also a clean
0/45, so these frames never consult the grid region at all — the backlight
classifier stays in its pinned-`backlight` branch, and neither the real nor a
worst-case tail can move it.  (The older 64-frame `ae_dump_clean.bin` capture
does enter the grid branch, and there the same synthetic tail still diverges on
61/64 frames, so the tail's potential to matter is scene-dependent, not a
property of every capture.)  The earlier fix proposal therefore applies only if
a future capture lands in the grid branch; on the green capture the tail read is
observably inert.

### A genuine vendor divide-by-zero, unrelated to the tail

The first real-tail replay aborted with SIGFPE before any frame.  Bisecting
showed the fault is not the tail and not the harness placement: the same abort
occurs with **no tail at all** on the new capture.  The fault is an integer
divide-by-zero in the deployed vendor core's backlight assessment (the
probability-weighted centroid/weighted-luminance step): it divides
the Q8 luminance × 256 by the accumulated pixel-weight sum with no zero check.  The
clean core guards the identical computation (`ae_clean.c` returns 0 when the
denominator is 0; the same guard exists in the histogram luminance and window
metering paths), which is why the deployed clean core does not fault on camera.
The quotient is `0/0`, so the guarded and unguarded forms agree on the value;
the only difference is that libgcc's weak `__aeabi_idiv0` raises SIGFPE.

The wdr_flow replay now provides a no-op `__aeabi_idiv0`/`__aeabi_ldiv0`
(the ARM ABI sets the result registers before branching to the hook, so a no-op
returns the pre-set quotient and execution continues; this matches a target with
a no-op SIGFPE disposition).  The clean core never reaches the hook, so the
override affects only the vendor side of the differential; with it, the real
tail replay completes and the 0/45 result above is available.  This is a
harness-completion fix, not a clean-AE change: the vendor's missing guard is a
latent defect in the deployed object, not in the clean room.


