<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Local Tone Mapping — Behaviour Specification

This document specifies observable behaviour only. It contains no source code, no
vendor identifiers, and no reference string literals. Names in `monospace` are
neutral descriptions of the data the module consumes and produces; the exact ABI
field names and offsets are supplied by the compatibility layout header and by
`interfaces.md`.

---

## 1. Purpose and scope

The module prepares, once per qualified frame, the control data for a **local**
tone-mapping stage. Its outputs are lookup tables and scalar knobs, not a
per-pixel image:

- a **per-tile merge weighting table** (horizontal and vertical), used to blend
  the tone-mapped detail layer with the original image across neighbouring tiles;
- a **per-level tone floor table** and a **per-level adaptation-gain table**,
  both indexed by luma level;
- scalar knobs: an original-picture ratio, a transform order, a last-order ratio,
  an output minimal-level target, an automatic-exposure compensation value, and
  a per-frame strength value.

It consumes per-tile local-contrast statistics (768 values), a measured minimum
level, runtime configuration, and two runtime-injected tuning banks. It carries a
**strength** value across frames under a convergence limiter, and selects one of
nineteen quantised parameter presets from that strength.

The module never loops over image pixels. Its only data-dependent loop over
measured data is over the 768 per-tile statistics entries. The per-pixel
evaluation is downstream, driven by the emitted tables.

---

## 2. Conventions and symbols

- Arithmetic is 32-bit two's-complement integer unless stated otherwise;
  divisions truncate toward zero.
- `clamp(x, lo, hi) = min(max(x, lo), hi)`.
- `sat8(x)` clamps to `0..0xFF`; `sat12(x)` clamps to `0..0xFFF`.
- `asr(v, s)` is an arithmetic right shift with the count guarded:
  `s <= 0 -> v`; `s > 31 -> v >> 31`; else `v >> s`.
- `floor0(x)` is `x & ~(x >> 31)`: it maps a negative value to 0 and leaves
  non-negative values unchanged. It is used before applying an upper clamp.
- `>>` on a signed negative value is an arithmetic shift (floor toward negative
  infinity).
- `u8(x)` / `u16(x)` denote truncation to the low 8 / 16 bits.
- `u32(x)` reinterprets `x` as unsigned.
- `sum_{j=a..b} f(j)` denotes the inclusive sum.
- **level axis**: the luma index `0..255` used by the tone and gain tables.
- **tile**: one statistics block; the picture is divided into
  `(block_cols+1) x (block_rows+1)` tiles.
- **Q10 unity** is `0x400`; **Q12 unity** is `0x1000`.
- `strength` is a 12-bit value `0..0xFFF` whose high byte selects a preset and
  whose low byte selects an interpolation fraction between adjacent presets.
- Names in `monospace` are this document's neutral names, not ABI names.

---

## 3. Input contract

### 3.1 Configuration (static tuning)

Read from per-instance parameter storage on every run. Indices are relative to
the start of the configuration array. Fields not listed are unused by this
module.

| Neutral name | Meaning | Type | Range / notes |
| --- | --- | --- | --- |
| `mode` | Strength-control mode | enum int | `0` = automatic/fixed-level, `1` = semi-manual, `>= 2` (and negatives) = manual/static. See section 7. |
| `oripic_ratio_cfg` | Manual original/picture ratio | signed int | Used only in manual/static mode; passed straight to the output. |
| `order_cfg` | Manual transform order | signed int | Used only in manual/static mode. |
| `last_order_ratio_cfg` | Manual last-order ratio | signed int | Used only in manual/static mode. |
| `clip_cfg` | Manual tone-floor increment | signed int | Manual/static mode only; feeds the tone-table floor. |
| `gain_cfg` | Manual gain blend factor | signed int | Manual/static mode only; feeds the gain-table blend. |
| `lss_switch` | Unreferenced by this module | signed int | — |
| `lum_ratio` | Unreferenced | signed int | — |
| `lp_halo_res` | Unreferenced | signed int | — |
| `white_level` | Unreferenced | signed int | — |
| `spatial_asm` | Unreferenced | signed int | — |
| `intens_asym` | Unreferenced | signed int | — |
| `block_rows` | Vertical tile divisions minus one | signed int | Picture height is divided into `block_rows+1` tile rows. |
| `block_cols` | Horizontal tile divisions minus one | signed int | Picture width is divided into `block_cols+1` tile columns. |
| `contrast` | Contrast trim | signed int | Multiplies the tone-floor increment by `(256 - contrast)/256`; see 7.7. |
| `tolerance` | Minimum-level deadband | signed int | `0` is replaced by 5. |
| `speed` | Convergence row selector | signed int | Used only after frame `0x50`; selects a row of the converge bank. |
| `step` | Fixed strength step | signed int | `0` is replaced by 2. |
| `interval` | Frame interval | signed int | `0` is replaced by 1. Strength recomputation happens on frames where `(frame_id - start) % interval == 0`. |

### 3.2 Dynamic configuration

| Neutral name | Meaning | Type | Range / notes |
| --- | --- | --- | --- |
| `auto_strength` | Automatic strength selector / scale | signed int | See 7.2 and 7.7. Encodes `row = value/64`, `frac = value%64` when used to build the working strength curve. Values `< 17` select a fixed strength level instead of statistics. |
| `manual_strength` | Semi-manual strength level | signed int | In semi-manual mode, calibrated strength `= value << 4`; also used as the first-frame seed. |
| `ae_comp` | Automatic-exposure compensation | signed int | Copied to the output as a byte each enabled run. |
| `min_threshold` | Minimum-level target override | signed int | When non-zero, the target is `value << 4`; when zero, derived from the calibrated strength. |

### 3.3 Sensor descriptor

| Neutral name | Meaning | Type | Range / notes |
| --- | --- | --- | --- |
| `sensor_width` | Active width in pixels | signed int | Used for the tile geometry. |
| `sensor_height` | Active height in pixels | signed int | Used for the tile geometry. |
| `wdr_mode` | Wide-dynamic-range mode | unsigned int | Exactly `1` selects the sparse tone-table and forces the gain blend to zero. |
| `ae_settled` | Exposure-settled flag | byte | When zero, the automatic path is suspended (see 6). |
| `backlight` | Backlight flag | byte | Present in the descriptor; not consumed by this module. |

`bit_offset` (below) is carried in the parameter block and is not part of the
sensor descriptor.

### 3.4 Source curve pointer

| Neutral name | Meaning | Type | Range / notes |
| --- | --- | --- | --- |
| `source_curve` | Tuning curve source | pointer to unsigned short | At least `0x300` entries. Sections `[0x100..0x1FF]` and `[0x200..0x2FF]` are read; `[0x000..0x0FF]` is not read here. |
| `bit_offset` | Wide-dynamic-range level shift | signed int | In `wdr_mode == 1`, controls the sparse sampling stride and the level maximum. |

### 3.5 Injected tables

Read-only, obtained through the runtime table-injection API (see
`interfaces.md`); never compiled in.

| Neutral name | Meaning | Type | Shape |
| --- | --- | --- | --- |
| `strength_bank` | Local-contrast-to-strength curve bank | signed int | `[4][256]` |
| `converge_bank` | Per-frame strength-step limit bank | unsigned byte | `[32][128]` |

Both tables are only read on the paths that call the corresponding generator
(sections 7.3 and 8.4).

### 3.6 Statistics block

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `measured_min` | Measured minimum level after tone mapping | unsigned short | 12-bit, `0..0xFFF` |
| `lst[768]` | Per-tile local-contrast statistic | unsigned short[768] | Each entry must be `<= 0xFFF` for in-range lookup (see 14) |

Exactly 768 statistic entries are read.

---

## 4. Output contract

On every run (section 6) the module writes the following. Fields are neutral
names; the ABI offsets are fixed by the compatibility layout header.

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `tbl[768]` | Output lookup tables, three sections of 256 | unsigned short[768] | see 9 |
| `oripic_ratio` | Original/picture blend ratio | signed int | `0..0xFF` |
| `order` | Transform order | signed int | `5..13` on the strength path; config value on the static path |
| `last_order_ratio` | Last-order ratio | signed int | `0..15` |
| `cal_en` | "Strength was computed" flag | signed int | `0` or `1` |
| `frame_smooth_en` | "Apply temporal smoothing" flag | signed int | `0` or `1` |
| `block_width` | Tile columns minus one | signed int | `sensor_width/(block_cols+1) - 1` |
| `block_height` | Tile rows minus one | signed int | `sensor_height/(block_rows+1) - 1` |
| `stat_scale` | Statistics reciprocal scale | unsigned int | `u32( 2^32 / (block_width+1)/(block_height+1) )` |
| `ae_comp` | Exposure compensation copy | byte | low 8 bits of `ae_comp` config |
| `old_strength` | Previous applied strength | unsigned short | `0..0xFFF` (see 14 for the seed) |
| `next_strength` | Newly computed strength | unsigned short | `0..0xFFF` (see 14 for the seed) |
| `cal_strength` | Strength target from mode | unsigned short | `0..0xFFF` |
| `min_threshold` | Applied minimum-level target | unsigned short | `0..0x1000` |

Entry-point status returns:

- run: `-1` if the instance, statistics or result pointer is null, else `0`;
- get-parameters: a pointer to the instance parameter storage, or `-1` if the
  instance pointer is null;
- set-parameters: `0` (no-op);
- set-default-result: `0` (no-op; the deployed body is empty).

`cal_en` and `frame_smooth_en` are latched: once set on a qualifying frame they
are not cleared on subsequent non-qualifying frames (see 6 and 14).

---

## 5. Internal state and lifecycle

Allocated once per instance and zeroed. Private per-instance state:

| Neutral name | Type | Role |
| --- | --- | --- |
| `config` | parameter block | The live configuration (section 3.1–3.4). |
| `busy_flag` | signed int | Set to 1 at allocation; otherwise unused. |
| `interval_frame` | signed int | Configured `interval`, defaulted to 1. |
| `strength_curve[256]` | signed int[256] | Working strength curve, rebuilt by 7.3. |
| `min_change_cnt` | signed int | Consecutive-frame confirmation counter for the minimum-level feedback. |
| `sensor_w_old` | signed int | Last sensor width used for the tile geometry. |
| `sensor_h_old` | signed int | Last sensor height. |
| `block_cols_old` | signed int | Last horizontal tile count. |
| `block_rows_old` | signed int | Last vertical tile count. |

One module-global integer, `start_frame` (initial value 3), holds the first
frame index at which automatic processing is permitted. It is rewritten when the
tile geometry changes (section 6).

Lifecycle:

- **init:** zero the instance, set `busy_flag = 1`, zero `strength_curve` and the
  four saved geometry fields, and return the ops table.
- **exit:** free the instance.
- **per frame:** the run entry point calls the control entry point; it updates
  the output tables and scalars and advances the strength value.
- **persistent across frames:** `strength_curve`, `min_change_cnt`, the four
  saved geometry fields, `start_frame`, the output `old_strength`, and the
  configuration block.

---

## 6. Per-frame dispatcher

Called once per frame from the run entry point. The sequence is:

```
tile_geometry_update()                        # 6.1, always
interval = max(config.interval, 1)
entity.interval_frame = interval

if config.enable == 0:
    cal_en = 0; frame_smooth_en = 0; ae_comp = 0; next_strength = 0
    return

ae_comp = u8(config.ae_comp)
start  = start_frame
fid    = config.frame_id

if fid < start:
    if fid == start - 1:
        cal_en = 1; frame_smooth_en = 0
        if next_strength == 0: next_strength = manual_strength
    else:
        cal_en = 0; frame_smooth_en = 0
else:
    if sensor.ae_settled == 0:
        min_change_cnt = 0
        return                                # old_strength is NOT updated
    if (fid - start) % interval == 0:
        cal_en = 1; frame_smooth_en = 1
        strength_control(stats, result)       # section 7
old_strength = next_strength
```

Consequences:

- Automatic processing is gated on `frame_id >= start_frame` and the exposure
  having settled. Before that, only a strength seed is written on the frame just
  before `start`.
- On non-qualifying frames past `start`, nothing is recomputed and the flags are
  left at their previous values; only `old_strength = next_strength` runs.
- On `ae_settled == 0` the dispatcher returns before the `old_strength` update,
  so the carried strength is intentionally frozen.

### 6.1 Tile geometry update

```
if block_rows_old != config.block_rows or block_cols_old != config.block_cols:
    start_frame = config.frame_id + 3

if sensor_h_old != sensor.height or block_rows_old != config.block_rows or
   sensor_w_old != sensor.width  or block_cols_old != config.block_cols:
    nw = sensor.width  / (config.block_cols + 1)
    nh = sensor.height / (config.block_rows + 1)
    block_width  = nw - 1
    block_height = nh - 1
    stat_scale   = u32( 2^32 / (nw * nh) )
    build_merge_table()                       # section 9.1
    sensor_h_old = sensor.height; sensor_w_old = sensor.width
    block_rows_old = config.block_rows; block_cols_old = config.block_cols
```

The merge table is rebuilt only when the geometry (sensor size or tile counts)
changes. `start_frame` is advanced when the tile counts change, so a geometry
change delays automatic processing by roughly three frames.

---

## 7. Strength-control paths

`strength_control` (called only on qualifying frames) has three effective paths,
selected by `mode`, plus one sub-variant:

| Path | Condition | Target strength source | Temporal limiter |
| --- | --- | --- | --- |
| Automatic | `mode == 0` and `auto_strength >= 17` | Local-contrast statistics through `strength_bank` | yes |
| Fixed-level | `mode == 0` and `auto_strength < 17` | `auto_strength << 4` | yes |
| Semi-manual | `mode == 1` | `manual_strength << 4` | yes |
| Manual/static | any other `mode` (i.e. `>= 2` or negative) | configured ratios/tables, no statistics | no |

On the manual/static path the module takes a shortcut: it copies
`oripic_ratio_cfg`, `order_cfg`, `last_order_ratio_cfg` to the output and uses
`clip_cfg`/`gain_cfg` directly as the tone-floor increment and gain blend. It
skips the preset machinery, the minimum-level feedback, the step limiter, and
the contrast/auto-strength modulation (section 7.7).

### 7.1 Automatic strength from statistics

```
strength_curve = strength_curve_build()        # 7.3
acc = 0
for k = 0..767: acc += strength_judge(stats.lst[k])
cal_strength = sat12( (acc * 16) / 768 )
```

`strength_judge` maps one 12-bit local-contrast statistic to a contribution:

```
strength_judge(x):
    i  = x >> 4                      # 8-bit coarse index
    off = config.bit_offset
    if i == 0xFF:
        return asr(strength_curve[0xFF] << 1, off)
    v = (strength_curve[i] * (16 - (x & 15)) + strength_curve[i+1] * (x & 15)) >> 4
    return asr(v << 1, off)
```

So a statistic is interpolated within the working curve with four fractional
bits, doubled, then reduced by the wide-dynamic-range bit offset. The average
over 768 entries is the automatic strength target; it is saturated to 12 bits.

### 7.2 Semi-manual / fixed-level target

```
cal_strength = (level << 4) mod 2^16
```

where `level` is `manual_strength` for `mode == 1`, or `auto_strength` when
`mode == 0` and `auto_strength < 17`. No saturation is applied here.

### 7.3 Working strength curve (`strength_curve`)

Built only on the fully automatic path. It selects or interpolates a curve from
the injected `strength_bank` according to the `auto_strength` encoding
`row = value / 64`, `frac = value % 64`:

```
for k = 0..255:
    if row == 0:
        base = strength_bank[0][k]
        sq   = (base * base) >> 8            # squared base curve
        strength_curve[k] = (frac == 0)
            ? sq
            : ((64 - frac) * sq + frac * base) >> 6
    else:
        lo = strength_bank[row - 1][k]
        hi = strength_bank[row][k]
        strength_curve[k] = (frac == 0)
            ? lo
            : (lo * (64 - frac) + hi * frac) >> 6
```

Semantics of the bank: four candidate monotone curves on the level axis.
Row 0 is special: its "squared" form is the strongest compression endpoint, and
the row-0 curve itself is the next point. Rows `>= 1` interpolate between
consecutive bank rows. Because the fractional term of row 0 blends the squared
base into the base row, the effective sequence of endpoints is
`squared(0), row0, row1, row2` with 64 sub-steps each; the top row is reachable
only with a non-zero fraction.

### 7.4 Minimum-level target

```
tol  = config.tolerance; if tol == 0: tol = 5
step = config.step;      if step == 0: step = 2
speed_index = 0
if config.frame_id >= 0x50: speed_index = config.speed

if config.min_threshold == 0:
    min_th = clamp( (cal_strength + 6) / 12, 1, 0x100 )
else:
    min_th = u16(config.min_threshold << 4)
min_threshold_out = min_th
```

### 7.5 Minimum-level feedback and confirmation

```
d = min_th - stats.measured_min             # wrapping 32-bit subtraction
if |d| < tol:
    entity.min_change_cnt = 0
else:
    a = clamp( 30 - ( ||d| - tol| ), 2, 30 )
    entity.min_change_cnt += 1
    if entity.min_change_cnt < a:
        d = 0                               # suppress, but keep direction sign
```

The confirmation gate requires a persistent minimum-level error: a small error
must persist for up to 30 qualifying frames before it is allowed to act, while a
large error is allowed to act sooner.

### 7.6 Strength step limiter

With `delta = cal_strength - old_strength` and

```
step_tbl = converge_bank[speed_index * 128 + min(|delta|, 127)]
```

the new strength is:

```
if |d| < tol:
    if delta < 0:       next = sat12(old_strength - step)
    else:               next = old_strength        # hold, never step up

elif d >= tol:
    if delta >= 1:      next = min(old_strength + step_tbl, 0xFFF)
    else:               next = sat12(old_strength - step)

else:   # d <= -tol
    if delta < 0:       next = sat12(old_strength - step_tbl)
    else:               next = sat12(old_strength + step)
```

Observations:

- The adaptive `converge_bank` step is used only when the minimum-level
  correction direction agrees with the requested strength change direction
  (raise strength when the measured minimum is too low; lower strength when it
  is too high). All other moves use the fixed `step` (default 2).
- Inside the `|d| < tol` deadband the strength is **held** whenever the requested
  change is positive (`delta > 0`) or zero; only a negative `delta` steps down by
  the fixed `step`. The hold applies on the WDR path as well.
- Because the confirmation gate may zero `d`, a suppressed correction falls into
  the `|d| < tol` deadband: an intended increase becomes a no-op, while an
  intended decrease still moves by the fixed `step`.
- The result is saturated to 12 bits.
- The speed index is only advanced after frame index `0x50`; before that the table's
  first row is always used.

### 7.7 Preset selection, interpolation, and modulation

The 12-bit `next_strength` is split into `high = next >> 8` and
`low = next & 0xFF`. The preset index is `idx = min(high, 15)`.

**Exact high byte (`low == 0`):** apply preset `idx` directly; set
`last_order_ratio = 15`.

**Fractional (`low != 0`):** take preset `idx` (`o0, t0, p0, g0`) and preset
`idx+1` (`o1, t1, p1, g1`) and interpolate with weight `w = 256 - low`:

```
oripic_ratio = sat8( (o0*w + o1*low) >> 8 )
order        = t0
last_order   = max( (low >> 4) - 1, 0 )
clip         = clamp( floor0( (p0*w + p1*low) >> 8 ), 0, 0x1000 )
gain         = clamp( floor0( (g0*w + g1*low) >> 8 ), 0, 0x1000 )
```

`order` is then bumped for intermediate strengths: with unsigned arithmetic,

```
if high > 2 and (u32(idx - 11) > 3) and (u32(idx - 11) != 4):
    order = clamp(t0 + 1, 5, 13)
else:
    last_order = 15
```

Because `idx <= 15`, the bump condition reduces to `3 <= high <= 10`; for
`high <= 2` and `high >= 11` the last-order ratio is forced to 15 and `order`
stays at `t0`.

**Contrast and automatic-strength modulation** (skipped in manual/static mode):

```
if sensor.wdr_mode == 1: gain = 0

clip = floor0( clip * (256 - config.contrast) / 256 )
clip = clamp( clip, 0, 0x1000 )

if auto_strength < 17:
    clip = (clip * auto_strength) >> 4
```

So `contrast == 0` leaves the clip unchanged, `contrast == 256` zeroes it, and a
small `auto_strength` scales the clip down by a factor of 16. In automatic mode
`auto_strength >= 17`, so the scale is inactive.

Finally the tone and gain tables are generated from `clip` and `gain`
(section 9.2).

### 7.8 The 19 quantised presets

`preset(i)` returns the four parameters below for every `i` in `0..18`. The
module's own callers pass indices `0..16`, so presets 17 and 18 are defined but
unreachable through the strength path. These are the platform's default
parameters; `../docs/provenance.md` records how they were obtained.

| Preset | `oripic_ratio` | `order` | `clip` (pre_adjust) | `gain` (ffunc) |
| --- | --- | --- | --- | --- |
| 0 | 0x0FF | 5 | 0x05A | 0x1000 |
| 1 | 0x080 | 5 | 0x0B4 | 0x0F7C |
| 2 | 0x040 | 5 | 0x168 | 0x0F3C |
| 3 | 0x030 | 5 | 0x2CD | 0x0F16 |
| 4 | 0x020 | 6 | 0x2DF | 0x0E56 |
| 5 | 0x010 | 7 | 0x339 | 0x0D8A |
| 6 | 0x002 | 8 | 0x393 | 0x0CE0 |
| 7 | 0x000 | 9 | 0x406 | 0x0AF0 |
| 8 | 0x000 | 10 | 0x564 | 0x078A |
| 9 | 0x000 | 11 | 0x604 | 0x0578 |
| 10 | 0x000 | 12 | 0x6F4 | 0x02A0 |
| 11 | 0x000 | 13 | 0x800 | 0x0150 |
| 12 | 0x000 | 13 | 0xA00 | 0x00A8 |
| 13 | 0x000 | 13 | 0xC00 | 0x0054 |
| 14 | 0x000 | 13 | 0xE00 | 0x002A |
| 15 | 0x000 | 13 | 0xED8 | 0x0015 |
| 16 | 0x000 | 13 | 0x1000 | 0x0000 |
| 17 | 0x000 | 14 | 0x800 | 0x0000 |
| 18 | 0x000 | 15 | 0x000 | 0x0000 |

Increasing preset index means: the original-picture ratio falls to zero, the
tone-floor increment rises, and the gain blend moves from unity-weighted to
source-weighted (`gain == 0x1000` replaces the source with Q10 unity `0x400`;
`gain == 0` keeps the source unchanged, section 8.3). The order rises from 5 to
15 across the table. The `clip` column is the tone-floor increment itself, used
directly as the running accumulator step; presets 17 and 18 carry 0x800 and 0x000.

---

## 8. Table generation

### 8.1 Merge weighting table (`tbl[0x000..0x0FF]`)

The merge table is built from the tile count in each axis. Two symmetric 3-tap
kernels are produced from truncated Gaussians and normalised to 256.

Given an axis span `n` (the tile count), define:

```
half = (n >> 1) + 1
top  = n + half
eta1 = (15 * n) >> 5 ;  eta1 = eta1 * eta1
eta2 = (18 * n) >> 5 ;  eta2 = eta2 * eta2
```

Build two Gaussian ramps on `i = 0..top` (with `e1`, `e2` from `exp`):

```
g1[i] = int( 0.45 + 256 * exp(-(i*i) / (2*eta1)) )
g2[i] = int( 0.45 + 256 * exp(-(i*i) / (2*eta2)) )
```

If a computed `eta` is 0, every weight of that ramp is 0, **including** `i = 0`
(the degenerate `exp` of a zero numerator over a zero width is treated as 0, not
1). For the smallest tile counts (`n = 1`, and `eta1` at `n = 2`) this makes the
corresponding ramp — and, when both ramps are zero, the whole table — collapse to
zero rather than to the untruncated Gaussian peak.

Blend them by the index-squared ramp `s[i] = (i*i) >> 8`, then force the result
monotone non-increasing:

```
t[0] = blend(0)
t[i] = min( blend(i), t[i-1] )
blend(i) = (g1[i] * (256 - s[i]) + g2[i] * s[i]) >> 8
```

Smooth `t` into `u` (`u[0] = t[0]`, `u[i] = (t[i-1]+t[i]+t[i+1])/3` for interior,
`u[top] = (t[top-1]+t[top]) >> 1`), then emit triples normalised to 256:

```
for i = 0..count-1:                        # count defined below
    a = |i + n|
    b = |i - n|
    sum = u[a] + u[i] + u[b]              # if sum == 0 this divides by zero
    r0  = sat8( (u[a] << 8) / sum )
    r1  = sat8( (u[i] << 8) / sum )
    r2  = sat8( 0x100 - r0 - r1 )
```

`n` is the tile count in the axis. The horizontal kernel uses
`n = block_width + 1`; the vertical kernel uses `n = block_height + 1`.

**Emitted count.** The geometry path emits `(block_width >> 1) + 2` entries from a
span of `block_width + 1` (and `(block_height >> 1) + 2` from `block_height + 1`);
a direct call with span `n` emits `(n >> 1) + 2`. These agree only for odd spans,
so the entry count is derived from the block length `span - 1`, not from the span
itself. Equivalently, for any span `s`, the geometry count is
`((s - 1) >> 1) + 2`: it is a function of the span alone, not of any separately
stored block dimension.

**Packing.** Each triple is packed little-endian into one `u16` as
`r0 | (r1 << 8)`; `r2` is implicit (`0x100 - r0 - r1`). The horizontal kernel is
written to `tbl[0..]` and the vertical kernel to `tbl[0x80..]` (each covering 128
`u16` values). Only the first `count` entries of each half are written; the
remainder of each 128-entry half is zero, so only `r0`/`r1` of each triple are
stored.

Semantics: for a tile at offset `i` from an edge, the pair gives the blend
weight of two neighbouring taps; the third weight is the remainder of the unit
sum. This is the per-tile mixing used when the locally tone-mapped detail is
merged back with the original image.

### 8.2 Tone floor table (`tbl[0x100..0x1FF]`)

Generated from the source curve section `[0x100..]` and the tone-floor increment
`clip` (pre-adjust). Two behaviours:

**Sparse (`wdr_mode == 1`):**

```
off     = bit_offset
tbl_max = (0x10000 >> off) - 1
step    = 2^off
acc     = clip
tbl[0x100] = 0
tbl[0x200] = tbl_max
k = 1
while k < (0x100 >> off) - 1:
    v   = source_curve[0x100 + k*step] >> off
    ref = max( acc >> 4, v )
    acc += clip
    tbl[0x100 + k] = min( ref, tbl_max )
    idx = k * step
    k += 1
if off != 0:
    for j = (0x100 >> off) .. 0xFE:
        tbl[0x100 + j] = tbl_max
        tbl[0x200 + j] = 0x400
```

The sampled entries are spaced by `2^off`. The fill (tail) begins at level
`(0x100 >> off)`, **not** at the last sampled source index `idx`, and runs to
`0xFE`; entries between the last sampled level (`(0x100 >> off) - 2`) and the
tail start remain zero, and index 255 of both tables is never written in sparse
mode. Because the tail starts at `0x100 >> off`, it is skipped entirely for
`off == 0` (where the whole level axis is sampled). When the loop does not run at
all (`0x100 >> off <= 2`, i.e. `off >= 7`) the tail still starts at `0x100 >> off`
(which reaches index 0 only for `off >= 9`) and may overwrite the forced value
there.

**Dense (`wdr_mode != 1`):**

```
acc = clip
tbl[0x100] = 0
for k = 1..254:
    v = source_curve[0x100 + k]
    tbl[0x100 + k] = max( v, acc >> 4 )
    acc += clip
tbl[0x1FF] = 0xFFFF
```

**Semantics.** The tone floor table is a monotonically non-decreasing lower
bound on the tone curve. `clip/16` is the per-level increment of the imposed
floor; a larger increment raises the floor faster, flattening the dark end. In
sparse mode the floor saturates at the level maximum implied by `bit_offset`;
in dense mode the last entry is pinned to `0xFFFF`.

### 8.3 Gain table (`tbl[0x200..0x2FF]`)

The gain table blends the source curve section `[0x200..]` with Q10 unity
(`0x400`), weighted by `gain` (ffunc): `gain == 0` keeps the source; `gain ==
0x1000` replaces it with unity.

**Sparse (`wdr_mode == 1`):** for each sampled `k` and its sparse source index
`idx = k * 2^off`, the source is read **without** the `bit_offset` shift,

```
tbl[0x200] = tbl_max
for k = 1 .. (0x100 >> off) - 2:
    idx = k * 2^off
    tbl[0x200 + k] = ( (0x1000 - gain) * source_curve[0x200 + idx] + gain * 0x400 ) >> 12
for j = (0x100 >> off) .. 0xFE:
    tbl[0x200 + j] = 0x400
```

and the tail begins at `(0x100 >> off)`, not at the last `idx`. With `off == 0`
the tail is skipped entirely, so index 254 keeps its blended value instead of
being overwritten with `0x400`.

**Dense (`wdr_mode != 1`):**

```
tbl[0x200] = 0xFFFF
for k = 1..254:
    tbl[0x200 + k] = ( source_curve[0x200 + k] * (0x1000 - gain) + gain * 0x400 ) >> 12
tbl[0x2FF] = 0x400
```

### 8.4 Strength curve (`strength_curve`)

See 7.3. This is the only consumer of `strength_bank`, and it is rebuilt only on
the fully automatic path, on every automatic qualifying frame (the bank itself
is read-through).

### 8.5 How the four parameters map to tables

The preset's four varying parameters are `oripic_ratio`, `order`, `clip` and
`gain`. They map to outputs as follows:

| Parameter | Consumer |
| --- | --- |
| `oripic_ratio` | Output scalar only (original/detail mix ratio). |
| `order` | Output scalar only (transform ordering). |
| `clip` | Tone floor table (`tbl[0x100..0x1FF]`) generation. |
| `gain` | Gain table (`tbl[0x200..0x2FF]`) generation. |

`last_order_ratio` is derived from the strength fraction, not from the preset
columns (the table stores 15 for every preset). The merge table is driven by
tile geometry, not by the four parameters.

---

## 9. Per-tile / per-pixel consumption contract

This module prepares tables; it does not evaluate pixels. The layout below is
the contract downstream code depends on.

### 9.1 Table layout

```
tbl (768 u16) = [
    0x000 .. 0x07F : merge weights, horizontal half (128 u16)
    0x080 .. 0x0FF : merge weights, vertical half   (128 u16)
    0x100 .. 0x1FF : tone floor table (256 u16)
    0x200 .. 0x2FF : gain table (256 u16)
]
```

Merge halves: `tbl[0x00 + i]` (horizontal) and `tbl[0x80 + i]` (vertical) each
pack two 8-bit weights as the low and high byte of a `u16`, with the third
weight equal to `0x100` minus their sum.

### 9.2 Per-pixel strength lookup

The local-contrast statistic of a pixel/tile is an unsigned value interpreted as
`level = x >> 4` (8-bit) with a 4-bit fraction. It indexes the working strength
curve with linear interpolation, the result is doubled and then shifted right by
`bit_offset` (an arithmetic shift), giving the per-location tone-mapping
strength. This is the per-pixel application of the injected `strength_bank`
(through `strength_curve`).

### 9.3 Clamps and shifts

- Merge weights are individually saturated to `0..0xFF`; the third is derived by
  subtraction and saturated, so a shortfall becomes 0.
- Tone floor values are clamped by the running `acc >> 4` floor and, in sparse
  mode, an upper clamp to `tbl_max`.
- Gain values are clamped to a 12-bit intermediate through the `>> 12` blend and
  the Q10 unity endpoint.
- All shift counts are well-defined: the arithmetic shift helper clamps the
  count. `bit_offset` should be small (`< 16`); larger values underflow
  `0x10000 >> off` and `0x100 >> off` (see 14).
- `stat_scale` is a 32-bit reciprocal multiplier for normalising the tile
  statistics; the module only computes and emits it.

---

## 10. Constants

| Constant | Value | Role |
| --- | --- | --- |
| start frame initial | 3 | First frame index allowed to run automatic processing. |
| start frame delay | +3 | `start_frame = frame_id + 3` on tile-count change. |
| speed frame gate | 0x50 | Frame index from which `config.speed` selects the converge row. |
| default tolerance | 5 | Used when `config.tolerance == 0`. |
| default step | 2 | Used when `config.step == 0`. |
| default interval | 1 | Used when `config.interval == 0`. |
| strength decode | `/64`, `%64` | `auto_strength` row / fraction for `strength_bank`. |
| auto threshold | 17 | `auto_strength` below this selects fixed-level mode. |
| curve bank shape | 4 x 256 | `strength_bank`. |
| converge bank shape | 32 x 128 | `converge_bank`. |
| converge row stride | 128 | `converge_bank[row*128 + delta]`. |
| delta clamp | 127 | `min(|delta|, 127)` before the table lookup. |
| stat count | 768 | Statistics entries averaged and judged. |
| stat scale | 16 / 768 | `cal_strength = (acc*16)/768`. |
| min-threshold derivation | `(cal+6)/12`, clamp 1..0x100 | Derived target when `min_threshold == 0`. |
| confirmation window | 30, clamp 2..30 | `a = 30 - ||d|-tol|`. |
| strength saturation | 0xFFF | `sat12` on the stepped strength. |
| preset index clamp | 15 | `idx = min(high, 15)`. |
| preset interpolation | 256, `>>8` | Linear blend by `low`. |
| last-order ratio | `max((low>>4)-1,0)`, else 15 | Derived from the strength fraction. |
| order bump | `t0+1`, clamp 5..13 | Applied for `high` in 3..10 when `low != 0`. |
| contrast scale | `(256 - contrast)/256` | Tone-floor increment modulation. |
| auto scale | `auto_strength / 16` | Applied when `auto_strength < 17`. |
| merge deltas | 15, 18 | Gaussian width factors (`eta1`, `eta2`). |
| merge gaussian base | 0.45 + 256*exp(...) | Truncated toward zero. |
| merge eta shift | `>>5`, then squared | `eta = (delta * n) >> 5`. |
| merge normalisation | `<<8`, `>>8`, /sum, sat8 | Triple weights to 0x100. |
| merge smoothing | /3, `>>1` | 1-2-1 then endpoint average. |
| tone floor shift | `>>4` | `acc >> 4` per-level increment. |
| tone floor increment | `clip` per level | Running accumulator step. |
| sparse level max | `(0x10000 >> off) - 1` | Tone ceiling in sparse mode. |
| gain unity | 0x400 | Q10 unity blended by `gain`. |
| gain blend shift | `>>12` | `(src*(0x1000-gain) + gain*0x400) >> 12`. |
| gain endpoint | 0xFFFF / 0x400 | Dense first / last entries. |
| statistic reciprocal | `2^32 / (nw*nh)` | `stat_scale` (mod 2^32). |

---

## 11. Edge cases and failure behaviour

- **Statistics beyond 12 bits.** The contrast contribution only special-cases `x >> 4 == 0xFF`.
  An entry `> 0xFFF` indexes `strength_curve` out of range; the statistics are
  assumed to be 12-bit.
- **`auto_strength` beyond the bank.** The curve builder uses
  `row = auto_strength / 64`; a value `>= 256` indexes past the four-row bank.
  Only the fully automatic path reaches this.
- **Presets 17 and 18 unreachable.** Strength targets saturate at 12 bits, so
  `high <= 15`, `idx <= 15`, and only presets 0..16 are applied. The bump can
  raise `order` to at most 13.
- **Order bump arithmetic.** The condition uses unsigned subtraction of 11, so
  indexes below 11 (with `high > 2`) take the bump while indexes 11..15 do not.
  A naive signed reading would invert this; the unsigned reading is required.
- **Zero merge sum.** The merge-table generator (`merge_axis_emit()` in
  `isp/src/pltm/pltm_clean.c`) divides by `u[a]+u[i]+u[b]`; for large
  tile counts all three Gaussians can truncate to 0, giving a divide-by-zero.
  Normal sensor geometries keep the gaussian tails non-zero.
- **Degenerate tile geometry.** `nw = width/(block_cols+1)` or
  `nh = height/(block_rows+1)` can be 0 when the picture is smaller than the
  tile grid, making `block_width`/`block_height` negative and the merge
  generator receive a non-positive tile count.
- **`stat_scale` overflow.** When `nw*nh == 1`, `2^32 / 1` truncated to 32 bits
  is 0.
- **Large `bit_offset`.** `0x10000 >> off` becomes 0 for `off >= 16`, so
  `tbl_max = -1` truncates to `0xFFFF`; `0x100 >> off` becomes 0, so the sparse
  loop does not run and the tail (starting at `0x100 >> off == 0`) overwrites
  index 0.
- **`speed` row overflow.** The speed index is set from `config.speed` and is not clamped to 31;
  a value `>= 32` reads past `converge_bank`.
- **Seed strength units.** On the frame before `start`, `next_strength =
  manual_strength` is stored raw, whereas the semi-manual target uses
  `manual_strength << 4`. The two uses disagree by a factor of 16.
- **Frozen strength during exposure.** With `ae_settled == 0` the dispatcher
  returns before `old_strength = next_strength`, so the carried strength does
  not advance even though `min_change_cnt` is reset.
- **Latched flags.** `cal_en`/`frame_smooth_en` are not cleared on
  non-qualifying frames past `start`; downstream code must treat them as
  "last decision", not "this frame".
- **Disabled path.** With `enable == 0`, `next_strength` is forced to 0 and the
  tables/scalars are not regenerated.
- **Window mode.** With `wdr_mode == 1`, the gain blend is forced to 0 before
  the contrast modulation, so the gain table keeps the source curve.
- **`contrast == 256`.** Zeroes the tone-floor increment, so the tone table
  becomes just the source curve (with the running floor removed).
- **`min_threshold` non-zero.** Overrides the derived target with
  `config.min_threshold << 4`; no clamping is applied to that shift.
- **Null pointers.** The run entry point returns `-1` without touching state.
  A null statistics pointer is not silently accepted.

---

## 12. Open questions / ambiguities

1. **`next_strength` seed units.** The pre-start seed stores
   `manual_strength` directly while the semi-manual target uses
   `manual_strength << 4`. Which scale the seed is meant to be on, and whether
   this is intentional, is unresolved.
2. **Unreachable presets 17/18.** The preset table defines 19 entries, but the
   strength path can only select 0..16. Whether presets 17/18 are reachable
   through another caller (e.g. a direct parameter write) cannot be determined
   from this object.
3. **Speed-row clamp.** `config.speed` is used unclamped as a row index into a
   32-row bank after frame `0x50`. Whether the configuration is required to
   stay below 32 is unknown.
4. **`stat_scale` meaning.** It is emitted as `2^32 / (nw*nh)` mod 2^32. The
   precise downstream normalisation it drives (and the intended handling of the
   `nw*nh == 1` overflow) is not documented.
5. **Tone/gain table axes.** The two tables are indexed by a level axis and
   share the source curve sections; whether the index is a raw luma level or a
   gamma-mapped level is inferred, not stated.
6. **Merge table third weight.** Only `r0` and `r1` are stored; the third weight
   is reconstructed as `0x100 - r0 - r1`. The exact tap positions the two
   stored weights apply to is inferred from the symmetry, not confirmed.
7. **`strength_bank` row 0 special case.** Row 0 is squared rather than used
   directly. Why the first curve needs this additional compression (and whether
   the squared form is meant to be row "−1") is not derivable from the object.
8. **Order-bump intent.** The bump that raises `order` by one for intermediate
   strengths (preset index 3..10) has no stated rationale; the unsigned
   comparison that makes it apply below index 11 may be accidental.
9. **Confirmation-gate asymmetry.** Suppressing an intended increase while
   letting an intended decrease through is a consequence of routing a zeroed
   `d` into the deadband. Whether this asymmetry is intended is unknown.
10. **Statistics domain.** `lst` is documented as per-tile local-contrast data;
    the exact statistic (edge energy, dynamic range, variance) is not stated,
    and the 16/768 averaging scale assumes 12-bit entries.
11. **Manual/static `old_strength`.** Manual/static runs never update
    `next_strength`, yet the dispatcher still copies it to `old_strength`. The
    intended meaning of the carried strength in that mode is unclear.

---

## 13. WDR stability findings

The adversarial SDK-boundary differential (part of the private differential
harness) drives
the clean core and the deployed `isp_pltm.o` through the shim with inputs at and
past the section-11 edges, each scenario in a forked child so a fault is caught
and attributed. Findings (2026-09-16):

- **Clean-only crash: `auto_strength` past the strength bank.** With
  `auto_strength >= 0x100` the curve builder's `row = value / 64` selects bank
  row 4 of 4; at `>= 0x400` the clean core faults (SIGSEGV) on the out-of-range
  read while the vendor object merely reads its adjacent tables. The clean core
  now clamps the selector to `0 .. PLTM_STRENGTH_ROWS*64 - 1` before it decodes
  the row and fraction (`src/pltm/pltm_clean.c`). In-domain behaviour is
  unchanged.
- **Statistics above 12 bits.** `lst > 0xFFF` indexes the working curve past its
  end; the contrast contribution now treats every `x >= 0xFF0` as the top-of-curve case.
- **`speed` past the converge bank.** A `speed >= 32` row is clamped to the last
  bank row.
- **Degenerate tile divisor.** A zero `block_cols + 1` or `block_rows + 1` is
  guarded in the geometry update.

Every in-domain input (all documented bounds respected) stays bit-identical to
the vendor object: the original 100-case shim differential is unchanged, and the
adversarial run reports zero in-domain mismatches and zero clean faults. The
divergences that remain are inputs on which the vendor object itself reads out
of bounds (for example `lst >= 0x8000`, `speed >= 0x100`, or tile counts that
overflow its fixed merge scratch) and faults or returns layout-dependent data;
there the clean core is defined and safe, and equality is not required.

---

## 14. Real-input capture/replay

To compare the deployed `isp_pltm.o` and the clean core on real on-camera data
instead of synthesised scenarios, the PLTM shim can capture its per-frame SDK
inputs and the host differential can replay them.

### Capture (`shim/pltm/pltm_shim.c`)

Inert unless `FREEISP_PLTM_DUMP` is set; the value is a path (`1` selects
`/tmp/freeisp_pltm_dump.bin`). The file is opened once, lazily, on the first
`run`, and one fixed-size record is appended per `run`. The record is exactly
the SDK inputs the shim's `map_config`/`map_stats` consume:

| Field | Offset | Bytes |
| --- | --- | --- |
| `pltm_param_t` | 0 | 260 |
| `struct isp_pltm_stats_s` | 260 | 1548 |
| `HW_U16 pltm_table[0x300]` (source curve) | 1808 | 1536 |
| **total** | | **3344** |

`pltm_param_t` embeds its `pltm_cfg`/`pltm_dynamic_cfg` arrays inline, so the
struct copy captures them; its one pointed buffer, the source curve
`pltm_table`, is captured inline because a pointer cannot cross the capture
boundary. A NULL `pltm_table` is recorded as a zeroed buffer.

On-camera capture (one session, on a build carrying this shim):

    FREEISP_PLTM_DUMP=/tmp/freeisp_pltm_dump.bin <run mediad>
    # then copy /tmp/freeisp_pltm_dump.bin off the camera

### Replay (private differential harness)

`run_replay(dump, bundle)` installs the real located tables from the optional
`freeisp_tables.bin` bundle, then for each record re-points each side's source
curve at its own buffer (seeded from the captured bytes) and drives the vendor
object and the clean-through-shim entity through the framework's per-frame
sequence (`isp_pltm_set_params` then `isp_pltm_run`). It compares, per frame,
the whole `pltm_result_t`, the framework write-back into `module_cfg.pltm_cfg`
(as `__isp_pltm_run` performs it), and the stored `pltm_param_t` mirror
(pointers excluded), and prints:

    == pltm replay regions: result=.. pltm_cfg=.. param=.. ==
    == pltm replay summary: N frames, M with mismatches ==

Host replay (deployed vendor object vs clean-through-shim, same records) runs
the harness's `diff_pltm_shim.bin` under `qemu-arm` with the dump and the
`freeisp_tables.bin` bundle as arguments.

The carried `pltm_result` old/next strength feedback is not part of the record;
the replay seeds both sides' result at zero and lets each entity carry its own
state from there, so a capture should begin at the start of the stream (or the
comparison is still vendor-vs-clean fair, both starting from the same state).

The no-argument run self-tests the replay path on a small synthetic capture:
the clean run must report zero mismatches and a deliberate clean-side input
perturbation (`pltm_dynamic_cfg[AE_COMP] += 1`) must be detected
(`pltm_shim replay self-test: clean=0 perturbed=1`).
