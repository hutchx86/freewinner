<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Integer divide semantics in the clean tier

Status: implemented 2026-09-20 (7). Closes the on-camera all-clean SIGFPE that
had been the last bring-up blocker (recorded 2026-09-18).

## 1. Finding

**The deployed libisp objects divide with the ARM integer divide instructions,
which saturate; the clean tier divided with the ABI helpers, which trap.**

- Deployed: `isp_manage.o` divides with a signed divide instruction with no
  compare/branch guard in front of it (recorded 2026-09-18). The ARM ARM defines
  the instruction's edge cases:

  | case | ARM SDIV / UDIV result |
  |---|---|
  | divisor `0` | `0` |
  | `INT32_MIN / -1` (signed only) | `INT32_MIN` |

  Neither raises a fault.

- Clean tier: GCC lowers a data-dependent C division to `__aeabi_idiv` /
  `__aeabi_uidiv` (the RTOS ABI helpers, pulled from the musl libgcc), and those
  **raise SIGFPE** on a zero divisor and on `INT32_MIN / -1`. A guard of the form
  `d ? a / d : 0` removes the zero case but *not* `INT32_MIN / -1`, because the
  division still lowers to the same helper.

`-fwrapv` does not change this; it only affects signed overflow, not division.

## 2. Consequence on camera

The all-clean `mediad` reached `high channel ready` / `low channel ready`, then
died with `Arithmetic exception` (`exit 136`) a few frames later. An on-camera
SIGFPE handler plus a debug-symbol lookup attributed the fault to the clean register tier
(`config_wdr` at the time). It is not one site: it is the whole class, and the
crash site moves with the frame content.

## 3. Contract

`include/freeisp/sdiv.h` (the ISP tree; the codec tree has no such header) provides:

| helper | deployed equivalent | semantics |
|---|---|---|
| `freeisp_sdiv(int32_t n, int32_t d)` | `sdiv` | `d == 0` → `0`; `INT32_MIN / -1` → `INT32_MIN`; else `n / d` |
| `freeisp_udiv(uint32_t n, uint32_t d)` | `udiv` | `d == 0` → `0`; else `n / d` |

Rule: **any division whose divisor is not provably nonzero must go through one
of these.** A literal nonzero constant divisor, or a divisor an enclosing branch
already proves nonzero, needs no helper.

## 4. Sites fixed

12 sites, in the register/config tier, the AFS module and the GTM module:

| site | divisor | reachable zero divisor? |
|---|---|---|
| `src/reg/base.c:201` `config_dig_gain` sensor-offset | `d = (int16)sensor_offset[1] >> 4 + 0x100` | **yes** — `d == 0` when the field's bits 4..15 are `0x100` (`sensor_offset[1]` in `[-4096,-4081]`) |
| `src/reg/base.c:209` `config_dig_gain` black-level | same shape, `offset[1]` | **yes** |
| `src/reg/base.c:547` `config_defog` | `0x3ff - pre` | **yes** — `pre == 0x3ff` (reached via `isp_apply_colormatrix`) |
| `src/reg/base.c:1124/1134/1148/1387` `handle_ae` / merge path | `win_pix_n` | **yes** — `stats_ae_win_pix_n()` returns `(w*h) >> sh`, which is `0` whenever the AE register window is under 4 pixels (`ae_mode 0`) |
| `src/afs/afs_clean.c:378` | `(unsigned)p->image_width` | **yes** — no validation on the param |
| `src/gtm/gtm_clean.c:122` LUT interpolator | `hi - lo` | **yes** — a flat gamma LUT makes a matching pair equal |
| `src/gtm/gtm_clean.c:183` otsu | `cnt` | **yes** — an all-zero histogram never matches a bin, so `cnt` stays `0` |
| `src/gtm/gtm_clean.c:258/260` alpha ramp | `p->black_level`, `256 - p->white_level` | defensive — the enclosing `i < black_level` / `i > white_level` test already proves the divisor nonzero (or the branch is unreachable). Patched for consistency, not because a fault was observed. |

## 5. Regression

Two host tests, in both trees:

- `tests/test_base.c::test_zero_divisor_paths` — the helper contract, then each
  reachable base-tier case (both `config_dig_gain` paths, `config_defog` via
  `isp_apply_colormatrix`, and `isp_handle_stats` with a 1x1 AE window so
  `win_pix_n == 0`).
- `tests/test_gtm.c::test_zero_divisor_paths` — a flat gamma LUT and an all-zero
  histogram.

**The test is proven live.** Temporarily reverting the `handle_ae` `win_pix_n`
site to a raw `/` makes `build/test_base` die with `Floating point exception`
(exit 136) — exactly the on-camera failure mode — and restoring the helper makes
it pass.

## 6. Validation

| check | result |
|---|---|
| `make check` (development tree) | green |
| `make -C isp check` | green |
| the private differential harness (full suite) | green — base_sdk 114/0, ae 156/0, awb 94/0, afs 67/0, gtm 49/0, iso 1109/0, pltm 83/0 mismatches |
| on camera (y623, fc70) | the **all-clean** `mediad` (clean 3A + register tier + framework + media-utils + clean H.264 codec; no vendor libisp/codec archive is linked) streams both channels; no SIGFPE; ffmpeg decodes both with zero macroblock errors |

The differential staying at zero mismatches is the point: the change can only
alter a result where the divisor is zero or `INT32_MIN / -1`, and there the
deployed object already produced the saturating value.
