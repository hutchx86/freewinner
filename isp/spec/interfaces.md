<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Clean-room interfaces (interoperability facts)

These are interface facts (entry points, ABI shape, data contract), not
Allwinner expression. They exist so the clean-room build can drop into the ISP
framework the same way the current one does.

## Build target

- 32-bit ARM, EABI5, hard-float (`-mfloat-abi=hard`), `-march=armv7ve`.
- `-fshort-enums` (the framework's enums are 1 byte).
- Portable C99; the host build uses gcc and links `-lm`.

## Table injection

No tuning tables are compiled in. The implementation receives them at runtime:

```c
typedef struct freeisp_tables { /* typed const pointers; Ae_DeltaLvTbl writable */ } freeisp_tables_t;
const freeisp_tables_t *freeisp_get_tables(void);
```

Each module reads only the tables named in its spec, via `freeisp_get_tables()`.
A missing table handle must be treated as a programming error, not silently
substituted.

## Per-module entry points (externally visible; interop contract)

| Module | Entry points |
| --- | --- |
| AE   | `ae_init`, `ae_exit`, `ae_get_params`, `ae_set_params`, `ae_run`, `ae_isr` |
| AWB  | `awb_init`, `awb_exit`, `awb_get_params`, `awb_set_params`, `awb_run`, `awb_isr` |
| AFS  | `afs_clean_init`, `afs_clean_exit`, `afs_clean_get_params`, `afs_clean_set_params`, `afs_clean_run`, `afs_clean_isr` (clean core); `afs_init`, `afs_exit` and `isp_afs_get_params`/`isp_afs_set_params`/`isp_afs_run` (shim SDK surface) |
| ISO  | `iso_init`, `iso_exit`, `iso_get_params`, `iso_set_params`, `iso_run` |
| PLTM | `pltm_init`, `pltm_exit`, `pltm_get_params`, `pltm_set_params`, `pltm_run` |
| GTM  | `gtm_init`, `gtm_exit`, `gtm_get_params`, `gtm_set_params`, `gtm_run` |

`*_init(out_ops) -> entity*` allocates per-instance state and returns the ops
vtable. `*_set_params(entity, param, result)`, `*_get_params(entity, out)`,
`*_run(entity, result)`, `*_isr(entity, stats, result)` match the framework's
call shapes. Exact struct offsets of the framework's param/stats/result types
are ABI facts supplied by the compatibility layout header; the spec describes
the fields the algorithm actually consumes.

AFS is named differently from the other five. Its clean core keeps the
`afs_clean_` infix (`include/afs_clean.h:104-121`); the library's own surface
defines none of the bare names `afs_get_params`, `afs_set_params` or `afs_isr`,
and no bare `afs_run` either (the only `afs_run` in the tree is a file-local
stand-in inside `tests/test_framework_manage.c:439`, which fills the ops member
for the test and is not library surface). The SDK-shaped AFS surface is the
shim's `afs_init`/`afs_exit`
(`shim/afs/afs_shim.c:330,366`) plus the `isp_afs_run`/`isp_afs_set_params`/
`isp_afs_get_params` member ops of `isp_afs_core_ops_t` (`shim/afs/afs_shim.c`
installs them; `include/freeisp/sdk_interop.h` declares them). `afs_init`
builds a clean core through `afs_clean_init` and forwards the ops to it. The
SDK ops struct has no ISR slot; the clean core's interrupt-shaped entry is
`afs_clean_isr`.

## What the spec must not contain

No vendor comments/strings from the original, and no
step-by-step structural transcription of their functions. Behaviour,
formulas, state machines, and table semantics only; identifiers are limited to
the interoperability surface above. See `../docs/provenance.md` for how the
interface facts were recovered.

## Real-input capture/replay (host differential)

Each shim can capture its per-frame SDK inputs on-camera and replay them
host-side, so the deployed vendor object and the clean core are compared on
identical real data. Capture is inert unless the named env var is set; the file
is opened once (lazily, on the first `run`) and one fixed-size record is
appended per `run` call. A record holds exactly the SDK structs the shim's
mapping functions consume.

| Module | Env var | Record layout | Bytes |
| --- | --- | --- | --- |
| AE  | `FREEISP_AE_DUMP`  | `[ae_param_t][isp_ae_stats_s]` | 12116 |
| GTM | `FREEISP_GTM_DUMP` | `[gtm_param_t][isp_gtm_stats_s][u16 gamma_tbl 3072][u16 drc_table 256][u16 drc_table_last 256]` | 14616 |
| AWB | `FREEISP_AWB_DUMP` | `[awb_param_t][isp_awb_stats_s]` | 37592 |
| ISO | `FREEISP_ISO_DUMP` | `[iso_param_t][struct isp_lib_context]` | 273712 |

The env value is a path; the legacy value `1` selects
`/tmp/freeisp_<mod>_dump.bin`. `gtm_param_t`'s three table pointers and
`struct isp_lib_context`'s `module_cfg` table pointers are not valid across a
capture boundary: GTM's pointed buffers are captured inline, and ISO's pointers
are re-pointed at side-local buffers by the replay.

On-camera capture (one session per module, on a build with that module's shim):

    FREEISP_<MOD>_DUMP=/tmp/freeisp_<mod>_dump.bin <run mediad>
    # then copy /tmp/freeisp_<mod>_dump.bin off the camera

Host replay (deployed vendor object vs clean-through-shim, same records; pass
`<freeisp_tables.bin>` to install the real located tables) runs the private
differential harness under `qemu-arm`, one binary per module — `diff_wdr_flow`
(AE), `diff_iso_shim` (ISO), `diff_gtm_shim` (GTM), `diff_awb_shim` (AWB) — each
taking the module dump and `<freeisp_tables.bin>` as arguments.

Each replay prints a per-region mismatch summary and ends with
`== <mod> replay summary: N frames, M with mismatches ==`. The no-argument runs
of the GTM/ISO/AWB harnesses additionally self-test the replay path on a small
synthetic capture (clean run 0 mismatches; a deliberate clean-side input
perturbation is detected).
