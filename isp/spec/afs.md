<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Auto-Flicker (Mains-Ripple) Detection — Behaviour Specification

This document specifies observable behaviour only. It contains no source, no
vendor identifiers, and no reference string literals. Names used below are
neutral descriptions of the data the module consumes and produces; the exact
ABI field names/offsets are supplied by the compatibility layout header and by
`interfaces.md`.

---

## 1. Purpose and scope

The module inspects each video frame for banding caused by ripple in
mains-powered lighting. A rolling-shutter sensor exposed under a flickering
source shows alternating bright/dark bands down the image; the spatial
frequency and phase of those bands depend on the mains rate (nominally 50 Hz or
60 Hz). The module's job is to decide whether such banding is present and, when
configured to run automatically, to select between the two mains rates.

The module:

- consumes a hardware statistics block giving 128 per-column intensity sums,
  plus a small parameter block;
- accumulates 16 consecutive statistics blocks into a 16-row x 128-column
  vertical-profile matrix;
- projects each column's vertical profile onto a 16-bin spatial-frequency basis
  using two injected 32-entry integer trigonometric tables;
- declares banding present when a sufficiently large number of columns show a
  dominant, symmetric pair of frequency bins whose combined energy is more than
  a configured fraction of that column's total energy;
- writes a flicker-type enum: none, 50 Hz, or 60 Hz.

It does **not** estimate a numeric frequency, does not alter exposure or gain,
and does not perform autofocus. Despite its historical label, it is an
auto-flicker suppression module and is unrelated to autofocus.

This is a stateful per-frame algorithm. It produces at most one detection
decision every 16 qualifying frames.

---

## 2. Conventions and symbols

- Arithmetic is 32-bit two's-complement integer unless stated; signed
  overflow/wraparound is permitted, divisions truncate toward zero.
- `|x|` is the absolute value.
- Unsigned comparisons are noted explicitly; a signed difference cast to
  unsigned is used as a range test.
- Dimension constants:
  - `NCOL = 128` image columns,
  - `NROW = 16` accumulated rows,
  - `NBIN = 16` frequency bins,
  - `NTRIG = 32` trigonometric table period.
- Names in `monospace` are neutral field names defined in this document, not
  ABI names.

---

## 3. Input contract

### 3.1 Parameter block

Read every call from the per-instance parameter storage. The block is written
by the framework/caller through the get-parameters entry point (the module's
set-parameters entry point is a no-op and does not copy anything).

| Neutral name | Meaning | Type | Range | Notes |
| --- | --- | --- | --- | --- |
| `frame_index` | Monotonic frame counter | signed int | 0..2^31-1 | Frames 0,1,2 are ignored entirely (see 6.1). |
| `mode` | User's mains setting | enum int | 0=off, 1=force-50, 2=force-60, 3=auto | Other values leave the previous active state unchanged. |
| `seed_type` | Starting/detection policy | signed int | 0,1,2 significant; other values clamp | 0 = alternate on each detection; 1 = prefer/force 50; 2 = prefer/force 60. |
| `min_peak_ratio` | Detection energy threshold | signed int (percent) | nominally 0..100 | A column counts as a flicker line only when its peak-pair energy share is strictly greater. |
| `enable` | Module enable flag | signed int | 0 = disabled, non-zero = enabled | Detection is allowed only when non-zero. |
| `gain_level` | AE gain index | signed int | wide | Detection is allowed only while it equals 255, 256, or 257 (see 6.1). |
| `image_width` | Sensor width in pixels | signed int | > 0 | Divisor used to normalise the raw column sums. 0 is a divide-by-zero fault. |
| `type` | Parameter-kind tag | enum/uint8 | unused by this module | |
| `platform_id` | Platform selector | signed int | unused by this module | |
| `auto_flag_param` | Framework auto flag | signed int | unused by this module | |

The `image_width` and `gain_level` fields live inside an embedded sensor
descriptor inside the parameter block; only those two fields of that descriptor
are read.

### 3.2 Statistics block

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `column_sum[c]` | Raw hardware intensity sum for image column `c` | unsigned int | hardware-dependent, any 32-bit |
| `stats_width` | Statistics picture width | unsigned int | unused by this module |
| `stats_height` | Statistics picture height | unsigned int | unused by this module |

Exactly `NCOL = 128` column-sum entries are read, indices 0..127.

### 3.3 Injected tables

Two read-only tables, obtained through the runtime table-injection API
(see `interfaces.md`). The module must not compile in or assume their values.

| Neutral name | Meaning | Type | Length |
| --- | --- | --- | --- |
| `SIN` | One full period of a scaled sine, sampled at `NTRIG=32` equal steps | signed int[32] | 32 |
| `COS` | One full period of a scaled cosine, sampled at `NTRIG=32` equal steps | signed int[32] | 32 |

Semantics: index `m` in 0..31 represents an angle `2*pi*m/NTRIG`; values are
scaled so that a full-amplitude wave reaches about +/-100 units, rounded to
integers. The two tables are independent (i.e. `COS` is not required to equal a
shifted copy of `SIN`). See section 7.

---

## 4. Output contract

Written on every call except frames 0,1,2 (see 6.1). The module also returns a
status.

| Neutral name | Meaning | Type | Range |
| --- | --- | --- | --- |
| `detected_type` | Selected/detected mains type | enum int | 0 = none, 1 = 50 Hz, 2 = 60 Hz |

Return value of the per-frame entry point:

- `0` on a normal call;
- `-1` when the instance pointer or statistics pointer is null; in that case
  `detected_type` is set to 50 (see edge cases in section 9).

A separate get-parameters entry point returns a pointer to the instance's
parameter storage (or `-1` if the instance pointer is null). The set-parameters
entry point always returns `0` and performs no work.

---

## 5. Internal state and lifecycle

Allocated once per instance at initialisation; the instance is opaque to the
framework apart from the embedded parameter block and result.

Per-instance buffers (conceptual; exact packing is private):

| Neutral name | Shape/type | Role |
| --- | --- | --- |
| `raw` | int[16][128] | Normalised per-frame column sums, one row per qualifying frame. |
| `centered` | int[16][128] | Mean-removed, 16x-scaled copy of `raw` used by the projection. |
| `spectrum` | int[16][128] | Projection magnitude per frequency bin per column. |
| `spectrum_t` | int[128][16] | Transpose of `spectrum` (column-major access for detection). |
| `energy` | int[128] | Sum over bins of `spectrum` for each column. |
| `trend` | int[16][128] | Per-cell trend class: 0=rising, 1=falling, 2=flat. |
| `trend_counts` | int[128][3] | Per-column counts of rising/falling/flat cells. |
| `total_up`, `total_down`, `total_flat` | int | Aggregate trend counts over all columns. |
| `total_cells` | int | Nominal constant 2048; set but never read. |
| `col_sum`, `col_avg` | int[128] | Both set to the column total; written but not read after. |
| `trend_weight` | int[3] | Fixed map {rising:+1, falling:-1, flat:0}. |
| `rows_filled` | int | Accumulator: number of rows captured in the current set (0..16). |
| `type_latch` | int | Last confirmed detected type, or 0xFF = unknown. |
| `active` | int | 1 while in auto mode and detecting, else 0. |
| `current_type` | enum int | Type used to decide the next alternating result; starts 0. |
| `busy_flag` | int | Set to 1 at allocation; otherwise unused by this algorithm. |

Lifecycle:

- **Allocation/initialisation:** storage is zeroed; `type_latch = 0xFF`
  (unknown), `rows_filled = 0`, `trend_weight = {+1,-1,0}`, `busy_flag = 1`.
  Everything else is zero, including `current_type` and `active`.
- **Per frame:** the parameter block is read live. `detected_type` is written
  (mode dispatch). Only when in auto mode, enabled, and the gain condition
  holds is one statistics row captured and `rows_filled` advanced.
- **Per completed set (16 rows):** the derived buffers and trend aggregates are
  cleared, the trend/decision stage runs, and `rows_filled` returns to 0.
- **Persistent across frames:** `type_latch`, `current_type`, `active`, the
  parameter block, and the partially filled `raw` rows.
- **Gain-window exit:** `rows_filled` is reset to 0, discarding the partial set.
- **Teardown:** free the instance.

Counter/ring semantics: `rows_filled` selects the destination row as
`rows_filled mod 16`; after writing it becomes `row + 1`. A complete set is
16 consecutive qualifying frames. It is not a wrapping ring: after the 16th
row the buffer is fully overwritten on the next cycle, and partial sets are
abandoned when the gain condition fails.

---

## 6. Algorithm

### 6.1 Per-frame dispatch and gating

1. If `frame_index < 3`: return `0` immediately. `detected_type` and all state
   are left untouched.
2. Mode dispatch, which sets both the output and the `active` flag:
   - off -> `detected_type = none`, `active = 0`;
   - force-50 -> `detected_type = 50`, `active = 0`;
   - force-60 -> `detected_type = 60`, `active = 0`;
   - auto -> `active = 1`; choose a provisional type:
     let `t = type_latch`; if `t == 0xFF` (unknown) then
     `t = clamp(seed_type, 1, 2)` with the special case `seed_type == 0 -> 1`
     (i.e. 0->1, 1->1, 2->2, values below 1 ->1, values above 1 ->2);
     set `detected_type = t`.
   - other mode values: change nothing (the output and `active` keep their
     previous values).
3. If `active != 1` or `enable == 0`: return `0` (no capture, no detection).
4. Gain window: if `gain_level` is not one of {255, 256, 257} — equivalently
   `(unsigned)(gain_level - 255) >= 3` — set `rows_filled = 0` and return `0`.

### 6.2 Row capture

On a qualifying in-window frame:

- `r = rows_filled mod 16`.
- For every column `c`: `raw[r][c] = column_sum[c] / image_width` (integer
  division).
- `rows_filled = r + 1`.
- If `r + 1 != 16`, return `0` (set incomplete).
- Otherwise fall through to the decision stage. At the end of the decision
  stage (whether or not a detection fired), `rows_filled = 0`.

Thus detection can run at most once per 16 qualifying frames.

### 6.3 Trend construction

For each column `c`:

- `S[c] = sum_{k=0..15} raw[k][c]`.
- `col_sum[c] = col_avg[c] = S[c]` (written; not used downstream).
- `centered[k][c] = 16 * raw[k][c] - S[c]` for `k = 0..15`
  (mean removal; the `centered` row sums are identically zero).
- Trend classes with a deadband of one unit, comparing adjacent rows:
  - `trend[0][c] = flat` (first row is unconditionally flat);
  - for `k = 1..15`:
    - if `raw[k][c] > raw[k-1][c] + 1` -> `rising`, increment `trend_counts[c][rising]`;
    - else if `raw[k][c] < raw[k-1][c] - 1` -> `falling`, increment `trend_counts[c][falling]`;
    - else -> `flat`, increment `trend_counts[c][flat]`.
- Accumulate the three per-column counts into `total_up`, `total_down`,
  `total_flat`; set `total_cells = 2048`.

### 6.4 Trend-balance gate

Only proceed when the vertical profiles contain a substantial but roughly
balanced mix of rising and falling transitions:

```
diff = total_up + total_down
proceed  if  diff > 64  and  |total_up - total_down| < diff / 2
```

`/` is integer division. (The first comparison is a strict `>` on a
non-negative quantity; the second requires the two directions to differ by less
than half their total, i.e. neither dominates by more than roughly 3:1.)

When it proceeds, count "imbalanced rows":

```
imbalanced = 0
for row k = 0..15:
    w = sum_{c=0..127} map[ trend[k][c] ]      map: rising=+1, falling=-1, flat=0
    if |w| > 32: imbalanced += 1
```

Run detection only when `imbalanced < 16` (i.e. not every row is strongly
one-sided).

### 6.5 Rotation / projection

Per column `c`, project the `centered` vertical profile onto `NBIN = 16`
spatial-frequency bins. For bin `b` in 0..15, with per-row index step
`p = 2*b`, and for row `k` in 0..15, the table index is:

```
m(k,b) = (p * k) mod 32 = (2*b*k) mod 32        (always in 0..31)
```

Accumulate two projections and take their absolute-value sum:

```
a[b][c] = sum_{k=0..15} SIN[ m(k,b) ] * centered[k][c]
q[b][c] = sum_{k=0..15} COS[ m(k,b) ] * centered[k][c]
spectrum[b][c] = |a[b][c]| + |q[b][c]|
```

Then the per-column total energy is the sum over bins:

```
energy[c] = sum_{b=0..15} spectrum[b][c]
```

Interpretation: bin `b` corresponds to about `b/16` cycles per accumulated row
across the 16 rows; bins above 8 mirror bins below 8, so a physical banding
frequency appears as a symmetric pair `b` and `16-b`. Bin 0 is identically
zero because `centered` is zero-mean.

Build the transposed view `spectrum_t[c][b] = spectrum[b][c]`.

### 6.6 Peak-pair detection

For each column `c` independently:

- `m1` = index of the first (lowest-index) maximum of `spectrum_t[c][0..15]`;
  ties keep the earlier index.
- `m2` = index of the maximum over bins excluding `m1` (strictly greater
  comparisons, first occurrence). The selection always excludes `m1`, so
  `m2 != m1`; ties resolve to the lowest such index.
- The pair qualifies if, using unsigned range tests:
  - `m1` in [4, 12],
  - `m2` in [4, 12],
  - `m1 + m2 == 16` (symmetric about bin 8).
- If it qualifies and `energy[c] != 0`, compute the peak-pair share:

  ```
  share[c] = (spectrum_t[c][m1] + spectrum_t[c][m2]) * 100 / energy[c]
  ```

  and count the column as a flicker line when `share[c] > min_peak_ratio`
  (strict).

Detection result: `flicker_present = (flicker_lines > 35)`, i.e. at least 36
of the 128 columns must qualify.

### 6.7 Flicker-type state machine

The detection result only updates the type in auto mode. When
`flicker_present` is true:

- If `seed_type == 0` (alternate):
  `detected_type = (current_type == 50) ? 60 : 50`
  (so the first decision is 50 because `current_type` starts at 0).
- Otherwise (a locked preference):
  `detected_type = (seed_type == 2) ? 60 : 50`
  (values other than exactly 2 select 50).

Then persist:

```
current_type = detected_type
type_latch   = detected_type
```

If `flicker_present` is false, `detected_type` keeps the provisional value set
during mode dispatch and the latches are unchanged.

States summary:

| State | `active` | Output source |
| --- | --- | --- |
| off | 0 | none |
| force-50 | 0 | 50 always |
| force-60 | 0 | 60 always |
| auto, latch unknown | 1 | seeded from `seed_type`, clamped to {50,60} |
| auto, latch known | 1 | latch value; replaced on each detection (alternate or lock) |

---

## 7. Table semantics

`SIN` and `COS` are one-period samplings of a scaled sinusoid:

- period = 32 indices = one full turn; sample `m` is at angle `2*pi*m/32`;
- scale: full amplitude around 100 integer units (values roughly in
  [-100, +100]);
- the pair is used as a rotation/projection kernel: `SIN` supplies the
  imaginary part, `COS` the real part, so
  `SIN[m]^2 + COS[m]^2` is roughly constant;
- index derivation in this module is always `m = (2*b*k) mod 32` with
  `b` in 0..15 and `k` in 0..15. Since `2*b*k` is always even, only even table
  entries (0,2,...,30) are ever used.

Because the tables are runtime-injected, the implementation must read them via
the injection API, treat a missing handle as an error, and never hard-code
their contents.

No other numeric tables are used.

---

## 8. Constants

| Constant | Value | Role |
| --- | --- | --- |
| `NCOL` | 128 | Columns per statistics block and per decision. |
| `NROW` | 16 | Rows accumulated per decision set. |
| `NBIN` | 16 | Frequency bins (projection steps `2*b`, `b`=0..15). |
| `NTRIG` | 32 | Trig-table period and modulo bound. |
| row-index mask | 0x1F | Applied to the trig index. |
| first valid frame | 3 | Frames with index < 3 are ignored. |
| gain window | 255..257 | AE gain indices at which detection is allowed. |
| trend deadband | 1 | Adjacent-row difference needed to call rising/falling. |
| trend-mix threshold | 64 | Minimum `total_up + total_down`. |
| trend-balance divisor | 2 | `|up-down| < (up+down)/2`. |
| row-imbalance limit | 32 | `|weighted row sum|` above this marks a row imbalanced. |
| row-imbalance count | 16 | All 16 rows imbalanced rejects the frame. |
| bin lower/upper bound | 4 / 12 | Accepted peak indices (unsigned range width 9). |
| symmetric bin sum | 16 | `m1 + m2`. |
| peak-pair energy scale | 100 | Percentage multiplier for the share. |
| minimum flicker lines | 35 (strict >) | i.e. at least 36 of 128 columns. |
| nominal total cells | 2048 | Written but not read. |
| unknown latch | 0xFF | Sentinel for "no type yet". |

---

## 9. Edge cases and failure behaviour

- **Frames 0..2:** no output is written and no state changes; the caller must
  not rely on `detected_type` during this warm-up.
- **Null instance or null statistics (non-null result):** the per-frame entry
  point returns `-1` and sets `detected_type = 50`. A null result pointer is
  not defended against and is a programming error (undefined behaviour).
- **Disabled / forced modes:** no statistics are consumed and `rows_filled` is
  not advanced.
- **Gain leaves the window mid-set:** `rows_filled` resets to 0; the partial
  `raw` rows are discarded. Re-entering the window starts a fresh set.
- **`image_width == 0`:** division by zero in row capture; undefined. Callers
  must supply a positive width.
- **`energy[c] == 0`:** the column is skipped (cannot be a flicker line).
- **Ties in the spectrum:** the first (lowest) local maximum wins for `m1`;
  `m2` is the best bin other than `m1`. Ties can make `m2` share `m1`'s value.
- **Strict comparisons:** the peak share must be strictly greater than
  `min_peak_ratio`; `min_peak_ratio >= 100` can never fire.
- **Integer overflow:** `share` computes `(peak1 + peak2) * 100` in 32-bit
  signed arithmetic; very large projections can overflow, which is accepted as
  wraparound behaviour and should be reproduced.
- **Unsigned range tests:** both the gain window and the bin-index bounds rely
  on unsigned wraparound of a subtracted signed value; implement exactly as
  written rather than as chained comparisons on the signed values.
- **Unknown mode value:** leaves `active` and `detected_type` stale from the
  previous frame, then proceeds to the `active`/enable checks; effectively the
  module may keep detecting under the previous mode.
- **`seed_type` values outside 0..2:** clamp to {50,60} on seeding; on detection
  anything other than 0 or 2 selects 50.
- **First decision with `seed_type == 0`:** yields 50 because `current_type`
  starts at 0; subsequent detections alternate.
- **Missing/partial statistics:** the module assumes `column_sum` has exactly
  128 valid entries; no bounds checking is performed.
- **Diagnostics:** optional diagnostic output is emitted when a global log-mask
  bit (0x40) is set; it has no effect on results and is not part of the
  behavioural contract.

---

## 10. Open questions / ambiguities

1. **Gain window rationale.** Why exactly three consecutive gain indices
   (255, 256, 257) are accepted, and whether they are absolute gain values or a
   sentinel range, is not derivable from the module. The spec encodes the
   literal condition.
2. **`col_avg` semantics.** The field named like an average receives the
   column *sum* and is never read; it may be vestigial. No behaviour depends on
   it.
3. **Unused point/peak classification.** The instance reserves a large
   per-cell point/character structure and a nominal 2048 total-cell field that
   this algorithm never uses. Their intended role is unknown.
4. **Trig-table generation.** The observed tables deviate slightly from
   `round(100*sin(2*pi*m/32))` at several indices, so the exact generation
   formula (scale, rounding, phase convention) is unclear. Since the tables are
   injected, behaviour depends only on their runtime values.
5. **Second-maximum tie handling.** The fallback used when the excluded index
   is 0 produces a specific candidate; whether this is intended or a latent
   bug is unknown. Reproduce the described selection.
6. **Column threshold inclusivity.** The module rejects exactly 35 flicker
   lines; it is unclear whether the intended threshold was 35 or 36 columns.
7. **Physical frequency mapping.** The bin pair symmetric about 8 maps to one
   physical banding frequency; the exact row-rate-to-mains-rate relation
   (exposure/line-time dependence) is not modelled here.
8. **Latch reset.** There is no path that returns `type_latch` to the unknown
   sentinel; whether a mode change or parameter update should reset it is
   unspecified. The set-parameters entry point currently does nothing.
9. **`total_cells` value.** 2048 = 16*128 is stored whereas the trend
   comparison actually produces at most 15*128 = 1920 entries; the field is
   unused, so the discrepancy is harmless but unexplained.
