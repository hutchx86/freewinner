<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Global Tone Mapping — Behaviour Specification

This document specifies observable behaviour only. It contains no source code, no
vendor identifiers, and no reference string literals. Names in `monospace` are
neutral descriptions of the data the module consumes and produces; the exact ABI
field names/offsets are supplied by the compatibility layout header and by
`interfaces.md`.

---

## 1. Purpose and scope

The module computes, once per qualified frame, a global 256-entry tone curve
(count `i = 0..255` -> output luma) for the whole image. It consumes a hardware
luminance histogram and a coarse block-average luminance array, plus runtime
tuning coefficients and a set of injected reference tables. It carries the curve
across frames (temporal smoothing) and emits a few scalar statistics for the
rest of the pipeline.

It implements exactly three effective behaviours:

- a **dynamic-range** curve derived by histogram equalisation against an injected
  kernel bank;
- a **luma-hold** curve derived by blending the normalised cumulative histogram
  with one of three fixed guide profiles, holding mean luminance;
- a shared **brightness/contrast** S-curve used by every other mode.

The module supports exactly four mode values (`fixed`, `dynamic-range`,
`dynamic-gamma`, `luma-hold`). There are no other tone-mapping modes in the
deployed object. It does not do local/region tone mapping, does not alter
exposure or gain, and does not itself expose the histogram.

---

## 2. Conventions and symbols

- Arithmetic is 32-bit two's-complement integer unless stated otherwise;
  signed overflow/wraparound is permitted and divisions truncate toward zero.
- `|x|` is the absolute value; `min`/`max` are saturation helpers.
- `clamp(x, lo, hi) = min(max(x, lo), hi)`.
- `>>` on a signed negative value is an arithmetic shift (floor toward negative
  infinity); `+ r` immediately before `>> s` is a rounding term.
- `u32(x)` reinterprets `x` as unsigned for a range test.
- `sum(a + k, n)` denotes `sum_{j=0..n-1} a[k+j]`.
- Histograms are counts; a normalised histogram is carried on a 4096 (Q12)
  scale. The 12-bit output range is `0..0xFFF`.
- `hist` is indexed by input luma `0..255`; `curve` is the output tone curve.
- Names in `monospace` are this document's neutral names, not ABI names.

---

## 3. Input contract

### 3.1 Configuration / parameter block

Read live on every run from per-instance parameter storage. Fields not listed
are unused by this module.

| Neutral name | Meaning | Type | Range / notes |
| --- | --- | --- | --- |
| `mode` | Tone-mapping mode | enum int | 0=`fixed`, 1=`dynamic-range`, 2=`dynamic-gamma`, 3=`luma-hold`. Other values behave as a non-fixed blend (see 6). |
| `gamma_mode` | Tone-domain transfer kind | enum int | 0=`fixed`, 1=`dynamic`. Selects the fixed-vs-dynamic division in the equalisation stage and the fixed-vs-multiply branch in the blend. |
| `frame_index` | Monotonic frame counter | signed int | Runs only when `frame_index > 2` (see 6). |
| `enable` | Module enable flag | int | Runs only when non-zero. |
| `range_max` | Target maximum output level | signed int | Valid inclusive `0x200..0x1000`; out-of-range triggers the coefficient reset in 8.1. |
| `eq_gain` | Equalisation strength | signed int | Valid `0..100` (unsigned test); out-of-range triggers the reset. |
| `cover_bias` | Kernel-row bias | signed int | Valid `-10..10` (test on `cover_bias+10`); out-of-range triggers the reset. |
| `black_level` | Shadow pivot | signed int | Alpha-ramp pivot; `0` causes a divide-by-zero (see 15). |
| `white_level` | Highlight pivot | signed int | Alpha-ramp pivot; `256` causes a divide-by-zero (see 15). |
| `white_slope` | Highlight ramp slope | signed int | Alpha-ramp coefficient. |
| `black_slope` | Shadow ramp slope | signed int | Alpha-ramp coefficient. |
| `pre_gamma_offset` | Pre-gamma row selector | signed int | Row index is `clamp(pre_gamma_offset+4, 0, 10)`. |
| `hist_pixel_count` | Divergence pixel count | signed int | Threshold is `hist_pixel_count * 16`. |
| `dark_floor` | Shadow floor | signed int | Clamped to `0..0x80` then divided by 8 (see 9.5). |
| `bright_floor` | Highlight floor | signed int | Clamped to `0..0x80` then divided by 8 (see 9.5). |
| `peak_map` | 9x9 luminance/activity -> target peak map | signed int[9][9] | Bilinear interpolation source (see 9.3). |
| `brightness` | User brightness | signed int | Used by luma-hold merge and by the blend. |
| `contrast` | User contrast | signed int | Used only by the blend. |
| `bit_offset` | WDR output downscale | int | Low byte `b` selects `span = 256 >> b` and a multiple-pass downscale in luma-hold (see 9.9). |
| `wdr_en` | WDR downscale enable | bool | When false, `bit_offset` is treated as 0 and the downscale is skipped. |
| `gamma_lut` | Per-instance monotone tone map | unsigned short[1024] | Domain remap and its piecewise-linear inverse (see 12.5). |
| `curve` | Current output tone curve | unsigned short[256] | Read-modify-write each run. |
| `curve_prev` | Previously applied curve | unsigned short[256] | Read-modify-write each run. |

`curve` and `curve_prev` are the module's primary persistent outputs (see 4).

### 3.2 Injected tables

Read-only, obtained through the runtime table-injection API (see
`interfaces.md`); never compiled in.

| Neutral name | Meaning | Type | Length |
| --- | --- | --- | --- |
| `GUIDE_LINEAR` | Neutral reference cumulative profile | signed short[256] | 256 |
| `GUIDE_LOW` | Shadow-biased guide profile | signed short[256] | 256 |
| `GUIDE_HIGH` | Highlight-biased guide profile | signed short[256] | 256 |
| `PRE_GAMMA` | Per-row pre-gamma transfer bank | signed short[11][256] | 2816 |
| `EQ_KERNEL` | Histogram equalisation kernel bank | signed int[20][31] | 620 |
| `CONVERGE` | Per-frame curve-step limit bank | unsigned char[32][128] | 4096 |

### 3.3 Statistics block

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `hist_raw[256]` | Hardware luminance histogram | unsigned int[256] | any 32-bit |
| `win_avg[384]` | Block-average luminance array | unsigned int[384] | any 32-bit |

Exactly 256 histogram bins and 384 average entries are read.

---

## 4. Output contract

On every run in which the module executes (see 6), it writes:

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `curve[256]` | Updated global tone curve | unsigned short[256] | 0..0xFFF |
| `curve_prev[256]` | Copy of the applied curve | unsigned short[256] | 0..0xFFF |
| `avg_lum` | Mean luma of the normalised histogram | unsigned short | 0..255 |
| `avg_var` | Mean absolute deviation statistic | unsigned short | 0..255 |
| `peak_level` | Slew-limited target peak level | unsigned short | >= 0x80 |
| `div_index` | Divergence histogram index | unsigned short | 0..255 |
| `ratio_hold` | Held merge ratio | double | see 9.7 |
| `hdr_flag` | "A curve was produced" marker | signed int | set to 1 on every run |

`avg_lum`, `avg_var`, `peak_level`, `div_index` and `ratio_hold` are written by
the `luma-hold` path; `hdr_flag` is written by the dispatcher. When the module
does not run, none of these are touched.

Entry-point status returns:

- the run entry point returns `-1` if the instance, statistics or result pointer
  is null, else `0`;
- the get-parameters entry point returns a pointer to the instance parameter
  storage, or `-1` if the instance pointer is null;
- the set-parameters entry point returns `0` for the `initialise` parameter kind
  (and runs a one-time table seeding), else `-1`.

---

## 5. Internal state and lifecycle

Allocated once per instance and zeroed. Private per-instance buffers (conceptual
shapes):

| Neutral name | Shape/type | Role |
| --- | --- | --- |
| `alpha_ramp` | int[256] | Static shadow/highlight weighting ramp (`dynamic-range`). |
| `alpha_ramp_auto` | int[256] | Auto-weighting ramp; disabled (see 8.2). |
| `weight_hist` | int[256] | Inverse-alpha weighting histogram (`dynamic-range`). |
| `weight_cum` | int[256] | Cumulative `weight_hist`. |
| `weight_gamma` | int[256] | Cumulative-gamma profile. |
| `weight_balance` | int[256] | Per-bin slope-balance weight. |
| `norm_hist` | int[256] | Normalised histogram (Q12). |
| `adj_hist` | int[256] | Floor/cap-clamped histogram (`luma-hold`). |
| `cum_hist` | int[256] | Cumulative `norm_hist` or `adj_hist`. |
| `tone_map` | int[256] | Equalisation-stage working curve. |
| `merge_curve` | int[256] | luma-hold working curve / smoothing scratch. |
| `blend_curve` | int[256] | Brightness/contrast working curve. |
| `hist_total` | unsigned int | Total raw histogram weight. |
| `var_hold` | unsigned short | Last `avg_var` below the re-use threshold. |
| `guide_state` | signed short | Guide polarity: -1, 0 or +1. |
| `guide_streak` | signed short | Consecutive contradictory-frame counter. |
| `brightness_saved` | int | Last applied brightness (sentinel `0xFFFF0001` after init). |
| `contrast_saved` | int | Last applied contrast (same sentinel). |
| `busy_flag` | int | Set to 1 at allocation; otherwise unused. |

Lifecycle:

- **Allocation/init:** storage zeroed, `busy_flag = 1`, both saved knobs set to
  the sentinel `0xFFFF0001`.
- **Table seeding** (set-parameters `initialise`): if `curve[255] == 0`, set
  every entry of `curve` and `curve_prev` to `0x200`. Otherwise leave both
  untouched.
- **Per frame:** the dispatcher decides whether to run, then updates `curve` and
  the persistent statistics.
- **Persistent across frames:** `curve`, `curve_prev`, `avg_var`'s hold,
  `peak_level`, `div_index`, `ratio_hold`, `guide_state`, `guide_streak`, the
  saved knobs, and the parameter block.

---

## 6. Top-level mode selection (dispatcher)

Called from the per-frame control entry point, which also sets a nominal
one-frame interval. No work is done until `frame_index > 2` and `enable != 0`.
When it runs:

```
if mode == dynamic-range:
    histogram_equalisation(stats)        # section 8
    prefilter_curve()                    # section 11.1 (curve = 15/16*prev + 1/16*new)

if mode == luma-hold:
    luma_hold_curve(stats)               # section 9 (replaces everything below)
else:
    brightness_contrast_curve()          # section 10

hdr_flag = 1
```

Consequences:

- `dynamic-range` runs the equalisation curve, then the temporal prefilter, then
  falls through to the shared brightness/contrast blend.
- `luma-hold` runs only its own path; it never applies the brightness/contrast
  blend (contrast has no effect; brightness enters through the merge term).
- `fixed`, `dynamic-gamma`, and any unrecognised mode run only the blend. The
  blend distinguishes `fixed` (overwrite) from everything else (multiply), using
  `gamma_mode` only in the equalisation stage.
- `frame_index <= 2` or disabled: no curve, no statistic and no `hdr_flag`
  update.

---

## 7. Average-luminance / variance statistic

Input: a normalised histogram `H[0..255]` summing to 4096. Writes `avg_lum`,
`avg_var`, updates the instance `cum_hist`, and uses `var_hold`.

All the arithmetic in this section is unsigned 32-bit (the counts, the class
weights and the between-class measure), not the signed default of section 2.
Because the normalisation in 9.1 can leave the histogram summing slightly above
4096, the upper-class weight `u = 4096 - c` is allowed to wrap rather than go
negative; the object relies on that.

```
M[i] = sum_{j=0..i} j*H[j]
C[i] = sum_{j=0..i} H[j]
avg_lum = M[255] >> 12
```

`C` is stored into `cum_hist`. Then an Otsu-style threshold scan over odd bins
`k = 1, 3, 5, ..., 253`:

```
c = C[k]                 # lower-class weight
u = 4096 - c             # upper-class weight
lum_l = (c != 0)    ? M[k] / c           : <unchanged from previous k>
lum_h = (c != 4096) ? (M[255]-M[k]) / u  : <unchanged from previous k>
d1 = |lum_l - avg_lum|
d2 = |lum_h - avg_lum|
between = c*d1*d1 + u*d2*d2
```

`lum_l`/`lum_h` are only assigned when their class is non-empty, so a degenerate
bin inherits the previous value (both start at 0). Selection keeps the largest
`between`; ties accumulate:

```
if between > best:      best = between; sum_k = k;   cnt = 1
elif between == best:   sum_k += k;      cnt += 1
otsu = sum_k / cnt
```

Because the loop starts from `best = 0, sum_k = 0, cnt = 0`, an all-zero
histogram leaves every `between == 0`; all odd `k` then tie and the threshold
becomes their mean (127).

Using the chosen `otsu`:

```
c = C[otsu]; u = 4096 - c
lum_l = (c != 0) ? M[otsu] / c               : <stale>
lum_h = (u != 0) ? (M[255]-M[otsu]) / u       : <stale>
var_low  = (c != 0) ? (sum_{j=0..otsu}    H[j]*|j-lum_l|) / c : <undivided sum>
var_high = (u != 0) ? (sum_{j=otsu+1..255} H[j]*|j-lum_h|) / u : <undivided sum>
raw = ((c*var_low + u*var_high) >> 12) * 5
avg_var = min(raw, 255)
if raw < 200:  var_hold = avg_var
else:          avg_var = var_hold          # re-use the last low-activity value
```

`var_low`/`var_high` accumulate from zero and are only divided when their class
is non-empty. `avg_var` is a mean-absolute-deviation activity figure
(deliberately distinct from the between-class measure used to pick `otsu`), with
an over-200 hold so sudden high-activity frames do not jitter the guide map.

---

## 8. Dynamic-range path (histogram equalisation)

### 8.1 Coefficient validation

```
if u32(range_max - 0x200) > 0xE00
   or u32(eq_gain) > 100
   or u32(cover_bias + 10) > 0x14:
        range_max = 0x500; eq_gain = 0x28; cover_bias = 10
```

### 8.2 Derived coefficients

```
sd      = population_stddev(win_avg[0..383])      # integer: sqrt(max(0, sq/n - mean^2))
strength = clamp(eq_gain * sd / 50, 1, 100)
cover    = clamp(sd / 5 + cover_bias, 0, 19)
gidx     = clamp(pre_gamma_offset + 4, 0, 10)
```

`cover` selects an `EQ_KERNEL` row; the 20 rows correspond to `cover` 0..19.

### 8.3 Static alpha ramp

```
inc = white_slope * black_level * 32
dec = -black_slope  * white_level * 256
for i = 0..255:
    if i < black_level:        alpha_ramp[i] = inc / black_level
    elif i > white_level:      alpha_ramp[i] = dec / (256 - white_level)
    else:                      alpha_ramp[i] = 0
    inc -= white_slope * 32
    dec += black_slope * 256
```

`black_level == 0` or `white_level == 256` divides by zero (see 15).

### 8.4 Weighting histogram and cumulative weight

Auto-weighting is disabled:

```
alpha_ramp_auto[0] = 0        # remaining entries are zero
for i = 0..255: weight_hist[i] = 0x271000 / (alpha_ramp[i] + 0x100 + alpha_ramp_auto[i])
usum = sum_{i} weight_hist[i]
```

Then, with `usum` as the divisor:

```
cum = 0
for i = 0..255:
    cum += weight_hist[i]
    weight_cum[i] = cum
    g = (cum * 0x20000) / usum
    g = max(0, g - 0x200)
    g = min(g, 0x20000)
    weight_gamma[i] = g
    if i != 0: weight_balance[i] = (g + i/2) / i
weight_balance[0] = weight_balance[1]
```

`weight_balance` is an inverse-slope weight; dividing by `i` makes it large in
the dark end, so the final weighting flattens shadows.

### 8.5 Weighted input histogram

Add a small pedestal proportional to the clipped top of the raw histogram so the
equaliser cannot produce empty tails:

```
top  = sum_{i=250..255} hist_raw[i]
bias = (top + 0x7F) / 0xFF
norm_hist[i] = hist_raw[i] + bias,  i = 0..255
```

### 8.6 Equalisation convolution

Convolve `norm_hist` (256 taps) with the selected `EQ_KERNEL` row (31 taps). Each
product is shifted before accumulation:

```
E[t] = sum_{j} (norm_hist[j] * EQ_KERNEL[cover][t-j]) >> 10
       over all j with 0 <= j <= 255 and 0 <= t-j <= 30
t = 0..285
```

Then keep the central 254 taps as the working histogram:

```
F[i] = E[i + 14],  i = 0..253
fix_sum = sum F[i]
if fix_sum == 0: return            # no curve is written this frame
```

### 8.7 Integrated gamma profile

```
run = 0
for i = 0..253:
    run += F[i]
    v = ((run * 0x1000 + fix_sum/2) / fix_sum) - 0x10
    HGamma[i] = clamp(v, 0, 0xFFF)
HGamma[254] = HGamma[253]
HGamma[255] = HGamma[253]
```

### 8.8 Pre-gamma mapping

```
for i = 1..255:
    gt = gamma_lut[i]
    if gt == 0:
        tone_map[i] = 0x200
    else:
        j = clamp(HGamma[i] >> 4, 0, 255)
        v = PRE_GAMMA[gidx][j] << 9
        if gamma_mode == fixed:
            v = v / i
            if v < 0: v += 0xF
            v >>= 4
        else:
            v = v / gt
        tone_map[i] = v
tone_map[0]   = tone_map[1]
tone_map[255] = tone_map[254]
```

### 8.9 Strength scaling and peak normalisation

```
for i = 0..255:
    v = ((tone_map[i] - 0x200) * strength + 0x32) / 100 + 0x200
    tone_map[i] = v
    curve[i] = v
```

Then scale toward the requested peak level:

```
mi = index of the first maximum of tone_map[0..255]
maxval = tone_map[mi]
if range_max < maxval:
    for i: curve[i] = ((tone_map[i]-0x200)*range_max + maxval/2)/maxval + 0x200
else:
    for i: curve[i] = tone_map[i]
```

### 8.10 Slope-balance weighting

```
for i = 0..255:
    v = curve[i] * weight_balance[i]
    r = (v + 0x100) >> 9                 # if v < 0, add 0x2FF before the shift
    curve[i] = clamp(r, 0, 0xFFF)
```

The dispatcher then applies the temporal prefilter of 11.1 before the shared
blend.

---

## 9. Luma-hold path

Replaces the other two stages entirely. Inputs: `hist_raw`, `gamma_lut`, the
configuration, `GUIDE_*`, `CONVERGE`, and the persistent result fields.

### 9.1 Normalised histogram

```
hist_total = sum_{i=0..255} hist_raw[i]
norm_hist[:] = 0
for i = 0..255:
    j = gamma_lut[i*4] >> 4              # 0..255
    norm_hist[j] += hist_raw[i]
for j = 0..255:
    norm_hist[j] = round_u32(norm_hist[j] / hist_total * 4096)
```

`hist_total == 0` divides by zero in floating point (see 15).

### 9.2 Statistic

Run section 7 on `norm_hist`; this fills `cum_hist`, `avg_lum`, `avg_var`.

### 9.3 Peak-level slew

Bilinearly interpolate `peak_map` at the (luminance, activity) coordinate and
slew `peak_level` toward it by at most 4 per frame, with an absolute deadband of
20 and a floor of `0x80`:

```
li = min(avg_lum >> 5, 8);  lr = avg_lum & 31
vi = min(avg_var >> 5, 8);  vr = avg_var & 31
P = peak_map
maxval_cal = ((32-lr) * (((32-vr)*P[li][vi] + vr*P[li][vi+1]) >> 5)
            +    lr   * (((32-vr)*P[li+1][vi] + vr*P[li+1][vi+1]) >> 5)) >> 5

cur  = peak_level
diff = maxval_cal - cur
if |diff| > 20:
    if maxval_cal < cur: peak_level = (cur - maxval_cal < 5) ? maxval_cal : cur - 4
    else:                peak_level = (diff < 5)             ? maxval_cal : cur + 4
    if peak_level < 0x80: peak_level = 0x80
```

### 9.4 Divergence index

```
thresh = hist_pixel_count * 16
acc = 0
for i = 0..255:
    acc += norm_hist[i]
    if acc >= thresh: div_index = i; break
```

If the threshold is never reached, `div_index` retains its previous value (see
15).

### 9.5 Floor/cap clamping and total normalisation

```
dark  = clamp(dark_floor,   0, 0x80) >> 3
bright= clamp(bright_floor, 0, 0x80) >> 3
cap   = peak_level >> 3
total = 0
for i = 0..255:
    lim = bright
    if i <= div_index:
        lim = interp(i, 0, div_index, dark, bright)   # linear in i
    h = norm_hist[i]
    adj = (h > cap) ? cap : (h < lim ? lim : h)
    adj_hist[i] = adj
    total += adj

if total < 4096:                      # top up to exactly 4096
    d = 4096 - total
    if d > 0xFF: for all i: adj_hist[i] += d >> 8; d &= 0xFF
    for i = d down to 1: adj_hist[i] += 1
elif total != 4096:                   # remove the excess
    d = total - 4096
    i = 0
    while d != 0:
        if bright < adj_hist[i]: d--; adj_hist[i] -= 1
        i++; if i > 256: i = 0
```

`interp(x, x0, x1, y0, y1) = y0` when `x1 == x0`, else
`y0 + (y1-y0)*(x-x0)/(x1-x0)`. The excess-removal loop can fail to terminate if
the excess exceeds the amount that can be taken from bins above `bright` (see
15).

### 9.6 Cumulative

```
cum_hist[0] = adj_hist[0]
for i = 1..255: cum_hist[i] = cum_hist[i-1] + adj_hist[i]
```

### 9.7 Correlations, guide selection, merge ratio

With `c = 1/4096`:

```
gry = sum_{i=0..255} (norm_hist[i]*c) * (cum_hist[i] - GUIDE_LINEAR[i]) * c
```

Guide-polarity state machine (hysteresis of 10 consecutive contradictory
frames). `guide_state` and `guide_streak` are persistent; `gry` is the sign
source:

```
if guide_state < 1:
    if guide_state == 0:
        if gry == 0.0: guide = GUIDE_LINEAR; guide_streak = 0
        else:
            guide_streak += 1
            if guide_streak < 10: guide = GUIDE_LINEAR
            else: reclassify()
    else:                                   # guide_state == -1
        if gry < 0.0: guide = GUIDE_LOW; guide_streak = 0
        else:
            guide_streak += 1
            if guide_streak < 10: guide = GUIDE_LOW
            else: reclassify()
else:                                       # guide_state == +1
    if gry <= 0.0:
        guide_streak += 1
        if guide_streak < 10: guide = GUIDE_HIGH
        else: reclassify()
    else: guide = GUIDE_HIGH; guide_streak = 0

reclassify():
    guide_streak = 0
    if gry > 0.0:  guide_state = +1; guide = GUIDE_HIGH
    elif gry < 0.0: guide_state = -1; guide = GUIDE_LOW
    else:           guide_state = 0;  guide = GUIDE_LINEAR
```

Then:

```
gud = sum_{i=0..255} (norm_hist[i]*c) * (guide[i] - GUIDE_LINEAR[i]) * c
merge_ratio = (gry == 0.0) ? 0.0 : gud / (gud - gry)
if gry < 0.0: merge_ratio = -merge_ratio

use = ratio_hold
if guide_streak == 0 and |(merge_ratio - use) * 1000| > 9:
    ratio_hold = merge_ratio
    use = merge_ratio
```

`ratio_hold` only moves in a stable state (`guide_streak == 0`) and only for
changes above 0.009.

### 9.8 Merge curve and gamma round-trip

```
M[0] = 0; M[255] = 0x1000
w = |use|
for i = 1..254:
    v = guide[i]*(1-w) + cum_hist[i]*w
    v += (brightness * GUIDE_LINEAR[i]) >> 7
    M[i] = clamp(v, 0, 0x1000)

for i = 0..255: merge_curve[i] = M[gamma_lut[i*4] >> 4]
for i = 0..255: merge_curve[i] = inv_map(merge_curve[i])
```

`inv_map(v)` inverts `gamma_lut` piecewise-linearly:

```
if v == 0: return 0
for j = 0..1022:
    if gamma_lut[j] <= v <= gamma_lut[j+1]:
        return (v - gamma_lut[j])*4 / (gamma_lut[j+1] - gamma_lut[j]) + j*4
return 0xFFF
```

### 9.9 Output curve, WDR downscale, smoothing

```
for i = 0..255:
    if GUIDE_LINEAR[i] == 0: curve[i] = 0x200
    else: curve[i] = min((merge_curve[i] << 9) / GUIDE_LINEAR[i], 0xFFF)

if not wdr_en:
    b = 0; passes = 2; span = 256
else:
    b = bit_offset & 0xFF
    passes = (2 << ((b + 1) & 0xFF)) & 0xFF
    span = 256 >> b
    for i = 0..255:
        curve[i] = (i < span) ? curve[i << b] : 0x200

sel = span - 1
for rep = 0..passes-1:
    for i = 1..sel-1:
        if i == 1 or i == span-2:
            scratch[i] = (curve[i-1] + curve[i+1] + 2*curve[i]) >> 2
        else:
            scratch[i] = (curve[i-2] + curve[i+2]
                          + 2*(curve[i-1] + curve[i+1]) + 4*curve[i]) / 10
    for i = 1..sel-1: curve[i] = min(scratch[i], 0xFFF)
```

### 9.10 Step-limited temporal smoother

Limit how far each entry may move from the previously applied curve, using one
row of the convergence bank:

```
for i = 0..255:
    last = curve_prev[i]
    delta = min(|last - curve[i]|, 127)
    step = CONVERGE[26][delta]
    curve[i] = (last < curve[i]) ? last + step : last - step
    curve_prev[i] = curve[i]
```

Only row 26 of `CONVERGE` is used, and only columns 0..127.

---

## 10. Shared brightness/contrast S-curve (blend)

Applies the same S-curve shape in all non-luma-hold modes. It consumes the
user brightness/contrast and the current `curve`, and writes `curve`.

```
c = contrast; b = brightness
if c < 0: c = c / 2; b = c + b            # truncating division

v = (c + 85) * (-128) + b*100 + 9800
for i = 0..320:
    t = v
    if v > 0x639B: t = 0x639C
    v += c + 85
    if t < 1: t = 1
    curve_tbl[i] = t

for i = 0..255: blend_curve[i] = (sum(curve_tbl + i, 64)) >> 6

base = blend_curve[0]
if contrast < 0: base = base / 2
for i = 1..255: blend_curve[i] -= base
if blend_curve[255] == 0: blend_curve[255] = 1

for i = 0..255:
    t = (blend_curve[i] << 12) / blend_curve[255]
    blend_curve[i] = min(t, 0x1000)
# all 256 entries are normalised, including blend_curve[255] (which becomes
# 0x1000 whenever it is non-zero)

for i = 0..255:
    if i == 0 or blend_curve[i] == 0:
        curve[i] = 0
    else:
        t = (blend_curve[i] << 9) / i
        if mode == fixed:
            if t < 0: t += 0xF
            out = (unsigned short)(t >> 4)
        else:
            t = wrapping_mul_32(t, curve[i])
            if t < 0: t += 0x1FFF
            out = (unsigned short)(t >> 13)
        curve[i] = out
        if curve[i] >= 0x1000: curve[i] = 0xFFF

brightness_saved = brightness
contrast_saved   = contrast
```

Notes:

- The 321-entry ramp is a running sum of the brightness/contrast linear term; the
  64-tap box sum then converts it into a smooth S-curve.
- `fixed` overwrites `curve` with the projection scaled by `1/16`; all other
  modes multiply the projection into the existing `curve` using a 32-bit
  wrapping multiply and a `2^-13` scale.

---

## 11. Temporal smoothers

### 11.1 Dynamic-range prefilter

A fixed 15/16-old + 1/16-new IIR applied entry-wise, after the equalisation
stage and before the blend:

```
curve[i] = (curve_prev[i]*15 + curve[i] + 8) >> 4
curve_prev[i] = curve[i],  i = 0..255
```

### 11.2 Luma-hold step limiter

As in 9.10: the change per entry and frame is bounded by
`CONVERGE[26][min(|desired - previous|, 127)]`, applied in the direction of the
desired change, and `curve_prev` is kept equal to the applied curve.

---

## 12. Table semantics

### 12.1 Guide profiles — `GUIDE_LINEAR`, `GUIDE_LOW`, `GUIDE_HIGH`

Each is a 256-entry monotone reference cumulative profile on the same `0..4096`
scale as the normalised cumulative histogram. `GUIDE_LINEAR` is the neutral
identity reference (used as the anchor in both correlations and in the merge
term); `GUIDE_LOW` is biased toward shadows and `GUIDE_HIGH` toward highlights.
Indices are luma `0..255`. The values are signed, but only their relative shape
matters; the implementation must read them through the injection API.

### 12.2 Pre-gamma bank — `PRE_GAMMA`

11 rows x 256 entries. Row `gidx = clamp(pre_gamma_offset+4, 0, 10)` is selected.
An index derived from a 12-bit value (`HGamma >> 4`, 0..255) is mapped to a
transfer value; the equalisation stage shifts that value left by 9 and divides
it by either the luma index (`gamma_mode == fixed`) or the per-pixel
`gamma_lut` entry (`gamma_mode == dynamic`).

### 12.3 Equalisation kernel bank — `EQ_KERNEL`

20 rows x 31 taps; row `cover` (0..19) is selected by the frame's activity
estimate. Taps are Q10 coefficients. The bank is applied as the convolution of
section 8.6; only the central 254 outputs are used. The bank smooths and shapes
the weighted input histogram into an equalised cumulative distribution.

### 12.4 Step-limit bank — `CONVERGE`

32 rows x 128 columns of unsigned bytes. Only row 26 is read. Column `d`
(0..127) is the maximum number of curve units an entry may move this frame when
its desired absolute change is `d`. The desired change is clamped to 127 before
lookup, and the sign of the move follows the desired direction. This bounds
per-frame flicker while allowing a rapid initial convergence.

### 12.5 Per-instance gamma map — `gamma_lut`

A monotone 1024-entry map carried in the parameter block (not an injected
table). It is used two ways: as a domain permutation (`gamma_lut[i*4] >> 4` maps
to 0..255) when building and remapping histograms/curves, and inverted
piecewise-linearly (`inv_map`, section 9.8) to map a `0..4096` curve value back
through the map. The inverse step assumes the map is non-decreasing; if it is
not, a step can divide by zero or return a negative segment width.

### 12.6 Peak map — `peak_map`

A 9x9 signed table in the parameter block. It maps (luminance class, activity
class) to a target peak level, bilinearly interpolated at (avg_lum, avg_var) as
in 9.3. It is not an injected table but is runtime tuning data.

---

## 13. Constants

| Constant | Value | Role |
| --- | --- | --- |
| mode values | 0..3 | fixed, dynamic-range, dynamic-gamma, luma-hold. |
| gamma-mode values | 0,1 | fixed, dynamic. |
| curve range | 0x000..0xFFF | 12-bit output clamp. |
| Q12 scale | 4096 | Histogram and merge-curve domain. |
| default range max | 0x500 | Fallback for `range_max`. |
| default eq gain | 0x28 (40) | Fallback for `eq_gain`. |
| default cover bias | 10 | Fallback for `cover_bias`. |
| activity divisor | 50 | `strength = eq_gain*sd/50`. |
| cover divisor | 5 | `cover = sd/5 + cover_bias`, clamp 0..19. |
| pre-gamma offset | +4 | Row index bias, clamp 0..10. |
| alpha scale | 32 / 256 / 0x271000 | Ramp and weighting denominators. |
| cum gamma scale | 0x20000 | Weighted cumulative scale. |
| cum gamma floor | 0x200 | Subtracted before capping. |
| convolution shift | 10 | Per-tap Q10 shift. |
| top-bin window | 250..255 | Clipped-highlight pedestal source. |
| pedestal divisor | 0xFF | `bias = (top+0x7F)/0xFF`. |
| central window | offset 14, length 254 | Equaliser output slice. |
| gamma integration | 0x1000, -0x10 | Integration scale and offset. |
| pre-gamma shift | 9 | `PRE_GAMMA << 9`. |
| strength blend | `+0x32`, `/100` | `((v-0x200)*strength+50)/100+0x200`. |
| balance shift | 9, round 0x100 | Slope-balance weighting. |
| prefilter | 15/16, round 8, shift 4 | Dynamic-range IIR. |
| Otsu bins | odd 1..253 | Threshold search set. |
| Otsu hist total | 4096 | Class-weight base. |
| slew deadband | 20 | Min peak-map change to act. |
| slew step | 4 | Max per-frame peak movement. |
| peak floor | 0x80 | Minimum `peak_level`. |
| divergence scale | 16 | `hist_pixel_count * 16`. |
| floor clamp | 0..0x80, >>3 | dark/bright floor. |
| cap divisor | 8 | `cap = peak_level >> 3`. |
| floor total | 4096 | Forced `adj_hist` total. |
| ratio scale / threshold | 1000 / 9 | `|delta|*1000 > 9`. |
| merge brightness shift | 7 | `(brightness*GUIDE_LINEAR)>>7`. |
| inverse-map scale | 4 | `inv_map` segment width. |
| curve divide shift | 9, cap 0xFFF | `curve = merge*512/guide`. |
| WDR passes | `(2<<((b+1)&0xFF))&0xFF` | Smoothing pass count. |
| WDR span | `256 >> b` | Active curve prefix. |
| convergence row | 26 | Only row used. |
| convergence width | 128 | Delta clamp 0..127. |
| blend ramp | 85, -128, *100, +9800 | Linear term for the S-curve. |
| blend ramp ceiling | 0x639C | Saturation value. |
| blend ramp length | 321 | Ramp entries. |
| blend box | 64, >>6 | 64-tap moving-average. |
| blend normalise | Q12, 0x1000 | `(blend<<12)/blend[255]`. |
| blend projection | <<9, fixed >>4, else >>13 | Per-mode scaling. |
| avg-var saturation | 255 | `min(raw, 0xFF)`. |
| avg-var hold | 200 | Re-use threshold. |
| avg-var scale | 5 | `raw = (...)>>12 * 5`. |
| init sentinel | 0xFFFF0001 | Saved brightness/contrast. |
| init curve seed | 0x200 | Used only when `curve[255] == 0`. |
| first valid frame | > 2 | `frame_index` gating. |

---

## 14. Edge cases and failure behaviour

- **Empty / all-zero histogram.**
  - `dynamic-range`: `fix_sum == 0` aborts before writing `curve`; the working
    state buffers are still updated. The temporal prefilter and blend do not run
    for that frame.
  - `luma-hold`: `hist_total == 0` makes the normalisation divide by zero in
    floating point (result undefined/NaN-cast). The Otsu scan then ties on all
    odd bins and yields 127; the floor/cap stage forces `adj_hist` to total
    4096, so *some* curve is still produced.
- **Clipped highlights.** The pedestal built from bins 250..255 keeps the top of
  the equaliser histogram non-empty. The output curve is always clamped to
  `0..0xFFF`; the blend additionally clamps at `>= 0x1000` to `0xFFF`.
- **Clipped/empty guide divide.** If `GUIDE_LINEAR[i] == 0`, the luma-hold
  output at that index is forced to `0x200` instead of dividing.
- **`black_level == 0` or `white_level == 256`.** Division by zero in the alpha
  ramp (undefined).
- **`gamma_lut` non-monotone.** `inv_map` may divide by a zero or negative
  segment width; the map is assumed monotone.
- **Peak-map indices.** `avg_lum`/`avg_var` are at most 255, so `>>5` yields at
  most 7; the clamp to 8 is defensive only and the `+1` access stays in bounds.
- **`div_index` not found.** If the cumulative weight never reaches the
  threshold, `div_index` keeps its previous-frame value (the implementation
  should not assume it is zeroed each frame).
- **Floor/cap total removal.** The excess-removal loop stops only when the
  excess reaches zero; if the excess exceeds what can be removed from bins
  strictly above `bright`, it can spin. The total-top-up path is likewise
  bounded by the modulo-256 split.
- **Temporal ratio update.** `ratio_hold` advances only when `guide_streak == 0`
  and the absolute change exceeds 0.009; otherwise the previous ratio is used.
- **`gud == gry` with non-zero `gry`.** The merge ratio divides by zero; callers
  must supply smooth guide/cumulative data.
- **WDR bit offset.** `span = 256 >> b` and `passes = (2 << ((b+1)&0xFF)) & 0xFF`
  are unsigned/byte arithmetic; large `b` underflows `span` to 0 and can make
  the smoothing loop degenerate. The index `i << b` assumes `b < 8`.
- **Mode value outside 0..3.** Treated as a non-fixed blend (equalisation and
  luma-hold are skipped). There are no further modes.
- **Null pointers.** The run entry point returns `-1` without touching state.
  A null statistics pointer is not silently accepted.
- **`gamma_lut[i] == 0` in the equalisation stage.** That entry is forced to
  `0x200` rather than divided.
- **Signed/rounding details.** Multiply-shift sequences round negatives by
  adding the shift's half-range before an arithmetic shift; the blend's
  non-fixed multiply uses 32-bit wraparound. These must be reproduced exactly.
- **Reserved / historical modes.** The deployed object implements only the four
  modes above; no additional tone-mapping modes are supported.

---

## 15. Open questions / ambiguities

1. **`div_index` reset.** The luma-hold path leaves `div_index` at its previous
   value when the threshold is never reached, whereas a host reference zeroes it
   first. Which is intended for a fresh instance (or after a statistics reset)
   is unclear; the spec records the no-reset behaviour as observed.
2. **Top-bin pedestal indexing.** The weighted-histogram pedestal derivation has
   a boundary ambiguity at the last bin. Whether the final bin receives the
   pedestal or is left at zero is not fully determinable from the object; the
   implementation should confirm against golden vectors.
3. **Floor clamp provenance.** The dark/bright floors are clamped to `0..0x80`
   before the divide-by-8 in the reference port; the object performs the divide
   directly on the configured value. Whether the clamp is needed for valid
   configurations is unresolved.
4. **Excess-removal termination.** The luma-hold total-reduction loop has no
   explicit bound; the minimum guarantee that it always terminates (given
   realistic floor/cap settings) is not proven.
5. **Guide hysteresis rationale.** The "10 consecutive contradictory frames"
   counter and its reset conditions are empirical. Whether the intent is noise
   rejection or scene-change detection is unknown; the spec encodes the literal
   state machine.
6. **`gud/(gud-gry)` singularity.** The merge ratio can divide by zero for
   particular guide/data combinations; no guard is present. It is unclear
   whether the injected guide profiles make this unreachable in practice.
7. **WDR downscale intent.** The `span`/`passes` derivation from `bit_offset`
   and the eventual meaning of the `0x200` filler are not documented; only the
   arithmetic is specified.
8. **Activity statistic scale.** `avg_var` is a mean absolute deviation scaled
   by 5 and saturated to 255, deliberately different from the between-class
   measure used to pick the threshold. The reason for the two different measures
   is not derivable from the object.
9. **`peak_map` shape semantics.** The 9x9 map's axes are inferred
   (luminance/activity) from the coordinate construction; the intended class
   boundaries and the choice of 9x9 are not specified by the algorithm.
10. **Pre-gamma row offset.** The `+4` applied to `pre_gamma_offset` before the
    0..10 clamp suggests the bank's neutral row is 4, but this is not stated
    anywhere in the object.
