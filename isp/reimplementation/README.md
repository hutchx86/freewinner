<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Clean-room re-production of the register tier

This directory records the re-production exercise required by legal finding **R1**
(see the engineering audit §3): the first implementation of
`src/reg/{base,module_cfg,reg_writers}.c` was written with direct access to
the vendor binary in the same sessions, so the "independent implementation"
element of the clean-room record is unsupportable for that tier.

The remedy is a **separated re-production**: a spec-team role (allowed to read the
deployed binary) writes a behaviour-only spec, and an implementation role — which
must never read the binary or any earlier transcription — writes
the code from that spec plus the private differential harness.

## Artefacts

| Artefact | Role | Status |
|---|---|---|
| `spec/reglayer2.md` (1025 lines) | spec-team output: behaviour-only, implementation-ready, all three units | done 2026-09-16 |
| `src/reg/base.c` | implementation (re-produced) | **DONE 2026-09-16** — written from the spec + harness by an implementation-role agent with no access to the binary; the differential fully green (12 ops + 101 variants, 0 mismatches) and `make check` 0 |
| `src/reg/module_cfg.c` | implementation (re-produced) | **DONE 2026-09-16** — re-produced from the spec + harness; reg_cfg 1481/0, `make check` 0. The exercise exposed three harness-blind spots where our own unit tests contradicted the deployed object (all confirmed by black-box probe: implementation corrected to the vendor, tests fixed), plus a saturation copy direction the harness had been masking |
| `src/reg/reg_writers.c` | implementation (re-produced) | **DONE 2026-09-16** — re-produced from the spec + harness; reg 680/0, reg_cfg 1481/0, base 0/0, `make check` 0; 69/69 writers determined, with four spec §1.3 errors corrected against the deployed object |

The superseded implementations are quarantined outside the implementation role's
permitted reading set. Keep them: the private harness's historical goldens were produced with
them, and they are the fallback if a re-production fails to converge.

## Result — `base.c`, 2026-09-16

`base.c` was re-produced in the clean-room and is green: `base/run.sh` exit 0 with
`12 ops + 16 dig variants + 7 wdr variants + 12 cm variants + 12 judge variants + 12
apply variants + 20 lens variants + 22 msc variants driven, 0 with mismatches, 0
word(s) differ`, both deferral lines `none`; `make check` exit 0; the
other differentials (7 module shims, reg 680/0, reg_cfg 1481/0, `link_a8`) unchanged
and green.

The exercise earned its keep — it exposed two real defects that the old tree had:

1. **`config_dig_gain`'s WB-fold predicate was unpinned by the differential harness.** The fold
   fires iff `linear_en && wdr_en && awb_en && !wb_en`; the re-implementation
   initially inverted the `awb_en`/`wb_en` pair and still passed, because
   the statistics-setup step overwrote `wb_out` to all-256 *after* the dig-gain inputs
   were set, making the fold a no-op. A 16-combination `config_dig_gain` variant
   sweep was added to the harness, the predicate is now pinned, and the pre-existing
   `clean_tu.c` `linear_table` preload (which made the `linear_en=0` rows disagree
   for a harness reason) was removed.
2. **The spec's §3.11 was wrong.** It claimed the deployment performs no register
   write in `config_band_step` — contradicting §6 of the same document, which lists
   that write as a true vendor behaviour the differential harness cannot see. The re-implementation
   followed §3.11 and dropped the write; the unit test caught it. §3.11 is corrected
   and the write is restored.

**R1 is closed (2026-09-16).** All three units — `base.c`, `module_cfg.c`,
`reg_writers.c` — have been re-produced from `spec/reglayer2.md` by implementation-role
agents with no access to the vendor binary. An independent verifier then re-ran all
four arbiters and re-scanned the three files: 0 tool-generated identifiers,
0 verbatim comments, 0 vendor strings, and
0.000% normalized-line overlap with the analysis output. The same verifier found — and
we fixed — one regression the re-production had reintroduced (a byte-exact
temperature-trigger array copied from the spec, undoing legal finding R3) and a
missing mismatch guard in the private register differential harness.

## Access rules for the implementation role

**May read:** `spec/reglayer2.md`; `include/*.h` (the fixed
interface contract — struct layouts and entry-point names are interoperability facts);
the private differential harness sources and goldens;
`tests/**` (our own behavioural assertions); sibling units it must call
(`src/reg/{module_cfg.c,reg_writers.c,comp_ref.c}`).

**Must not read:** the vendor binary or any transcription derived from it,
the vendor SDK tree, the quarantined implementations, or any
object file. No binary inspection, no symbol-name greps against the binary.

## Arbiter

The private `base` differential harness links the candidate implementation against the
deployed object and reports, per operation, the first differing field. A pass is:
exit 0, `0 with mismatches, 0 word(s) differ`, both deferral lines `none`, sensitivity
detected for all ops. `make check` must also pass.

## Known limits of "green" (record honestly)

A green differential run does **not** prove equivalence for behaviour the harness cannot see.
The spec (`reglayer2.md` §6) lists three such items; a fourth is recorded
separately in §4 (`reglayer2.md:963-964`). All four are recorded in
the engineering audit:

1. the `config_gamma` upscale rounding detail (branch not exercised);
2. the deployment's `comp_ref` over-read for `msc_mode >= 4` (deliberately
   neutralised in the harness);
3. three harness-blind vendor behaviours (e.g. `isp_reg_enable_msc` clearing a CONTRAST
   bit on disable; the `config_band_step` register write the harness cannot see);
4. the `isp_reg_enable_*` count being 29, not 30 (spec §4, not §6).

A re-production that passes the differential harness therefore establishes behavioural equivalence
*within the tested envelope*, and that limit must be stated in any legal submission.
