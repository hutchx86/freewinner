<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Auto White Balance (AWB) — Behaviour Specification

This document specifies observable behaviour only. It contains no source, no
vendor identifiers, and no reference string literals. Names used below are
neutral descriptions of the data the module consumes and produces; the exact ABI
field names and offsets are supplied by the compatibility layout header and by
`interfaces.md`. Vendor symbol names are deliberately omitted: internal helpers
are referred to by the behaviour they perform.

The module is stateful and runs once per video frame. It consumes a block of
per-window colour statistics plus a configuration block, maintains temporal
gain history and scene state, and writes a white-balance gain quadruple and a
correlated colour-temperature estimate.

---

## 1. Purpose and scope

The module estimates the scene illuminant's colour cast and compensates for it:

- It classifies the colour of each statistics window against a set of
  configured reference illuminants ("light presets"), each tagged with a
  correlated colour temperature.
- It selects a subset of windows it trusts, based on how close each window is
  to a reference, the scene brightness, the time of exposure, and the spatial
  consistency of window colour temperature.
- It estimates a grey-world gain from the trusted window averages.
- It applies an optional user colour-preference blend and per-channel favourite
  factors.
- It normalises and clamps the gains, smooths them over time at a configurable
  rate, and reports the resulting gain and an estimated colour temperature.

The module also supports a deterministic fixed-temperature mode and a set of
user-selected preset scene modes; both bypass the adaptive estimator.

It does **not** drive exposure, focus, tone mapping, or lens shading. It does
not decode raw frames: it reads a pre-computed statistics grid and returns the
white-balance gains the rest of the pipeline should apply.

---

## 2. Conventions and symbols

- Arithmetic is 32-bit two's-complement integer unless stated; divisions
  truncate toward zero. A few accumulators are 64-bit (noted where used).
- `<<`/`>>` on signed values are arithmetic.
- `x mod n` is the non-negative remainder; where the original uses a mask
  (`& 0x3f`, `% 0x30`) this is stated.
- Dimension constants (section 8):
  - `NWIN = 1024` statistics windows (32 x 32),
  - `NREF = 10` reference classes,
  - `NCURVE = 64` points in the global illuminant curve,
  - `NPTS = 16` points in each per-class curve,
  - `NGHIST = 48` entries in the temporal gain history,
  - `NPAL = 5` reference classes searched for the nearest illuminant.
- A gain of `0x100` (256) means unity. The channel ratios used for
  classification are stored as `round(256 * channel / green)`.
- Names in `monospace` are neutral field names defined here, not ABI names.

---

## 3. Input contract

### 3.1 Configuration block

Read live from per-instance parameter storage each call (the module keeps a
pointer to it; the set-parameters entry point only triggers a table/curve
rebuild, see 5).

| Neutral name | Meaning | Type | Notes |
| --- | --- | --- | --- |
| `mode` | Operating mode | enum int | 0 = manual, 1 = adaptive (auto), 2..10 = preset scenes (see 6.14). |
| `manual_gain` | Fixed manual gains | 4x u16 | Used only in manual mode. |
| `lock` | Freeze the adaptive result | bool | When true the adaptive/preset update is skipped. |
| `win_region` | Window-of-interest rectangle | 4x s32 | Read only by callers; not consumed by this module. |
| `interval` | Re-estimate cadence | s32 | Non-zero forces a full estimate every frame gate; also used by the frame dispatch (6.1). |
| `speed` | Temporal smoothing speed | s32 | Selects a row of the smoothing-weight table (6.10). |
| `temp_low`, `temp_high` | Configured colour-temperature bounds | s32 | **Not read** by this module. |
| `base_temp` | Preference target temperature | s32 | Used in the preference blend (6.11); 0 means 6500 K. |
| `green_dist` | Green-zone distance gate | s32 | See 6.7.1. |
| `blue_dist` | Blue-sky distance gate | s32 | See 6.7.2. |
| `light_num` | Count of standard light presets | s32 | 0..32. |
| `ext_light_num` | Count of extended presets | s32 | Filled but never classified (5.2, open questions). |
| `skin_num` | Count of skin references | s32 | 0..16. |
| `special_num` | Count of special-colour references | s32 | 0..32. |
| `light_info` | Standard preset records | s32[10*32] | Layout in 3.3. |
| `ext_light_info` | Extended preset records | s32[10*32] | Same layout. |
| `skin_info` | Skin reference records | s32[10*16] | Same layout. |
| `special_info` | Special-colour records | s32[10*32] | Same layout. |
| `preset_gain` | Per-scene red/blue ratio pairs | s32[22] | Pair at offset `mode*2` used by preset modes. |
| `r_favor`, `b_favor` | Favourite red/blue multipliers | s32 | Applied as `value*favor/256`, only when both non-zero (6.11). |

Embedded sensor descriptor fields actually read:

| Neutral name | Meaning | Type | Notes |
| --- | --- | --- | --- |
| `ae_lv` | Scene brightness level (light value) | s32 | Drives brightness bucket and night detection. |
| `ae_index` | Current exposure-table index | u32 | Gating for re-estimation (6.1). |
| `ae_index_max` | Exposure-table length | u32 | Gating. |
| `ae_done` | Exposure settled flag | u8 | Gating. |

A small test/diagnostic sub-block is also read:

| Neutral name | Meaning | Role |
| --- | --- | --- |
| `test_mode` | Test mode selector | Not read by this module. |
| `platform_id` | Platform selector | Not read by this module. |
| `fixed_temp` | Fixed colour temperature | Used when adaptive control is disabled (6.14); 0 means 6500 K. |
| `control_enable` | Adaptive-control enable | Non-zero enables the adaptive/preset state machine (6.1). |

### 3.2 Statistics block

A pointer to a 32 x 32 grid of per-window statistics. Only two per-window
quantities are read:

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `avg[3]` | Average red, green, blue level in the window | u32 each | 0..255 nominal |
| `npix` | Count of pixels that contributed | u32 | hardware-dependent |

The block also carries per-window sums and a separate average array; they are
not read. Exactly `NWIN = 1024` entries are processed, laid out row-major with
32 columns.

### 3.3 Reference-record layout

Each illuminant/colour reference record is a run of 10 signed integers:

| Offset | Neutral name | Meaning |
| --- | --- | --- |
| 0..2 | `ref[3]` | Reference normalised R, G, B levels (white-point chromaticity proxy). |
| 3..5 | `pref[3]` | Preference-gain triple (see 6.11). |
| 6 | `tol` | Distance tolerance; selects a row of the trust table. |
| 7 | `temp` | Reference correlated colour temperature. |
| 8 | `prob_ls` | Light-source probability / base trust. |
| 9 | `prob_pref` | Preference probability. |

On load (5) each record also derives two channel ratios:

```
refk_r = (ref[0] * 256 + ref[1]/2) / ref[1]
refk_b = (ref[2] * 256 + ref[1]/2) / ref[1]
```

### 3.4 Injected tables

Obtained through the runtime table-injection API (see `interfaces.md`); never
compiled in. See section 7 for semantics.

| Neutral name | Shape | Role |
| --- | --- | --- |
| `TRUST` | u8[96][256] | Distance/tolerance -> trust probability. |
| `SPEED_W` | u16[48][64] | Temporal smoothing weight rows. |
| `CLASS_LABEL` | char[10][32] | Human labels for the ten classes (diagnostics only). |
| `STD_TRUST` | s32[10] | Per-preset default trust. |
| `TEMP_BRIGHT` | s32[10][10] | Base brightness x temperature trust matrix. |

---

## 4. Output contract

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `gain_out.r`, `.gr`, `.gb`, `.b` | White-balance channel gains | u16 each | adaptive path smooths then clamps each to [32, 2048]; fixed/manual paths do not |
| `color_temp_out` | Estimated correlated colour temperature | s32 | Kelvin-like units used by the reference curve |

`0x100` (256) is unity gain. `gr` and `gb` are normally equal; they are kept
separate because the constraint step scales each independently.

The six entry points (init / exit / get-params / set-params / run / isr; exact
interop names in `interfaces.md`) behave as follows:

- **init** allocates and initialises an instance and returns the operation
  table; returns NULL on allocation failure.
- **run** is the per-frame entry point and calls the ISR path. Returns `0`
  normally, `-1` if called with a null entity or statistics (and writes a safe
  default, see 9).
- **isr** is the actual per-frame state machine and always returns `0`.
- **set-params** triggers a table/curve rebuild when the parameter kind is the
  init-data tag, and returns `-1` otherwise.
- **get-params** writes the parameter pointer; returns `-1` on a null entity.
- **exit** releases the instance.

---

## 5. Internal state and lifecycle

One instance is allocated at init and is otherwise opaque to the framework.

| Neutral name | Shape/type | Role |
| --- | --- | --- |
| `frame_id` | s32 | Caller-supplied frame counter (from config). |
| `frame_count` | u32 | Internal count of smoothed frames; indexes the gain ring. |
| `total_windows` | int | 1024. |
| `win[]` | NWIN records | Per-window classification state (5.1). |
| `pal[10][5]` | reference records | Anchor sets for classes 0..2 and composites 3..4. |
| `pal_count[10]` | int | Anchor counts per class. |
| `class_curve[10][16]` | reference records | Per-class interpolated curves. |
| `curve[64]` | reference records | Global illuminant curve (6.4). |
| `dist_min[10]` | int | Per-class minimum anchor distance for the current window. |
| `win_count[10]` | int | Per-class window counts for the frame. |
| `temp_mean[10]` | int | Trust-weighted mean temperature per class. |
| `trust_lut[100][20]` | int | Resampled brightness/temperature trust table (6.5). |
| `gain_new` | 4x u16 | Latest grey-world estimate. |
| `gain_hist[48]` | 4x u16 | Ring of recent gains for smoothing. |
| `gain_target` | 4x u16 | Estimate before smoothing (for save/restore). |
| `gain_saved` | 4x u16 | Last accepted gain; fallback when a window invalidates the estimate. |
| `high_temp_gain` | 4x u16 | Reference gain derived from the cool class, used for skin handling. |
| `is_night` | int | Night flag for the current frame. |
| `color_temp`, `color_temp_target` | int | Working / pre-smoothing temperature. |
| `hist[765]`, `max_sum` | u32[765], u32 | Per-frame histogram of window `R+G+B` and its maximum. |

### 5.1 Per-window record

| Neutral name | Meaning |
| --- | --- |
| `avg[3]` | Window channel averages (copied from the statistics block). |
| `kr`, `kb` | `round(256*R/G)` and `round(256*B/G)`. |
| `npix` | Valid pixel count. |
| `sum` | `R + G + B`. |
| `dist` | Class distance after saturation. |
| `w_dist` | Trust weight from `TRUST` (0 = reject). |
| `color_temp` | Decided colour temperature. |
| `seg` | Index into the global `curve[64]`. |
| `w_level` | Brightness/temperature weight before scene weighting. |
| `valid` | Window accepted flag. |
| `cls` | Reference class 0..9. |
| `is_skin`, `is_special` | Colour-detector flags. |

Lifecycle:

- **Allocation/init:** all storage zeroed; `frame_count = 0`;
  `total_windows = 1024`; `gain_new` and `high_temp_gain` set to unity;
  `gain_hist` filled with unity; `gain_saved` unity; the brightness/temperature
  trust table is built (6.5). Presets, classes and curves are built separately
  by the init-data parameter push (5.2).
- **Per frame:** the statistics are extracted (6.2); classification and
  weighting run (6.3-6.7); optionally a gain estimate is produced (6.8-6.9);
  smoothing runs (6.10); preference and constraint run (6.11-6.12);
  `gain_saved`/`color_temp` and `frame_count` persist.
- **Teardown:** release storage.

### 5.2 Reference hierarchy construction (on init-data)

1. Build the ten preset records from the configured arrays (3.3). Two defaults
   are applied per record, using its own derived reference ratios
   `(refk_r, refk_b)`:
   - **Preference gains.** If `prob_pref < 101`:
     ```
     pref[1] = 256
     pref[0] = ((100 - prob_pref) * (refk_r - 256)) / 100 + 256
     pref[2] = ((100 - prob_pref) * (refk_b - 256)) / 100 + 256
     ```
     i.e. `prob_pref == 0` yields the full reference ratio and `prob_pref == 100`
     yields unity.
   - **Light-source probability.** If `prob_ls > 128`, replace it with
     `STD_TRUST[record_index]` for the first ten records, or with
     `TEMP_BRIGHT[0]` beyond ten.
2. Partition the standard presets into three anchor groups by reference
   temperature:
   - group 0: `temp < 3500`,
   - group 1: `3500 <= temp < 4500`,
   - group 2: `temp >= 4500`.
3. Build 16-point curves for groups 0, 1, 2 by linear interpolation (6.4).
4. Build the two composite classes:
   - class 3 anchors: `{ last anchor of group 0, first curve point of group 1 }`,
   - class 4 anchors: `{ first curve point of group 1, first anchor of group 2 }`,
   then build 16-point curves for them.
5. Assemble the global 64-point curve:
   `curve[0..15]` = class 0 curve, `curve[16..31]` = class 3 curve,
   `curve[32..47]` = class 4 curve, `curve[48..63]` = class 2 curve.

The ten classes and their meaning are in 6.3.

The reference-record data type is 13 integers (3.3 gives the first 10; the
remaining three fields carry a derived reference gain pair and a light-type
tag, both carried but unused in the estimator).

---

## 6. Algorithm

### 6.1 Per-frame dispatch and gating

```
if frame_id < 3:
    if all four result gains are non-zero: return 0        # warm-up, keep caller's gain
    else: write fixed-temperature result (6.14) and return 0

if test.control_enable != 0:
    if interval != 0:  gate = (interval == 0 or (frame_id-1) % interval == 0)
    else:              gate = (frame_id < 33)
    if gate and not lock:
        mode == adaptive  -> adaptive estimate (6.8) + smoothing + preference + constraint
        mode == manual    -> result gain = manual_gain
        mode is a preset   -> adaptive estimate + preset correction (6.14)
        write diagnostic record
    return 0

else:  # adaptive control disabled, fixed-temperature path
    write fixed-temperature result (6.14)
```

The adaptive path is `6.2 -> 6.3 -> 6.4..6.7 -> 6.8 -> 6.9 -> 6.10 -> 6.11 ->
6.12`; after it, `gain_saved` is updated from `gain_target` and the frame count
advances inside the smoother.

Re-estimation gate inside the adaptive path: a fresh full estimate is performed
only when **any** of the following holds; otherwise the previous `gain_new` is
re-used:

```
ae_index < 2
or ae_index_max - 1 <= ae_index
or interval != 0
or ae_done == 0
or frame_id < 34
or frame_id % 10 == 0
```

### 6.2 Statistics extraction

For every window `i` in row-major order over the 32 x 32 grid:

```
accept = (avg_r - 2 < 249) and (avg_g - 2 < 249) and (avg_b - 2 < 249)   # unsigned
```

Interpreting the unsigned range test, a channel is accepted when its average is
in `[2, 250]` (the subtraction must not underflow). If all three pass:

```
win.sum   = R + G + B
win.kr    = (R*256 + G/2) / G
win.kb    = (B*256 + G/2) / G
win.npix  = statistics count
hist[win.sum] += 1
max_sum = max(max_sum, win.sum)
```

If any channel fails, the window's averages, ratios, `npix` and `sum` are
zeroed and it is marked unusable. `hist` and `max_sum` are reset at the start
of every extraction.

### 6.3 Reference classes and per-window classification

The ten classes are:

| id | Neutral role | How it is populated / assigned |
| --- | --- | --- |
| 0 | Warm / low-temperature illuminant cluster | Presets with `temp < 3500`. |
| 1 | Neutral / mid illuminant cluster | Presets with `3500 <= temp < 4500`. |
| 2 | Cool / daylight illuminant cluster | Presets with `temp >= 4500`. |
| 3 | Warm-to-mid transition | Composite anchors from classes 0 and 1 (5.2). |
| 4 | Mid-to-cool transition | Composite anchors from classes 1 and 2 (5.2). |
| 5 | Green zone | Runtime override for green-tinted windows (6.7.1). |
| 6 | Shadow | Label reserved; no runtime assignment path in this module. |
| 7 | Blue sky | Runtime override for blue/sky windows (6.7.2). |
| 8 | Flash | Label reserved; no runtime assignment path in this module. |
| 9 | Outlier / invalid | Windows failing validity or trust tests. |

`CLASS_LABEL` supplies human-readable names for these ids and is used only in
diagnostics. Classes 0..4 are the only ones searched for a nearest illuminant;
5, 7 and 9 are assigned by the override/validity logic and 6, 8 are unreachable.

Per window, classification proceeds:

1. **Per-class minimum anchor distance.** For each class `c` with
   `pal_count[c] != 0`, find the minimum over its anchors `j`:
   ```
   dr = win.kr - pal[c][j].refk_r
   db = win.kb - pal[c][j].refk_b
   dist_min[c] = min_j (dr*dr + db*db)
   ```
   The running minimum is held in a **16-bit** quantity seeded to `65535`
   (an anchor distance is `dr*dr + db*db`, which for saturated ratios can
   exceed this). Each candidate distance above `65535` therefore saturates at
   `65535` rather than being compared as a 32-bit value. This is observable:
   it truncates the reported minimum distance, and when every class clamps it
   turns the nearest-class search into a tie that the first class (lowest
   index) wins. Only the minimum value is retained (the anchor index is not).
   Classes with zero anchors are skipped (their `dist_min` is untouched).
2. **Nearest and second-nearest class.** Search only classes 0..4:
   - first-best = index of the smallest `dist_min` (first on ties).
   - second-best = index of the smallest `dist_min` excluding first-best. The
     candidate is seeded with the last searched index (`4`) when
     first-best == 0, and with the first index (`0`) otherwise, then replaced
     only by a class whose `dist_min` is **strictly** smaller. Exact ties for
     second place therefore resolve to the later index when first-best == 0, and
     to the earlier index otherwise. Classes with no anchors are not
     candidates.
3. **Refine along each candidate's curve.** For the 16 points of
   `class_curve[first-best]` and of `class_curve[second-best]`, compute the
   squared distance from `(win.kr, win.kb)` to each point's `(refk_r, refk_b)`.
   Pick the best point of each curve (first on ties).
4. **Choose the winner.** If the second-class best distance is strictly less
   than the first-class best, the window's class is second-best and its
   decided reference is that curve point; else first-best and its point.
5. **Saturated distance.** `win.dist = clamp(trunc(sqrt(best_distance)), 0, 255)`
   (saturate to 8 bits). `win.color_temp` = the chosen point's `temp`.
6. **Segment id.** `win.seg` = index of the first `curve[]` entry whose
   temperature is `> win.color_temp`, capped at 63 (`curve[63]` when the search
   runs off the end).

### 6.4 Curve interpolation

`build_curve(anchors, n) -> 16 points`, used for classes 0..4:

```
if n <= 0: return (leave curve unchanged)
t_lo = anchors[0].temp
t_hi = anchors[n-1].temp
if not (1000 <= t_lo < 9000) or t_lo > t_hi: return (unchanged)
step = (t_hi - t_lo) / 15
for k = 0..15:
    t = t_lo + k*step
    j = first anchor index with anchors[j].temp > t   (n if none)
    if j == n:    j = n-1
    if j == 0:    lo = 0, hi = 1
    else:         lo = j-1, hi = j
    for each of the 13 integer fields of the record:
        point[k].field = anchors[lo].field
                       + (anchors[hi].field - anchors[lo].field)
                         * (t - anchors[lo].temp)
                         / (anchors[hi].temp - anchors[lo].temp)
```

The division yields `anchors[lo].field` when the two temperatures are equal.

### 6.5 Brightness and temperature trust table

Built once at init from `TEMP_BRIGHT`:

```
in[10][10]  = TEMP_BRIGHT
out[100][20] = bilinear_resize(in, 10, 10 -> 20, 100)
trust_lut[i][j] = trunc(out[i][j])
```

The bilinear resize uses ratio `(dim-1)/dim_out` on each axis and the standard
four-tap formula. The first index is the colour-temperature bucket (0..99), the
second is the brightness bucket (0..19).

### 6.6 Per-window trust and scene weighting

For each window:

```
temp_idx  = min( (color_temp - 950) / 100, 99 )          # unsigned divide
level_idx = (ae_lv + 50) / 100; if < 0 then 0; if > 18 then 19

p = clamp(pal.trust_prob, 16, 128)                       # base trust
w = trust_lut[temp_idx][level_idx] * p
if w < 0: w += 15
w >>= 4
win.w_level = w
win.w_dist  = TRUST[tol*256 + win.dist]
win.valid   = (w != 0 and win.w_dist != 0)
```

Overrides on the class id (in this order):

1. If the green-zone gate fires (6.7.1), `win.cls = 5`.
2. If the blue-sky gate fires (6.7.2), `win.cls = 7`.
3. If `win.valid == 0`, `win.cls = 9`.

Per-class bookkeeping for the frame:

```
win_count[cls] += 1
temp_mean[cls] += color_temp * win.w_dist      # trust-weighted
class_weight[cls] += win.w_dist
```

After all windows, `temp_mean[c] = class_weight[c] ? temp_mean[c]/class_weight[c] : 0`.

**Scene weights.** Ten per-class multipliers start at 16. In daytime they stay
16. At night they depend on the class composition:

```
if win_count[0] > 50% of total_windows:
      [0..4] = [16, 12, 1, 12, 2]
elif win_count[0] + win_count[3] > 75% of total_windows:
      [0..4] = [16, 12, 2, 16, 8]
elif win_count[3] + win_count[4] <= 70% of total_windows:
      if win_count[4] <= 70% of total_windows:
          [0..4] = [16, 16, 16, 16, 16]
      else:
          [0..4] = [8, 12, 16, 8, 16];  scene_weight[5] = 8
else:
      [0..4] = [12, 16, 2, 16, 16]
```

All ten multipliers default to 16; only those listed are changed.

Then for every valid window:

```
win.w_level = (win.w_level * scene_weight[win.cls]) >> 2
```

**Outlier abort.** With `thr = 80` by day and `90` at night, if

```
thr * total_windows / 100 < win_count[9]
```

the adaptive estimate is abandoned for this frame (return `-1`): the previous
`gain_new` and `gain_target` are kept and the smoother still runs. Otherwise the
estimator proceeds.

### 6.7 Colour adjusters

These run on the classified windows before the grey-world sum, in the order
blue sky (6.7.2), special colour (6.7.4), green zone (6.7.1); skin (6.7.3) is
invoked separately only in the high-temperature branch (6.7.3).

#### 6.7.1 Green zone

Fires when:

```
green_dist < win.dist
and win.kr < decision.refk_r
and win.kb < decision.refk_b
and 4001 <= decision.temp < 7500        # unsigned range test
```

The adjuster rewrites the window's chromaticity to the reference curve entry
`curve[win.seg]` at the green level and marks it invalid (its class was already
set to 5 during classification):

```
r_ratio = (curve.seg.ref[0]*256 + curve.seg.ref[1]/2) / curve.seg.ref[1]
b_ratio = (curve.seg.ref[2]*256 + curve.seg.ref[1]/2) / curve.seg.ref[1]
win.avg_r = (G * r_ratio) >> 8
win.avg_b = (G * b_ratio) >> 8
win.kr = curve.seg.refk_r
win.kb = curve.seg.refk_b
win.valid = 0
```

#### 6.7.2 Blue sky

Fires when:

```
blue_dist < win.dist
and |win.kr - decision.refk_r| < blue_dist
and decision.refk_b < win.kb
and decision.temp > 7300
```

The adjuster remaps the window chromaticity toward a fixed cool reference: find
`base` = first `curve[]` entry with `temp > 5499` (capped at 63), compute its
ratios, and:

```
scale_r = (win.kr * base_r_ratio + r_ratio/2) / r_ratio
scale_b = (win.kb * base_b_ratio + b_ratio/2) / b_ratio
win.avg_r = (G * scale_r) >> 8
win.avg_b = (G * scale_b) >> 8
win.kr = base.refk_r
win.kb = base.refk_b
win.valid = 1
```

where `r_ratio`/`b_ratio` are the ratios of `curve[win.seg]`. The window stays
valid and is class 7.

#### 6.7.3 Skin

Invoked only when the warm/cool windows (classes 2 and 4 together) exceed
`total_windows / 25` (about 4%); `high_temp_gain` is then recomputed from the
summed channels of those windows before this adjuster runs. `high_temp_gain` is
computed as `r = sum_g*16 / (sum_r>>4)`, `gr = gb = 256`, and
`b = sum_g*16 / (sum_b>>4)`; if either summed red or blue is below 17 it is
reset to unity instead. The adjuster runs on every currently valid window using
`high_temp_gain` as the reference pre-set gain. If `skin_num == 0`, or
`win.kr == 0`, or `win.kb == 0`, skip.

```
u = win.kr * preset.r / preset.gr
v = win.kb * preset.b / preset.gr
find skin reference index minimizing (u-refk_r)^2 + (v-refk_b)^2   (first on ties)
real_dist = clamp(trunc(sqrt(min distance)), 0, 255)
if TRUST[skin.tol*256 + real_dist] == 0 or u == 0 or v == 0:
    win.is_skin = 0; skip
win.dist = real_dist
win.is_skin = 1
win.avg_r = (win.avg_r*256 + u/2) / u
win.avg_b = (win.avg_b*256 + v/2) / v
win.kr = (win.kr*256 + u/2) / u
win.kb = (win.kb*256 + v/2) / v
win.w_dist = TRUST[skin.tol*256 + real_dist]
```

#### 6.7.4 Special colour

Run on all windows (whether valid or not, provided the channel ratios are
non-zero and `special_num != 0`). Select the nearest special reference by
squared distance in `(kr, kb)` and compute `real_dist` as above. If the trust
lookup is zero, clear `is_special` and skip.

Determine a blend weight from the reference's preference/light-source
probabilities and the scene brightness `lv`:

```
if pref_prob <= 100:
    gain_r = gain_b = 256
    if lv <= 700: skip
    weight = 32
else:
    gain_r = (pref.r/2 + pref.g*256) / pref.r
    gain_b = (pref.b/2 + pref.g*256) / pref.b
    if pref_prob <= prob_ls:
        if pref_prob == prob_ls:
            if prob_ls >= lv: skip
            weight = 32
        else:
            if pref_prob > lv: weight = 32
            elif lv > prob_ls: skip
            else: weight = (lv - pref_prob)*32 / (pref_prob - prob_ls) + 32
    else:
        if lv < prob_ls: skip
        elif pref_prob < lv: weight = 32
        else: weight = (lv - prob_ls)*32 / (pref_prob - prob_ls)
    if weight == 0: skip
```

Then remap the window to the cool reference curve entry `base` (first entry
with `temp > special.temp`, capped at 63):

```
ratio_r = (base.ref[0]*256 + base.ref[1]/2) / base.ref[1]
ratio_b = (base.ref[2]*256 + base.ref[1]/2) / base.ref[1]
t0 = round(gain_r * ratio_r / 256)          # negative-result correction handled
t2 = round(gain_b * ratio_b / 256)
win.avg_r = (G * t0) >> 8
win.avg_b = (G * t2) >> 8
win.kr = base.refk_r
win.kb = base.refk_b
win.dist = real_dist
win.is_special = 1
win.color_temp = base.temp
win.w_dist = round(weight * TRUST[special.tol*256 + real_dist] / 32)
```

Negative intermediate products are corrected by adding 31 before the `>> 5`
and by adding 255 before a `>> 8`, matching the reference's signed rounding.

### 6.8 Night detection

```
if ae_lv > 600: return daytime
dark = count of windows with (R + 2G + B) < 84
night = (total_windows * 70) / 100 < dark
```

At night the neighbour-consistency filter (6.9.2) is skipped.

### 6.9 Window validity and the grey-world estimate

#### 6.9.1 Neighbour consistency (bad-window filter; daytime only)

For each window that is currently valid, examine its 3 x 3 neighbourhood
(clipped to the 32 x 32 grid, the centre included):

```
same = count of neighbours whose color_temp differs from the centre by < 500
total = number of in-grid neighbours
bad = (same * 256) / total < 100
```

A window is marked invalid when `bad` (fewer than about 39% of neighbours agree).

#### 6.9.2 Grey-world sum

With `weight = win.w_dist * win.valid * win.w_level`:

```
sum_r = Σ weight * avg_r
sum_g = Σ weight * avg_g
sum_b = Σ weight * avg_b
```

If any of the three is zero, keep the previous `gain_saved` as `gain_new`.
Otherwise:

```
Y = round(G/2) + round(B/4) + round(R/4)          # ≈ (R + 2G + B)/4
gain_new.r  = (Y*256 + sum_r/2) / sum_r
gain_new.gr = (Y*256 + sum_g/2) / sum_g
gain_new.gb = gain_new.gr
gain_new.b  = (Y*256 + sum_b/2) / sum_b
```

#### 6.9.3 Colour-temperature lookup

Given a gain quadruple, scan the 64-point global curve and pick the entry
minimising the squared difference in the two channel-ratio axes:

```
for i in 0..63:
    v = ((b/2 + gb*256) / b) - curve[i].refk_b
    r = ((r/2 + gr*256) / r) - curve[i].refk_r
    d[i] = r*r + v*v
color_temp = curve[argmin(d, first on ties)].temp
```

This is applied to `gain_target` (pre-smoothing) and to `gain_out`
(post-constraint) to produce `color_temp_target` and `color_temp_out`.

### 6.10 Temporal smoothing

A weighted moving average over the last 48 estimates. Let `speed` be the
configuration value, adjusted during the first 33 frames:

```
if frame_id < 33:
    if speed < 16: speed = speed * (frame_id - 3) / 30 + 1
    else:          speed = (frame_id*15 - 45) / 30 + (speed - 15)
row = speed & 0xffff
weights = SPEED_W[row * 64 .. row*64 + 47]
```

Push `gain_new` into `gain_hist[frame_count % 48]` and compute (64-bit
accumulators) over the taps that hold real history. Until the ring is full the
sum must cover only the taps pushed so far, `taps = frame_count + 1` (capped
at 48); averaging the whole row during warm-up would blend in the unity-seeded
ring entries and pull early gains toward unity:

```
taps = min(frame_count + 1, 48)
for k = 0..taps-1:
    w = weights[k]
    g = gain_hist[(frame_count + 48 - k) % 48]
    numer += w * g;  denom += w
gain_out.ch = clamp_gain(round(numer / denom))     # clamp to [32, 2048]
frame_count += 1
```

At/after `frame_count == 47` all 48 taps are summed. `gain_out.gb` is smoothed
independently even though `gain_new.gb == gain_new.gr`.

### 6.11 User preference blend

Find `i` = first `curve[]` entry with `temp > color_temp` (capped at 63). Pull
the entry's preference gains `(pr, pg, pb)`. If `pref_prob < 101` and the
current gain is non-zero:

```
base = base_temp or 6500
j = first curve[] entry with temp > base (capped at 63)
ratio_r = round(gr*256 / r)
ratio_b = round(gr*256 / b)
yr = round(ratio_r * 256 / curve[j].refk_r)
yb = round(ratio_b * 256 / curve[j].refk_b)

pr = pr * (((100 - pref_prob) * (yr - 256)) / 100) / pg + 256
pb = pb * (((100 - pref_prob) * (yb - 256)) / 100) / pg + 256
pg = 256
```

Then apply `r = r * pr / pg`, `b = b * pb / pg` (integer division).

Finally, if both favourite multipliers are non-zero:

```
r = (r_favor * r + 128) >> 8
b = (b_favor * b + 128) >> 8
```

Negative-result correction (+255 before `>> 8`) applies as in the reference.

### 6.12 Gain normalisation / constraint

Scale all four gains so the smallest of `{r, gb, b}` becomes exactly 256
(`gr` is not part of the minimum):

```
m = min(r, gb, b)
ch = (ch * 256) / m     for ch in {r, gr, gb, b}
```

The smoother's clamp of [32, 2048] applies before this step; the normalisation
itself can push channels above 2048.

### 6.13 Perfect-reflector path (alternate estimator)

An entry point is provided that estimates the illuminant from the brightest
windows (specular/reflector assumption) instead of the grey world:

1. Extract statistics (6.2).
2. Choose a luminance threshold from the `R+G+B` histogram. Start at `uv = 750`
   and walk down accumulating counts until the cumulative count first exceeds
   5% of `total_windows`, or `uv` reaches 0:
   ```
   threshold = total_windows * 5 / 100
   uv = 750; acc = 0
   while acc <= threshold and uv > 0:
       acc += hist[uv]; uv -= 1
   ```
3. For every window with `sum > uv`, weight it by
   `TRUST[80*256 + (max_sum - sum)]` and accumulate weighted `avg_r/avg_g/avg_b`
   and the weight total (`sum_w`).
4. `t = sum_w * max_sum * 256`; replace `max_sum` with `t / 3`.
5. If the weighted sums are all non-zero and `t > 2`, set
   `gain_new = max_sum / sum_ch` for each channel (`gb = gr`); else fall back to
   `gain_saved`.
6. Run smoothing (6.10), colour-temperature lookup (6.9.3), preference (6.11)
   and constraint (6.12), then save the output to `gain_saved`.

This path is not invoked by the normal per-frame dispatch in this module; it is
an exposed alternative estimator.

### 6.14 Preset scenes and fixed temperature

**Preset scenes (mode 2..9).** After the adaptive estimate, compute red/blue
targets indexed by the mode:

```
p = preset_gain[mode*2], q = preset_gain[mode*2 + 1]
if p == 0 or q == 0:
    p = round(r * 256 / gr)          # current output ratios
    q = round(b * 256 / gr)
    p = clamp(p, mode_low_r, mode_high_r)
    q = clamp(q, mode_low_b, mode_high_b)
r = (gr * p) >> 8;  gb = gr;  b = (gr * q) >> 8
```

with per-mode clamps:

| mode | `r` clamp | `b` clamp |
| --- | --- | --- |
| 2 warm | [120, 170] | [450, 500] |
| 3 fluorescent | [200, 220] | [320, 370] |
| 4 high fluorescent | [290, 305] | [290, 305] |
| 6 daylight | [250, 260] | [250, 260] |
| 8 cloudy | [260, 270] | [245, 250] |
| 5, 7, 9 (horizon/flash/shade) | [254, 256] | [254, 256] |
| 10 (unspecified) | no preset handling | |

Modes 2..9 are handled. Mode 10 lies outside the preset range: the adaptive
estimate still runs, but the preset step leaves the red/blue ratios at unity
relative to green.

**Fixed temperature path.** When adaptive control is disabled (or during the
start-up frames with zero gains), a temperature `ct = test.fixed_temp`
(or 6500 if zero) is used: find the first `curve[]` entry with `temp > ct`
(capped at 63), output `color_temp = ct` and

```
r = ((curve.refk_r/2) + 65536) / curve.refk_r
gr = gb = 256
b = ((curve.refk_b/2) + 65536) / curve.refk_b
```

These are the inverse reference gains (correction gains).

---

## 7. Table semantics

All tables are runtime-injected and must never be compiled in; a missing handle
is an error.

### 7.1 Trust table (`TRUST`, u8[96][256])

Row index = a reference's distance tolerance `tol`; column = a saturated
distance (0..255). The value is a trust probability in `[0, 32]` in the
captured table; `0` means "reject". Used for the per-window distance weight
(6.6), skin (6.7.3), special colour (6.7.4), and the perfect-reflector weight
(row 80, 6.13).

### 7.2 Smoothing speed table (`SPEED_W`, u16[48][64])

Row index = smoothing speed; 48 weights per row. Low speeds are short/impulsive
(e.g. row 0 ≈ one-frame impulse); higher speeds approach a flat 48-tap window.
Rows are applied as a weighted average of the last 48 gains (6.10). The
denominator is the row sum, so absolute scale does not matter.

### 7.3 Class-label table (`CLASS_LABEL`, char[10][32])

Human-readable names for classes 0..9, matching the roles in 6.3. Used only in
the optional diagnostic dump; it has no effect on gains.

### 7.4 Standard-trust table (`STD_TRUST`, s32[10])

Per-preset default light-source trust. Used at preset load: if a record's
`prob_ls` is greater than 128, it is replaced by `STD_TRUST[preset_index]` for
standard presets, or by `TEMP_BRIGHT[0]` for presets beyond the first ten
(5.2).

### 7.5 Temperature-level trust matrix (`TEMP_BRIGHT`, s32[10][10])

Base matrix of brightness x colour-temperature trust; resampled to
`trust_lut[100][20]` at init (6.5). Values in the captured table are in
`[0, 24]`. Larger values mean more trust; the second axis is scaled by the
brightness bucket.

---

## 8. Constants

| Constant | Value | Role |
| --- | --- | --- |
| `NWIN` | 1024 | 32 x 32 windows. |
| `NCURVE` | 64 | Global curve points. |
| `NPTS` | 16 | Points per class curve. |
| `NPAL` | 5 | Classes searched for the nearest illuminant. |
| `NGHIST` | 48 | Gain-history length and smoothing tap count. |
| data channel range | 2..250 | Window average acceptance (unsigned). |
| gain ratio scale | 256 | Unity channel ratio. |
| unity gain | 256 | `0x100`. |
| gain clamp | 32..2048 | Smoother output clamp. |
| class partition | 3500 / 4500 | Warm / mid / cool group boundaries. |
| class-distance clamp | 65535 | 16-bit per-class minimum-anchor accumulator. |
| curve temp validity | [1000, 9000) | Curve build guard. |
| green-zone temp window | [4001, 7500) | Green-zone gate. |
| blue-sky temp floor | > 7300 | Blue-sky gate. |
| blue-sky base temp | > 5499 | Blue-sky remap reference. |
| night brightness floor | 84 | `R + 2G + B` below this is dark. |
| night LV ceiling | 600 | Above this it is never night. |
| night dark fraction | 70% | Fraction of dark windows to call night. |
| neighbour temp delta | 500 | Agreement radius for the bad-window filter. |
| neighbour agreement floor | ~39% | `(same*256)/total < 100`. |
| outlier abort | 80% / 90% | Day / night outlier-window abort. |
| initial re-estimate frames | 33 | Speed ramp; frame gate. |
| periodic re-estimate | every 10 frames | Frame gate. |
| `max_sum` histogram bins | 765 | `R+G+B` maximum. |
| perfect-reflector threshold | 750, 5% | Histogram walk start and window fraction. |
| perfect-reflector trust row | 80 | Fixed tolerance row. |
| default preference temp | 6500 | When `base_temp == 0`. |
| preference probability cutoff | < 101 | Blend only below this. |
| probability clamp | 16..128 | `prob_ls` clamp for weighting. |
| speed ramp split | 16 | `speed` below/above this uses different ramps. |
| preset-gain pairs | 22 (11 pairs) | Mode-indexed red/blue ratios. |

---

## 9. Edge cases and failure behaviour

- **Frames 0..2:** if the caller already supplied non-zero gains, nothing is
  written; otherwise the fixed-temperature gains are written. No statistics are
  consumed.
- **Null entity or statistics:** the run entry point returns `-1` without
  touching the smoother; if *any* result gain is zero it writes the deployed
  caller-injected safe gains (`awb_clean_tables_t.safe_gain`; skipped when
  NULL) and 6500 K. A null result pointer is not defended
  (programming error).
- **Channel average at 0 or 1, or above 250:** the window is unusable and its
  fields are zeroed; it can still be classified (distance 0 to a zero
  reference) only if references are zero, which normally cannot happen.
- **Division by zero:** window ratios divide by `avg_g`; accepted windows have
  `avg_g >= 2`. The grey-world estimate guards against zero sums; the
  constraint step divides by `min(r, gb, b)`, which is non-zero because the
  smoother clamps to `>= 32`.
- **Empty class group:** if no preset falls in a group, that group's curve
  build is skipped and the composite classes may read `pal[c][-1]`. The
  captured configuration avoids this; callers must supply at least one preset
  per temperature group.
- **Curve-build guard failure:** an invalid temperature span leaves the curve
  entry at its previous (zeroed) value, so classification may match entry 0.
- **Aborted adaptive estimate (too many outliers):** the previous `gain_new`
  and `gain_target` are retained, and the smoother still runs; `gain_saved` is
  set from the stale `gain_target`.
- **Night:** the neighbour-consistency filter is skipped, and the outlier abort
  uses the 90% threshold.
- **Out-of-range `speed`:** the raw value (masked to 16 bits) indexes
  `SPEED_W` with no bounds check; callers must supply a speed in `[0, 47]`.
- **`frame_count % 48`:** the gain ring wraps and overwrites; early frames
  blend toward the unity-initialised history.
- **Signed/unsigned range tests:** class temperature selection, group
  boundaries, and the colour-temperature index derive from subtracted values
  cast to unsigned; implement the literal (wrapping) tests rather than chained
  comparisons.
- **Diagnostics:** an opt-in per-frame log may be emitted when a sentinel file
  exists; it has no effect on gains and is outside the behavioural contract.

---

## 10. Open questions / ambiguities

1. **Classes 6 and 8 (shadow, flash).** Their labels exist and the per-class
   arrays are sized for them, but no code path assigns a window to class 6 or 8
   in this module. Whether they are purely legacy, or are intended for
   extended-light classification that was not captured, is unknown.
2. **Extended light presets.** `ext_light_info` is loaded into storage but the
   partition step only walks standard presets, so extended presets never reach
   classification. Their intended use is unclear.
3. **Unused configuration.** `temp_low`, `temp_high`, `platform_id` and
   `test_mode` are accepted but never read by this module.
4. **Carried light-type tag.** Every reference record carries a light-type
   integer that is copied but never compared. Its semantics are unknown.
5. **Accumulator widths.** The grey-world and preference sums are wide enough
   to overflow 32 bits in large scenes; the reference object appears to use
   32-bit accumulators while a natural implementation may use 64-bit. The
   intended overflow behaviour should be fixed by golden vectors. (The class
   minimum-anchor accumulator is settled: it is a 16-bit value seeded to
   `65535`, see 6.3 step 1. The second-nearest tie rule is settled too, see
   6.3 step 2.)
6. **Rounding corrections.** Several places add `(divisor-1)` before shifting a
   possibly negative intermediate ("add 15 before `>> 4`", "add 31 before
   `>> 5`", "add 255 before `>> 8`"); the exact signed-rounding rule is
   reproduced literally, but whether it is intentional rounding or an artefact
   of compiler codegen is unknown.
7. **`max_sum` reuse in the perfect-reflector path.** The histogram maximum is
   overwritten with a scaled quantity before being used as the numerator; the
   derived scaling `sum_w * max_sum * 256 / 3` is not obviously a physically
   motivated normalisation. Confirm against the differential.
8. **Curve step truncation.** The 16-point curve uses
   `step = (t_hi - t_lo)/15` in integer arithmetic, so the last point can
   undershoot `t_hi`. Whether the reference expects the endpoints to coincide
   is unclear.
9. **Scene-weight index range.** Only classes 0..5 receive non-default night
    scene weights; classes 6..9 keep the daytime default of 16. This is
    harmless for 6 and 8 (unreachable) but the treatment of class 9 is
    implicit.
10. **Value range of `TRUST`.** The captured table's values are small
    (0..32); the spec treats them as relative weights. The intended absolute
    scale is not derivable from the module.
