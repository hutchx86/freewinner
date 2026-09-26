<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# ISO Gain/Luminance Switchboard — Behaviour Specification

This document specifies observable behaviour only. It contains no source, no
vendor identifiers, and no reference string literals. Names in `monospace` are
neutral labels defined here, not ABI names; the exact field names/offsets come
from the compatibility layout header and `interfaces.md`.

---

## 1. Purpose and scope

The ISO module is the gain/luminance-driven parameter switchboard of the ISP. A
per-frame exposure controller already knows two things about the current scene:

- the sensor's current **gain** (how much analogue/digital gain the exposure
  loop has applied), and
- the current **AE table position** (roughly, how bright the scene is, i.e. the
  exposure level the loop has chosen).

The module maps those two quantities to a pair of curve indices, selects (or
interpolates) a whole record of image-quality settings for each, and then pushes
the selected settings into the individual hardware blocks. In effect it is a
lookup: *at this gain and this scene brightness, use these denoise, sharpening,
defect-correction, tone, colour, black-level, etc. parameters.*

The module:

- converts the current sensor gain into a monotonically-searched index into a
  350-entry gain table, and the current AE table position into a luminance index;
- precomputes two arrays of 350 dynamic records, one indexed by gain and one by
  luminance, by interpolating a set of 14 configured dynamic records against a
  set of 14 breakpoints;
- per frame, picks, for each individual IQ block independently, either the
  gain-indexed or luminance-indexed record (the choice is configured per block);
- applies that block's own formula/clamp to the selected record's values and
  writes the result into the shared module configuration;
- additionally runs a temporal (3D) denoise configuration pass and a
  colour-table (CEM) generation pass, including a colour-to-gray fade.

It does **not** perform exposure control, AWB, lens shading, or tone-mapping
curve generation; it only distributes per-ISO parameters. All tuning data
(breakpoints, dynamic records, curves) is supplied at runtime; nothing is
compiled in.

---

## 2. Conventions and symbols

- Arithmetic is 32-bit two's-complement integer unless stated. All shifts are
  arithmetic. Signed division truncates toward zero. Unsigned fields read into
  signed expressions are reinterpreted as their two's-complement value.
- `interp(c; x0,x1; y0,y1)`:
  ```
  if x1 == x0:  result = y0
  else:         result = y0 + (y1 - y0) * (c - x0) / (x1 - x0)
  ```
  Computed in signed 32-bit with truncating division. `x0` may be greater than
  `x1` (used for descending ramps).
- `clrneg(v)` = `v < 0 ? 0 : v`.
- `sat12(v)` = `min(clrneg(v), 0x0fff)` — the ubiquitous "clear sign bit, then
  cap at 4095" clamp.
- `rshift8(t)` = `(t < 0 ? t + 0xff : t) >> 8` — arithmetic right shift after
  adding a rounding bias for negatives. Used only where stated.
- `usat(v, b)` = `v < 0 ? 0 : (v > 2^b - 1 ? 2^b - 1 : v)`.
- A **dynamic record** is a fixed block of 148 32-bit words (see §5.1). The
  words carry signed or unsigned quantities depending on the block; when a
  whole record is copied or interpolated it is treated as 148 signed words.
- `TRIG_LUM` / `TRIG_GAIN`: the per-block trigger selector. Value `0` selects
  the luminance-indexed record; any non-zero value selects the gain-indexed
  record.

---

## 3. Input contract

### 3.1 Parameter block (per instance)

| Neutral name | Meaning | Type | Range/notes |
| --- | --- | --- | --- |
| `param_kind` | Operation selector passed to set-parameters | enum | `0` = rebuild interpolation arrays; other = error |
| `frame_id` | Monotonic frame counter | signed int | used for temporal-denoise warm-up |
| `chroma_gray_th` | Luminance index at/above which the WDR colour-to-gray fade may run | unsigned 16 | compared against the luminance index |
| `chroma_gray_span_min` | Inner colour-to-gray half-window, degrees | unsigned 16 | |
| `chroma_gray_span_max` | Outer colour-to-gray half-window, degrees | unsigned 16 | effective only when `>= span_min` |
| `cnr_on`, `sharp_on`, `sat_on`, `contrast_on`, `brightness_on`, `cem_on`, `denoise_on`, `sensor_offset_on`, `black_level_on`, `dpc_on`, `defog_on`, `pltm_dyn_on`, `tdnr_on`, `ae_cfg_on`, `gtm_cfg_on`, `lca_cfg_on`, `af_cfg_on` | Per-block enable flags | unsigned 32 | non-zero = run that block's update |
| `dn_core_ratio[4]` | 2D denoise per-layer core ratios | unsigned 32 | written as bytes to the denoise block |
| `gen` | Pointer to the shared ISP context | pointer | all tuning/3A state lives behind it |

### 3.2 Shared context inputs (read)

| Neutral name | Meaning | Range/notes |
| --- | --- | --- |
| `total_gain` | Current sensor total gain | scaled by `*100 >> 8` before table search |
| `ae_pos` | Current AE table position | clamped to `ae_pos_max` |
| `ae_pos_max` | AE table maximum position | `0` yields luminance index `0` |
| test enables: `tdf_enable`, `contrast_enable`, `wb_enable`, `sat_enable`, `pltm_enable` | Feature gates | boolean |
| tuning levels: `contrast_level`, `sat_level`, `sharp_level`, `brightness_level`, `denoise_level`, `pltm_level`, `tdf_level`, `highlight_level`, `backlight_level` | User/tuning strength knobs | signed |
| per-block triggers | 17 enum selectors (`sharp`, `contrast`, `denoise`, `sensor_offset`, `black_level`, `dpc`, `defog`, `pltm_dynamic`, `brightness`, `saturation`, `cem_ratio`, `tdf`, `color_denoise`, `ae_cfg`, `gtm_cfg`, `lca_cfg`; a `gcontrast` trigger exists but is unused) | `0` = luminance, else gain |
| tuning curves | `k3d_incre_curve[256]`, `tdnf_diff[256]`, `sharp_edge_lum[33]`, `sharp_hfrq_lum[33]`, `sharp_hsv[46]`, `sharp_s_map[33]`, `sharp_val[66]`, `bdnf_th[33]`, `tdnf_th[66]`, `tdnf_k[32]`, `cem_table_a[5888]`, `cem_table_b[5888]` | raw tuning data (exact byte/element sizes per the compatibility header) |
| `dynamic_enable` | Dynamic-judge enable (also written here, see §6.4) | boolean |
| `*_comp_target` values | Dynamic-judge compensation targets: `tdnf_comp`, `tdnf_diff_comp`, `lp_th_ratio_comp`, `sharp_hfrq_comp`, `sharp_edge_comp`, `sharp_under_shoot_comp` | signed |
| `pltm_strength` | PLTM current strength (Q12) | 0..4095 |
| WB gains `r_gain`, `gb_gain`, `b_gain` | Applied white-balance gains | Q12 |
| `color_matrix` (3x3 + offset) | RGB->RGB matrix | |
| `gamma_table[3072]` | Gamma LUT, three 1024-entry planes | |
| `rgb2yuv` (3x3 + offset) | RGB->YUV matrix | |
| `ae_mode` | `0` = normal, `1` = WDR | gates colour-to-gray |
| default breakpoints `gain_point_default[14]`, `lum_point_default[14]` | fallbacks when configured breakpoints are invalid | injected tables |

### 3.3 Injected tables

Obtained through the runtime table-injection API (`interfaces.md`); never
compiled in. The module reads exactly four:

| Neutral name | Shape | Role |
| --- | --- | --- |
| `gain_index_table` | unsigned int[350] | monotone non-decreasing; maps a scaled gain value to a 0..349 index |
| `gain_point_default` | signed int[14] | default gain breakpoints (monotone) |
| `lum_point_default` | signed int[14] | default luminance breakpoints (monotone) |
| `af_square_table` | unsigned char[16] | fixed 16-byte seed for the AF square LUT |

---

## 4. Output contract

On a successful run the module writes, into the shared context, the fields
listed in §6, and returns `0`. The per-frame result block receives:

| Neutral name | Meaning | Range |
| --- | --- | --- |
| `lum_i` | Computed luminance curve index | 0..349 |
| `gain_i` | Computed gain curve index | 0..349 |

The parameter query returns a pointer to the instance's parameter block (or `-1`
if the instance is null). The set-parameters entry point returns `0` when it
rebuilds the arrays and `-1` otherwise. The per-frame entry point returns `-1`
only if the instance or result pointer is null.

---

## 5. Internal state and lifecycle

### 5.1 Dynamic record layout (148 words, offsets 0..147)

| Offset | Neutral name | Count |
| --- | --- | --- |
| 0..28 | `sharp_cfg` | 29 |
| 29..39 | `contrast_cfg` | 11 |
| 40..59 | `denoise_cfg` | 20 |
| 60..63 | `sensor_offset` | 4 |
| 64..67 | `black_level` | 4 |
| 68..73 | `dpc_cfg` | 6 |
| 74..77 | `pltm_dynamic_cfg` | 4 |
| 78 | `defog_value` | 1 |
| 79 | `brightness` | 1 |
| 80 | `contrast` | 1 |
| 81 | `sat_cb` | 1 |
| 82 | `sat_cr` | 1 |
| 83..89 | `sat_cfg` | 7 |
| 90 | `cem_ratio` | 1 |
| 91..112 | `tdf_cfg` | 22 |
| 113 | `color_denoise` | 1 |
| 114..127 | `ae_cfg` | 14 |
| 128..136 | `gtm_cfg` | 9 |
| 137..147 | `lca_cfg` | 11 |

(The layout matters only so the reader knows which word each block formula
below refers to; an implementation may define any equivalent accessor.)

### 5.2 Instance state

| Neutral name | Shape | Role |
| --- | --- | --- |
| `lum_i` | int | last/current luminance index |
| `gain_i` | int | last/current gain index |
| `param` | parameter block | instance configuration |
| `by_gain` | dynamic record[350] | gain-indexed interpolation output |
| `by_lum` | dynamic record[350] | luminance-indexed interpolation output |
| `gain_axis` | int[14] | breakpoints converted to gain-table indices |
| `lum_axis` | int[14] | breakpoints used directly |
| `busy_flag` | int | set to 1 at allocation; otherwise unused |

### 5.3 Module-global state

A single process-global signed counter, `tdnf_start_frame`, is shared by all
instances and initialised to **`1`** at load — the deployed object initialises it
to `1` (not `0`) and its `init` does not reset it. It records the frame
at which the temporal-denoise block last (re)started accumulation (see §6.13).

### 5.4 Lifecycle

- **Allocation**: allocate and zero the instance; `busy_flag = 1`.
- **Initialisation**: `lum_i = gain_i = 0x20` (32).
- **`init`**: as above, plus return an ops vtable of {set-params, get-params,
  run}.
- **set-parameters** with `param_kind == 0`: rebuild `by_gain` then `by_lum`
  from the current tuning data. With any other kind: return `-1`, change
  nothing.
- **Per frame (`run`)**: compute indices, then run each enabled block in the
  order of §6. Return `0`.
- **Teardown**: free the instance.

---

## 6. Algorithm

Throughout, `dyn` denotes the selected dynamic record for a block: either
`by_gain[gain_i]` or `by_lum[lum_i]` per that block's trigger. The subsections
below are grouped by block, **not** in execution order; the exact per-frame
order is listed in §8.

### 6.1 Index computation (runs unconditionally, first, every frame)

```
max = ae_pos_max
p   = ae_pos
if max < p: p = max
lum_i = interp(p; 0,max; 0, 0x15d)          # map [0,max] -> [0,349]
if max == 0: lum_i = 0

g = (total_gain * 100) >> 8
if g > 0xc7fff: g = 0xc8000                 # note: strict > then snap
gain_i = 0x15d                              # default if no match
for k = 0x18; k != 0x15d; k++:
    if g <= gain_index_table[k]: gain_i = k; break
```
- The gain-table search starts at index `0x18` (24) and stops before `0x15d`
  (349). If no entry matches, `gain_i` remains `0x15d` (349), which is a valid
  record index (`by_gain` has 350 entries).
- `lum_i` is stored in `by_lum[lum_i]` (`0..349`).

Both are copied to the result block.

### 6.2 Array construction (`param_kind == 0`)

Both arrays are built from the same 14 configured dynamic records
`cfg[0..13]`.

#### 6.2.1 Axis conversion

Let `gp[0..13]` be the configured gain breakpoints and `lp[0..13]` the
configured luminance breakpoints.

Validation: if any breakpoint is `< 1`, replace **all 14** gain breakpoints
with `gain_point_default` and all 14 luminance breakpoints with
`lum_point_default`. (The configured arrays are overwritten.)

Gain axis (value -> gain-table index):
```
for i in 0..13:
    gain_axis[i] = 0x15d
    for k = 0x18; k != 0x15d; k++:
        if gp[i] <= gain_index_table[k]: gain_axis[i] = k; break
```
Luminance axis is a plain copy: `lum_axis[i] = lp[i]`.

#### 6.2.2 Gain-indexed array

```
j = 0
for x = 0..349:
    if gain_axis[j] <= x: j++
    if j == 0:
        lo = hi = gain_axis[0];  prev = next = cfg[0]
    else:
        lo = gain_axis[j-1]; hi = gain_axis[j]
        prev = cfg[j-1];      next = cfg[j]
    for w = 0..147:
        by_gain[x].word[w] = interp(x; lo,hi; prev.word[w], next.word[w])
```
Note the increment is by one per `x` and is **not** clamped; when
`gain_axis[13] <= x` the index reaches 14 and the references
`gain_axis[14]` / `cfg[14]` run past the 14-entry arrays. This is inherited
behaviour, not a transcription error (see edge cases and open questions).

#### 6.2.3 Luminance-indexed array

```
j = 0
for x = 0..349:
    if x < lum_axis[j]:
        if j == 0:
            lo = hi = lum_axis[0];  prev = next = cfg[0]
        else:
            goto segment
    else:
        if j < 0xd: j++
      segment:
        lo = lum_axis[j-1]; hi = lum_axis[j]
        prev = cfg[j-1];    next = cfg[j]
    for w = 0..147:
        by_lum[x].word[w] = interp(x; lo,hi; prev.word[w], next.word[w])
```
Here the index is capped at `0xd` (13), so the luminance path never runs past
the arrays.

### 6.3 CNR / colour-denoise block (`cnr_on`)

`dyn` selected by the colour-denoise trigger.

```
c  = dyn.color_denoise                  # signed word
m  = clrneg(c)
if m > 0xffe: m = 0xfff
out.c_threshold = (u16)m

a = m
if a < 0x40: a = 0x40
a = (c * a) >> 8                         # signed
cap = m < 0x3ff ? m : 0x3ff
if a < 0x20: a = 0x20
if cap < a:  a = cap
out.y_threshold = (u16)a

t = c * 0xc0
if t < 0: t += 0x1ff
v = 0x100 - (t >> 9)
if v < 0x40: v = 0x40
if v > 0xffe: v = 0xfff
out.st_v_yth = (u16)v

t = (s32)(c << 2)                        # reinterpret shifted pattern as signed
if t < 0: t = 4*c + 0x1ff
v = 0x10 - (t >> 9)
if v < 0xc: v = 0xc
if v > 0xffe: v = 0xfff
out.st_h_yth = (u16)v
```
(The `4*c + 0x1ff` fallback is taken when the 32-bit pattern `c << 2` is
negative; `>> 9` is arithmetic.)

### 6.4 Contrast block (`contrast_on`)

`dyn` selected by the contrast trigger.

```
v = dyn.contrast + contrast_level
if v < -0x80: v = -0x80
if v >  0x7f: v = 0x80
shared.adjust.contrast = v

shared.dynamic_enable = (contrast_enable != 0)     # side effect, see note

if shared.dynamic_enable:
    c0..c9 = dyn.contrast_cfg[0..9]
    comp.tdnf_comp[0]             = c0
    comp.lp_th_ratio_comp[0]      = c1
    comp.sharp_hfrq_comp[0]       = c2
    comp.sharp_edge_comp[0]       = c3
    comp.sharp_under_shoot_comp[0]= c4
    comp.tdnf_comp[3]             = c5
    comp.lp_th_ratio_comp[3]      = c6
    comp.sharp_hfrq_comp[3]       = c7
    comp.sharp_edge_comp[3]       = c8
    comp.sharp_under_shoot_comp[3]= c9
    comp.tdnf_comp[1]             = c0/2       # truncating
    comp.lp_th_ratio_comp[1]      = c1/2
    comp.sharp_hfrq_comp[1]       = c2/2
    comp.sharp_edge_comp[1]       = c3/2
    comp.sharp_under_shoot_comp[1]= c4/2
    comp.tdnf_comp[2]             = c5/2
    comp.lp_th_ratio_comp[2]      = c6/2
    comp.sharp_hfrq_comp[2]       = c7/2
    comp.sharp_edge_comp[2]       = c8/2
    comp.sharp_under_shoot_comp[2]= c9/2
```
Note the contrast block **sets the shared dynamic-judge enable** whenever
`contrast_on` is set, regardless of the block's own values. Sharpening, 2D
denoise and 3D denoise read this flag later in the same frame; if
`contrast_on` is clear the flag keeps its previous value.

### 6.5 Brightness block (`brightness_on`)

`dyn` selected by the brightness trigger.

```
v = dyn.brightness + brightness_level
if v < -0x80: v = -0x80
if v >  0x7f: v = 0x80
shared.adjust.brightness = v
```

### 6.6 Saturation block (`sat_on`)

`dyn` selected by the saturation trigger.

```
out.satu_r = (s16)dyn.sat_cfg[0]
out.satu_g = (s16)dyn.sat_cfg[1]
out.satu_b = (s16)dyn.sat_cfg[2]
out.saturation_mode = dyn.sat_cfg[3]

cb  = dyn.sat_cfg[4]
cr  = dyn.sat_cfg[5]
den = dyn.sat_cfg[6]                     # curve shoulder; must be > 0
if pltm_enable:
    cb  += (dyn.sat_cb * pltm_strength) >> 12
    cr  += (dyn.sat_cr * pltm_strength) >> 12
base = sat_level
hi   = base + cb
lo   = base + cr
clamp hi to [-0x100, 0x200]
clamp lo to [-0x100, 0x200]

for k = -den .. (0xff - den):
    d = pow(2.72, (k * -10.0) / den)
    table[k + den] = (s16)( lo + (1/(d+1)) * (hi - lo) )

shared.table_update |= 0x20
```
`pow` uses the natural-constant base `2.72`, real arithmetic, then the result is
truncated to signed 32 before the 16-bit store. `den == 0` is a divide-by-zero
fault.

### 6.7 CEM chroma table and colour-to-gray (`cem_on`)

#### 6.7.1 Table blend

`dyn` selected by the CEM-ratio trigger.

```
r = dyn.cem_ratio                        # signed
for i in 0..0x16ff:                      # 5888 entries
    out_cem[i] = ((0x100 - r) * cem_table_b[i] + r * cem_table_a[i]) >> 8
```
Here `cem_table_a` is the tuning table weighted by the CEM ratio and
`cem_table_b` the one weighted by its complement. The output table is 6656
bytes; only the first 5888 are written by this blend. The remaining bytes are
used (and partly overwritten) by the per-cell pass below.

#### 6.7.2 Colour-to-gray window setup

Colour-to-gray runs only when **all** of:

- `chroma_gray_span_max >= chroma_gray_span_min`,
- `ae_mode == WDR`,
- `lum_i >= chroma_gray_th`,
- `wb_enable != 0`.

If it does not run, use the neutral fallback values
`span_min = 0x14`, `span_max = 0x1e`, and `inner_lo = inner_hi = outer_lo =
outer_hi = 0` (see the definitions below). The same fallback is used when
`wb_enable == 0`.

When it runs:

```
gb = (wb_gb_gain * 0xfff) >> 12
b  = (wb_b_gain  * 0xfff) >> 12
r  = (wb_r_gain  * 0xfff) >> 12

yy  = color_matrix.offset[0] + ((b*cm[0][2] + cm[0][0]*r + gb*cm[0][1]) >> 8)
cb_ = color_matrix.offset[1] + ((yy*cm[1][0] + gb*cm[1][1] + b*cm[1][2]) >> 8)
cr_ = color_matrix.offset[2] + ((cb_*cm[2][1] + yy*cm[2][0] + b*cm[2][2]) >> 8)
gY  = gamma_table[yy >> 2] >> 4
gCb = gamma_table[(cb_ >> 2) + 0x400] >> 4
gCr = gamma_table[(cr_ >> 2) + 0x800] >> 4
uu  = (rgb2yuv.offset[1] >> 2)
      + ((gCr*rgb2yuv.matrix[1][2] + gY*rgb2yuv.matrix[1][0]
          + gCb*rgb2yuv.matrix[1][1]) >> 10)
vv  = (rgb2yuv.offset[2] >> 2)
      + ((gCr*rgb2yuv.matrix[2][2] + gY*rgb2yuv.matrix[2][0]
          + gCb*rgb2yuv.matrix[2][1]) >> 10)
h0 = HUE(uu, vv)

span_min = chroma_gray_span_min
span_max = chroma_gray_span_max
inner_lo = h0 - span_min
inner_hi = h0 + span_min
outer_lo = h0 - span_max
outer_hi = h0 + span_max
```

`HUE(u,v)`:
```
h = atan2( -(u - 128.0), (v - 128.0) )
if h < 0: h += 6.2831853
return (s32)(h * 180.0 / 3.14159265)     # degrees, 0..359
```

#### 6.7.3 Per-cell chroma processing

Loop over the 9 x 17 x 17 chroma grid:

```
for g3 = 0..8:
  for g2 = 0..16:
    for g1 = 0..16:
        bits  = ((g3 & 1) << 2) + ((g2 & 1) * 2) + (g1 & 1)      # 0..7
        plane = ((g3 >> 1) * 0x51) + ((g2 >> 1) * 9) + (g1 >> 1)
        base  = bits > 3 ? bits + 0x65c : bits
        idx   = base + plane * 4                                 # byte-pair index

        # (a) saturation shaping, only when hardware saturation is disabled
        if sat_enable == 0:
            p0 = out_cem[2*idx]
            p1 = out_cem[2*idx + 1]
            sat = g3 < 2 ? dyn.sat_cfg[0]
                : g3 < 4 ? dyn.sat_cfg[1]
                : g3 < 7 ? dyn.sat_cfg[2]
                :          dyn.sat_cfg[3]
            apply_saturation(p0, p1, sat) -> p0', p1'
            out_cem[2*idx]     = (u8)(p0' + 0x80)
            out_cem[2*idx + 1] = (u8)(p1' + 0x80)

        # (b) colour-to-gray fade
        if (not (span_max < span_min)) and ae_mode == WDR
           and lum_i >= chroma_gray_th and wb_enable != 0:
            x = HUE(g1 << 4, g2 << 4)
            decision, y0, y1, x' = colour_gray_decision(x)
            if decision == GRAY:
                out_cem[2*idx]     = 0x80
                out_cem[2*idx + 1] = 0x80
            elif decision == BLEND:
                q0 = out_cem[2*idx]     - 0x80
                q1 = out_cem[2*idx + 1] - 0x80
                coeff = interp(x'; y0,y1; -0x100, 0)
                q0 += (s8)((coeff * q0) >> 8)
                q1 += (s8)((coeff * q1) >> 8)
                out_cem[2*idx]     = (u8)(q0 + 0x80)
                out_cem[2*idx + 1] = (u8)(q1 + 0x80)
            # decision == SKIP: leave unchanged
```

`apply_saturation(p0, p1, sat)` operates on the centred components
`v = p0 - 128`, `w = p1 - 128`, using signed 8-bit truncation `s8()`:

```
v = p0 - 128;  w = p1 - 128
a = (s8)v;     b = (s8)w

if sat < 1:
    s = sat < -0x100 ? -0x100 : sat
    a = a + (s8)((s * v) >> 8)
    b = b + (s8)((s * w) >> 8)

else if |w| < |v|:
    t = 0
    if v >= 4:      t = (interp(v; 4, 0x7f; 0x100, 0) * sat) >> 8
    elif v < -3:    t = (interp(v; -4, -0x80; 0x100, 0) * sat) >> 8
    n = v + ((t * v) >> 8)
    if n < 0x80:
        if n < -0x80:
            n = -0x80
            d = (s8)(-(w * p0) / v)      # p0 is the original byte value
        else:
            d = (s8)((t * w) >> 8)
        a = (s8)n
        b = b + d
    else:
        a = 0x7f
        b = b + (s8)((w * (0x7f - v)) / v)

else:
    t = 0
    if w >= 4:      t = (interp(w; 4, 0x7f; 0x100, 0) * sat) >> 8
    elif w < -3:    t = (interp(w; -4, -0x80; 0x100, 0) * sat) >> 8
    n = w + ((t * w) >> 8)
    if n < 0x80:
        if n < -0x80:
            b = -0x80
            d = (s8)(-(p1 * v) / w)      # p1 is the original byte value
        else:
            b = (s8)n
            d = (s8)((t * v) >> 8)
        a = a + d
    else:
        b = 0x7f
        a = a + (s8)((v * (0x7f - w)) / w)

return (a, b)
```

`colour_gray_decision(x)` returns one of `GRAY`, `BLEND`, `SKIP`, and for
`BLEND` the interpolation endpoints `y0,y1` and possibly a wrapped `x'`. Using
the shorthand `a=inner_lo`, `b=inner_hi`, `c=outer_lo`, `d=outer_hi`:

```
if a < 0:
    if (a + 360 <= x) or (x <= b):            return GRAY
    if b > 360:                                goto E
    goto P
else:
    if b < 361:
        if (b < x) or (x < a):                 goto P
    else:
      E:
        if (x < a) and (b - 360 < x):          goto P
    return GRAY

P:
    if c < 0:
        if (c + 360 <= x) or (x <= d):         goto ADJUST
        if d > 360:                            goto Q
        goto ADJUST
    else:
        if d < 361:
            if (d < x) or (x < c):             return SKIP
        else:
          Q:
            if (x < c) and (d - 360 < x):      return SKIP
    goto ADJUST

ADJUST:
    if c < 0:
        y0 = a; y1 = c
        if a > 0:
            x' = (x > a) ? x - 360 : x
        else:
            x' = x - 360
    else:
        if d > 360:
            if (x >= c) or (x <= d - 360):
                y0 = b; y1 = d
                x' = (a >= 360 or x < b) ? x + 360 : x
            else:
                y0 = a; y1 = c; x' = x
        else:
            if x >= b: y0 = b; y1 = d
            else:      y0 = a; y1 = c
            x' = x
    return BLEND(y0, y1, x')
```

Semantically: hues inside the inner `+/-span_min` window are set to neutral
gray; hues between `span_min` and `span_max` are pulled linearly toward gray
(`coeff` runs from `-256` at the inner edge, i.e. full gray, to `0` at the outer
edge, i.e. unchanged); hues outside `span_max` are untouched. The branch
structure exists only to normalise windows whose endpoints fall outside
`[0,360)`.

### 6.8 2D denoise block (`denoise_on`)

`dyn` selected by the denoise trigger.

```
g1 = dyn.denoise_cfg[1]
g3 = dyn.denoise_cfg[3]
if pltm_enable:
    g1 = (g1 * pltm_strength) >> 12
    g3 = (g3 * pltm_strength) >> 12

out.hf_ratio           = (u8)dyn.denoise_cfg[4]
out.bf_ratio           = (u8)dyn.denoise_cfg[5]
out.lf_ratio           = (u8)dyn.denoise_cfg[6]
out.lp0_np_side_ratio  = (u8)dyn.denoise_cfg[7]
out.lp1_np_side_ratio  = (u8)dyn.denoise_cfg[8]
out.lp2_np_side_ratio  = (u8)dyn.denoise_cfg[9]
out.lp0_np_core_ratio  = (u8)dn_core_ratio[0]
out.lp1_np_core_ratio  = (u8)dn_core_ratio[1]
out.lp2_np_core_ratio  = (u8)dn_core_ratio[2]
out.lp3_np_core_ratio  = (u8)dn_core_ratio[3]
out.lp0_pcnt_ratio     = (u8)dyn.denoise_cfg[0xe]
out.lp1_pcnt_ratio     = (u8)dyn.denoise_cfg[0xf]
out.lp2_pcnt_ratio     = (u8)dyn.denoise_cfg[0x10]
out.lp3_pcnt_ratio     = (u8)dyn.denoise_cfg[0x11]

r_b = dyn.denoise_cfg[0xb]
r_c = dyn.denoise_cfg[0xc]
r_d = dyn.denoise_cfg[0xd]
if shared.dynamic_enable:
    r_c += (comp.lp_th_ratio_comp_target * r_c) >> 8

for k = 0..32:
    v1 = interp(k; 0,0x20; dyn.denoise_cfg[0], dyn.denoise_cfg[2])
    v2 = interp(k; 0,0x20; g1, g3)
    t  = ((denoise_level * v1) / 0x32) * bdnf_th[k]
    if t < 0: t += 0xff
    u = clrneg(v2 + (t >> 8))
    if u > 0xffe: u = 0xfff
    out.d2d_lp0_th[k] = (u16)u
    if (signed)u < 1:                      # equivalent to "pre-clamp value <= 0"
        out.d2d_lp1_th[k] = 0
        out.d2d_lp2_th[k] = 0
        out.d2d_lp3_th[k] = 0
    else:
        p = (u * r_b) >> 8;  p = p<1 ? 1 : (p>0xffe ? 0xfff : p); out.d2d_lp1_th[k] = p
        p = (r_c * u) >> 8;  p = p<1 ? 1 : (p>0xffe ? 0xfff : p); out.d2d_lp2_th[k] = p
        p = (u * r_d) >> 8;  p = p<1 ? 1 : (p>0xffe ? 0xfff : p); out.d2d_lp3_th[k] = p
```

### 6.9 Sensor-offset block (`sensor_offset_on`)

`dyn` selected by the sensor-offset trigger.

```
s0 = (s16)dyn.sensor_offset[0]   # R
s1 = (s16)dyn.sensor_offset[1]   # Gr
s2 = (s16)dyn.sensor_offset[2]   # Gb
s3 = (s16)dyn.sensor_offset[3]   # B
shared.module.sensor_offset = {s0,s1,s2,s3}
shared.sensor.gain_offset   = {s0,s1,s2,s3}     # same values written back
```

### 6.10 Black-level block (`black_level_on`)

`dyn` selected by the black-level trigger.

```
shared.module.offset.r  = (s16)dyn.black_level[0]
shared.module.offset.gr = (s16)dyn.black_level[1]
shared.module.offset.gb = (s16)dyn.black_level[2]
shared.module.offset.b  = (s16)dyn.black_level[3]
```

### 6.11 Defect-pixel-correction block (`dpc_on`)

`dyn` selected by the DPC trigger.

```
out.hot_ratio          = (u8)dyn.dpc_cfg[0]
out.cold_ratio         = (u8)dyn.dpc_cfg[1]
out.nbhd_diff_ratio    = (u8)dyn.dpc_cfg[2]
out.nearest_diff_ratio = (u8)dyn.dpc_cfg[3]
out.slope_th           = (u16)dyn.dpc_cfg[4]
out.cold_abs_th        = (u16)dyn.dpc_cfg[5]
```

### 6.12 Defog / PLTM / AE / GTM / LCA / AF

**Defog** (`defog_on`), `dyn` selected by the defog trigger:
```
shared.adjust.defog_value = dyn.defog_value
```

**PLTM dynamic** (`pltm_dyn_on`), `dyn` selected by the PLTM-dynamic trigger:
```
shared.ae.pltm_dynamic_cfg[0] = usat(pltm_level + dyn.pltm_dynamic_cfg[0], 8)
shared.ae.pltm_dynamic_cfg[1] = usat(pltm_level + dyn.pltm_dynamic_cfg[1], 12)
shared.ae.pltm_dynamic_cfg[2] = dyn.pltm_dynamic_cfg[2]
shared.ae.pltm_dynamic_cfg[3] = dyn.pltm_dynamic_cfg[3]
```

**AE configuration** (`ae_cfg_on`), `dyn` selected by the AE-config trigger:
```
v = dyn.ae_cfg[0]
if highlight_level < 0:
    v = v * (highlight_level + 0x20)
    if v < 0: v += 0x1f
    v >>= 5
else:
    v = highlight_level * v + v
shared.ae.exposure_cfg[0] = (u32)v

v = dyn.ae_cfg[1]
if backlight_level < 0:
    v = v * (backlight_level + 0x20)
    if v < 0: v += 0x1f
    v >>= 5
else:
    v = backlight_level * v + v
shared.ae.exposure_cfg[1] = (u32)v

for k = 2..13:
    shared.ae.exposure_cfg[k] = dyn.ae_cfg[k]
```

**GTM configuration** (`gtm_cfg_on`): note the array is selected with the
**AE-config trigger**, not a GTM trigger.
```
dyn = (ae_trig == TRIG_LUM) ? by_lum[lum_i] : by_gain[gain_i]
for k = 0..8:
    shared.ae.ae_hist_eq_cfg[k] = dyn.gtm_cfg[k]
```

**LCA configuration** (`lca_cfg_on`), `dyn` selected by the LCA trigger:
```
out.lca_gf_cor_ratio   = (u16)dyn.lca_cfg[0]
out.lca_pf_cor_ratio   = (u16)dyn.lca_cfg[1]
out.lca_lum_th         = (u16)dyn.lca_cfg[2]
out.lca_grad_th        = (u16)dyn.lca_cfg[3]
out.lca_clr_gth        = (u16)dyn.lca_cfg[4]
out.lca_pf_rshf        = (u16)dyn.lca_cfg[5]
out.lca_pf_bslp        = (u16)dyn.lca_cfg[6]
out.lca_clrs_lum_th    = (u8)dyn.lca_cfg[7]
out.lca_pf_clrc_ratio  = (u8)dyn.lca_cfg[8]
out.lca_gf_clrc_ratio  = (u8)dyn.lca_cfg[9]
out.lca_pf_decr_ratio  = (u8)dyn.lca_cfg[10]
```

**AF configuration** (`af_cfg_on`): fixed defaults, plus sensor offsets and a
16-byte injected table.
```
af_r_offset = module.sensor_offset.r
af_g_offset = (s16)((module.sensor_offset.gr + module.sensor_offset.gb) >> 1)
af_b_offset = module.sensor_offset.b

af_mode = 0
af_iir0_en = af_fir0_en = af_iir0_sec0_en = af_iir0_sec1_en = af_iir0_sec2_en = 1
af_iir0_ldg_en = af_fir0_ldg_en = af_offset_en = af_peak_en = 1
af_iir_ds_en = af_fir_ds_en = af_squ_en = 0

IIR g0..g5 =  0x01e0, -233, 0x019a, -206, 0x01a5, -186
IIR s0..s3 =  injected table `af_iir_s` if supplied, else 0 (IIR feedback off)
FIR g0..g4 =  0x14, 0x10, 0x00, -16, -20
iir0_dilate = 2

(ldg_lgain, ldg_hgain, ldg_lth, ldg_hth) = (8, 8, 4, 0xc8)  for both IIR and FIR
(ldg_lslope, ldg_hslope)                  = (0x0a, 0x0e)    for both IIR and FIR
(core_th, core_peak, core_slope)          = (2, 0xff, 5)    for both IIR and FIR
af_hlt_th = 0xeb

copy first 16 bytes of af_square_table -> out.af_square_lut
```

The coefficient banks, thresholds and enable flags above are the platform's
defaults; `../docs/provenance.md` records how they were obtained.

### 6.13 Temporal (3D) denoise block (`tdnr_on`)

`dyn` selected by the **TDF trigger** (independently of the other blocks).

```
dyn = (tdf_trig == TRIG_LUM) ? by_lum[lum_i] : by_gain[gain_i]

if tdf_enable == 0:
    out.tdf.rec_en = 0
    tdnf_start_frame = frame_id + 1
else:
    enable = shared.module_enable_flag
    if dyn.tdf_cfg[4] == 0 and dyn.tdf_cfg[6] == 0:
        shared.module_enable_flag = enable & ~0x20
        tdnf_start_frame = frame_id + 1
    else:
        shared.module_enable_flag = enable | 0x20
    if (shared.module_enable_flag & 0x20) == 0 or frame_id <= tdnf_start_frame:
        out.tdf.rec_en = 0
    else:
        out.tdf.rec_en = 1

# working coefficients
p4  = dyn.tdf_cfg[4]
p5  = dyn.tdf_cfg[5]
p6  = dyn.tdf_cfg[6]
p7  = dyn.tdf_cfg[7]
p8  = dyn.tdf_cfg[8]
p9  = dyn.tdf_cfg[9]
p10 = dyn.tdf_cfg[10]
p11 = dyn.tdf_cfg[0xb]
if pltm_enable:
    s = pltm_strength
    p7  = (p7  * s) >> 12
    p5  = (p5  * s) >> 12
    p9  = (s * p9) >> 12
    p11 = (p11 * s) >> 12

copy k3d_incre_curve[256] -> out.tdnf_table[0..255]
copy tdnf_diff[256]       -> local_diff[0..255]

limit = clrneg(dyn.tdf_cfg[0x14])
if shared.dynamic_enable:
    c = comp.tdnf_comp_target
    limit += (comp.tdnf_diff_comp_target * limit) >> 8
    p4  += (p4  * c) >> 8
    p6  += (p6  * c) >> 8
    p8  += (p8  * c) >> 8
    p10 += (p10 * c) >> 8

for i in 0..255:
    if local_diff[i] > limit: local_diff[i] = limit
copy local_diff[256] -> out.tdnf_table[0x100..0x1ff]

out.tdf.noise_clip_ratio        = (u16)dyn.tdf_cfg[0]
out.tdf.lum_diff_clip_ratio     = (u8)dyn.tdf_cfg[0xc]
out.tdf.bright_diff_clip_ratio  = (u8)dyn.tdf_cfg[0xd]
out.tdf.bright_diff_ratio       = (u8)dyn.tdf_cfg[0xe]
out.tdf.mv_ori_ratio            = (u8)dyn.tdf_cfg[0xf]
out.tdf.st_2d_ratio             = (u8)dyn.tdf_cfg[0x10]
out.tdf.c_weight1               = (u16)dyn.tdf_cfg[0x11]
out.tdf.c_weight2               = (u16)dyn.tdf_cfg[0x12]
out.tdf.c_weight3               = (u16)dyn.tdf_cfg[0x13]
out.tdf.ltf_update_frm          = 0x19
out.tdf.ltf_en                  = (u8)dyn.tdf_cfg[0x15]
for i in 0..31:
    out.tdf.tdnf_k_delta[i] = (u8)(0x1f - i)

for i = 0..32:
    v3 = interp(i; 0,0x20; p4,  p6)
    v4 = interp(i; 0,0x20; p5,  p7)
    v5 = interp(i; 0,0x20; p8,  p10)
    v6 = interp(i; 0,0x20; p9,  p11)

    t = ((tdf_level * v3) / 0x32) * tdnf_th[i]
    if t < 0: t += 0xff
    u = clrneg(v4 + (t >> 8))
    if u > 0xffe: u = 0xfff
    out.tdf.tdnf_th[i] = (u16)u

    t = v5 * tdnf_th[0x21 + i]
    if t < 0: t += 0xff
    u = clrneg(v6 + (t >> 8))
    if u > 0xffe: u = 0xfff
    out.tdf.tdnf_th[0x21 + i] = (u16)u

copy tdnf_k[32] -> out.tdf.tdnf_k
```

The global `tdnf_start_frame` is updated as shown on every 3D-denoise call,
including when `tdf_enable == 0`. `rec_en` is asserted only when the enable bit
stays set and at least one frame has elapsed past the recorded start frame.

### 6.14 Sharpness block (`sharp_on`)

`dyn` selected by the sharpness trigger.

```
out.edge_scale_ratio  = (u8)sharp_cfg[10]
out.hfrq_scale_ratio  = (u8)sharp_cfg[11]
out.edge_conv_para    = (u8)sharp_cfg[12]
out.hfrq_conv_para    = (u8)sharp_cfg[13]
out.dir_eq_ratio      = (u16)sharp_cfg[14]
out.dir_clip_val      = (u16)sharp_cfg[15]
out.ns_lw_th          = (u16)sharp_cfg[16]
out.ns_hi_th          = (u16)sharp_cfg[17]
out.edge_th           = (u8)sharp_cfg[18]
out.hv_edge_sm_ratio  = (u8)sharp_cfg[19]
out.aa_edge_sm_ratio  = (u8)sharp_cfg[20]
```

```
w_area = sharp_cfg[0x15]     # edge white numerator
b_area = sharp_cfg[0x16]     # edge black numerator
w_frq  = sharp_cfg[0x17]     # hfrq white numerator
b_frq  = sharp_cfg[0x18]     # hfrq black numerator
und_a  = sharp_cfg[0x1a]
und_v  = sharp_cfg[0x1c]
if shared.dynamic_enable:
    t = comp.sharp_hfrq_comp_target
    w_frq += (t * w_frq) >> 8
    b_frq += (t * b_frq) >> 8
    t = comp.sharp_edge_comp_target
    t2 = comp.sharp_under_shoot_comp_target
    w_area += (t  * w_area) >> 8
    b_area += (t  * b_area) >> 8
    und_a  += (t2 * und_a) >> 8
    und_v  += (t2 * und_v) >> 8

L = sharp_level
L = clrneg(L)
if L > 999: L = 1000
shared.tune.sharpness_level = L   # persisted back

out.edge_white_stren = min(0xfff, (w_area * L) / 100)
out.edge_black_stren = min(0xfff, (b_area * L) / 100)
out.hfrq_white_stren = min(0xfff, (w_frq  * L) / 100)
out.hfrq_black_stren = min(0xfff, (b_frq  * L) / 100)
out.under_area_ctrl  = (u16)und_a
out.over_area_ctrl   = (u16)sharp_cfg[0x19]
out.under_val_ctrl   = (u16)und_v
out.over_val_ctrl    = (u16)sharp_cfg[0x1b]

copy sharp_edge_lum (33 u16, 66 bytes) -> out.sharp_edge_lum
copy sharp_hfrq_lum (33 u16, 66 bytes) -> out.sharp_hfrq_lum
copy sharp_hsv      (46 u16, 92 bytes) -> out.sharp_hsv
copy sharp_s_map    (33 u8,  33 bytes) -> out.sharp_s_map

for k = 0..32:
    v38 = interp(k; 0,0x20; sharp_cfg[6], sharp_cfg[8])
    v32 = interp(k; 0,0x20; sharp_cfg[7], sharp_cfg[9])
    for (src, dst) in [(sharp_val[k], out.sharp_val[k]),
                       (sharp_val[0x21+k], out.sharp_val[0x21+k])]:
        t = v38 * src
        if t < 0: t += 0xff
        u = clrneg(v32 + (t >> 8))
        if u > 0xffe: u = 0xfff
        dst = (u16)u
```

Min-clamps above are `min(value, 0xfff)` only (no sign clear), matching the
reference; the division is by decimal 100.

---

## 7. Table semantics

| Table | Shape | Semantics |
| --- | --- | --- |
| `gain_index_table` | u32[350] | Monotone non-decreasing gain->index table. Index `k` is the first whose value is `>= scaled_gain`. Searched only over `k = 24..348`; search failure yields 349. Values are raw gain representations comparable to `(total_gain*100)>>8`, and imply the effective gain-search ceiling `0xc8000`. |
| `gain_point_default` | s32[14] | Monotone default gain breakpoints, substituted for the configured gain breakpoints when any configured value is `< 1`. Each breakpoint is converted to a gain-table index by the same search. |
| `lum_point_default` | s32[14] | Monotone default luminance breakpoints, substituted when any configured value is `< 1`. Used directly as luminance-axis positions, so their values are expected in `0..349`. |
| `af_square_table` | u8[16] | Opaque 16-byte seed copied into the AF square LUT when `af_cfg_on` is set. |

The module mutates the configured gain/luminance breakpoint arrays in place when
it substitutes defaults. The axes and the built arrays are pure functions of the
injected tables and the configured 14 breakpoints and 14 dynamic records.

---

## 8. Ordering and cross-block dependencies

Within one `run`, blocks execute in exactly this order:

1. index computation (§6.1)
2. CNR (§6.3)
3. sharpness (§6.14)
4. saturation (§6.6)
5. contrast (§6.4)
6. brightness (§6.5)
7. CEM chroma + colour-to-gray (§6.7)
8. 2D denoise (§6.8)
9. sensor offset (§6.9)
10. black level (§6.10)
11. DPC (§6.11)
12. defog (§6.12)
13. PLTM dynamic (§6.12)
14. 3D denoise (§6.13)
15. AE config (§6.12)
16. GTM config (§6.12)
17. LCA config (§6.12)
18. AF config (§6.12)

Cross-block state:

- `shared.dynamic_enable` is **written by the contrast block** (§6.4) and read
  by sharpness, 2D denoise and 3D denoise. If `contrast_on` is clear, these
  blocks observe the stale value from a previous frame.
- `shared.module_enable_flag` bit `0x20` is **written by the 3D-denoise block**;
  the contrast block does not touch it.
- The module enables/disables camera modules only through that bit.

---

## 9. Edge cases and failure behaviour

- **Null instance / null result**: per-frame entry returns `-1` and does
  nothing. Null instance in get/set parameters returns `-1`.
- **`param_kind != 0`**: set-parameters returns `-1` and changes nothing.
- **`ae_pos_max == 0`**: luminance index becomes `0` (the `interp` degenerate
  case returns its low endpoint).
- **Scaled gain above every table entry**: `gain_i = 349`. The clamp
  `(total_gain*100)>>8` first snaps anything strictly above `0xc7fff` to
  `0xc8000`.
- **Gain axis over-run**: in the gain-indexed array construction the segment
  index is not capped. Since every gain-axis value is `<= 349`, the index
  eventually reaches 14 and then keeps reading adjacent memory (in the reference
  layout, the luminance-axis array and the bytes that follow the 14 configured
  records) as the loop advances. The corresponding luminance path *is* capped at
  13. The literal loop is given in §6.2.2; see open question 1.
- **Breakpoint validation**: a single breakpoint `< 1` (interpreted as signed)
  discards all configured breakpoints and substitutes the defaults for that
  axis (gain and luminance are validated separately).
- **`sat_on` with `sat_cfg[6] == 0`**: division by zero in the exponential
  saturation curve; undefined. Callers must supply a positive shoulder.
- **Saturation level clamps**: the combined levels saturate to
  `[-256, 512]` inclusive (upper clamp is `512`, i.e. `0x200`).
- **Contrast / brightness clamps**: both to `[-128, 128]` inclusive (upper
  clamp is `128`, i.e. `0x80`).
- **`dynamic_enable` coupling**: sharpness/denoise/3D-denoise compensation only
  applies when the contrast block set the flag this frame or it was left set.
- **Colour-to-gray disabled**: not WDR, or `wb_enable == 0`, or
  `lum_i < chroma_gray_th`, or `span_max < span_min` all disable the fade.
  The chroma blend and saturation shaping still run.
- **Colour-to-gray window normalisation**: window endpoints `h0 +/- span` may lie
  outside `[0,360)` when the spans are large; the branch structure in §6.7.3
  reduces every window modulo 360. Test-hue angles themselves are always in
  `[0,360)`.
- **CEM table bounds**: the blend writes 5888 bytes; the saturation/gray passes
  index up to byte offset ~6496 of the 6656-byte table, so part of the table is
  left as it was before the call (all-zero on a fresh context).
- **3D-denoise warm-up**: `rec_en` stays 0 until `frame_id > tdnf_start_frame`,
  and `tdnf_start_frame` is reset whenever the feature is disabled or the block
  cannot run. Because the counter is process-global, successive instances share
  it.
- **Missing context**: every block dereferences the shared context and the
  injected tables unconditionally; a missing `gen` pointer or table handle is a
  programming error (undefined behaviour), not a handled condition.
- **Sign handling of config words**: values stored as unsigned but used in
  signed arithmetic (e.g. `color_denoise`, `cem_ratio`, saturation levels) are
  interpreted as two's-complement; negative inputs can produce out-of-range
  byte truncation on output, matching the reference.

---

## 10. Open questions / ambiguities

1. **Gain-axis index over-run.** The gain-indexed array builder increments its
   segment index without the cap used by the luminance builder. Unless the
   largest gain breakpoint maps to a table index greater than 349 (impossible),
   the loop reads one axis entry and one dynamic record past the 14 configured
   items near the top of the range. The spec describes the literal behaviour;
   the intended design may have been a clamp at 13. Whether the golden vectors
   exercise the affected records (and what the out-of-range memory contains)
   should be confirmed.
2. **`0xc7fff` snap.** The scaled gain is tested with a strict `>` against
   `0xc7fff` and then forced to `0xc8000`, leaving a one-unit gap. This looks
   like an off-by-one in a saturation helper; the spec preserves it.
3. **`tdnf_start_frame` scope.** The counter is process-global rather than
   per-instance. Whether multiple ISP instances are ever active concurrently is
   unknown; with one instance the behaviour is deterministic.
4. **Saturation base 2.72.** The exponential shoulder uses `2.72` (a truncated
   `e`) rather than an exact constant; the generation formula's precision and
   rounding to the 16-bit table are not independently specified.
5. **GTM trigger reuse.** The GTM block selects its record with the AE-config
   trigger, not a GTM-specific trigger. Whether this is intentional is unclear;
   the spec encodes the observed behaviour.
6. **Contrast side effect.** The contrast block writes the shared dynamic-judge
   enable based on `contrast_enable`. If the contrast update flag is off, the
   flag is left stale, so sharpening/denoise compensation depends on history.
   Whether that is intended is unclear.
7. **CEM table tail.** The chroma table's entries beyond the blended 5888 bytes
   are written by the saturation/gray passes but never initialised by this
   module; their pre-call contents are caller-determined.
8. **`af_square_table` provenance.** The 16-byte AF seed is treated as opaque
   tuning data; its internal meaning (a squared-distance ramp) is not used by
   this module.

---

## 11. Framework integration contract (boundary)

The behaviour above assumes the per-frame entry re-reads the shared parameter
block; this section records why, as an interface fact.

- The install entry is entered by the framework's context-config step while the
  per-block update gates in the shared parameter block are still clear. The
  framework then raises those gates **in the same block** (the block is the
  core's own storage, returned by the parameter-query entry) and calls the
  per-frame entry; it does not re-enter the install entry for the raised gates.
- The per-frame path re-enters the install entry for none of its inputs: it
  rewrites the shared block in place between frames (frame counter, the
  colour-to-gray thresholds, and, at config, the update gates and the 2D
  denoise core ratios).
- A core that latches the gates only at install time therefore skips every
  gated block for the whole stream. The boundary writer gates its write-back on
  the **live** block, so it publishes the core's untouched (zero) result. On
  y623 this left the saturation channel values zero, and the downstream
  hardware-config step skipped saturation programming whenever the three
  values summed to zero (198 clean-tier occurrences; 0 in the all-vendor
  build), producing the neon-green image.

The clean core exposes `iso_set_live_params()` to re-read the per-frame fields
(update gates, denoise core ratios, frame counter, colour-to-gray thresholds)
without rebuilding the interpolation arrays. The boundary shim calls it before
each per-frame run, mirroring the framework's in-place mutation. An optional
diagnostic, enabled by setting `FREEISP_ISO_TRACE` to a file path (or `1` for
the console), prints per frame the live gate, the picked saturation record
words, the clean result and the published saturation values.
