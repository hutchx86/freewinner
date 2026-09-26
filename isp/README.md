<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# freewinner

An independently written C reimplementation of the Allwinner **libisp 3A**
algorithms — AE (auto exposure), AWB (auto white balance), AFS (anti-flicker),
ISO, PLTM and GTM tone-mapping — together with the register/config tier that
drives them, for sun8iw19p1 / V833-class Yi IP cameras.

> [!WARNING]
> **Early stage — not fully functional.** The clean 3A cores and register tier
> build and pass their unit tests, but this is not yet a drop-in replacement for
> the vendor library. There is **no standalone auto-focus (AF), rolloff or
> motion-detect algorithm module** (the deployed RTOS blob ships them only as
> init/exit stubs, so there is no algorithm in the target to reimplement from).
> The SDK-boundary `shim/` layer and the on-camera integration are experimental
> and are **not** covered by `make check`. Treat this as a work in progress, not
> production firmware.

## Provenance

This is clean-room work with one disclosed exception: the register tier
(`src/reg/{base,module_cfg,reg_writers}.c`) was first written with direct access
to the vendor binary and later re-produced from a behaviour-only specification
and re-verified — the full statement is in `docs/provenance.md`. Everywhere else
a behaviour-only specification was written from observing the *deployed*
on-camera binary (`libisp_algo_rtos.a`, ARMv7, Melis RTOS); the implementation
was written from that specification and then verified
against the deployed object under qemu-arm, module by module. No Allwinner
implementation code or comments are reproduced. The interoperability ABI
surface (`include/freeisp/sdk_interop.h`) restates the struct, enum, macro and
entry-point declarations required to match the deployed interface; those are
interface facts, not copied implementation.

The large constant tables the algorithms need are not compiled in: they are
located at runtime inside the camera's own stock `rmm` image and then cached to
the SD card, so only the first boot reads the vendor image. The library does
carry a small set of compiled-in tables — the platform's default configuration
seed (the 3A control values and the 9-entry AWB illuminant roster), two observed
platform-default tables (the PLTM preset bank and the AF filter defaults), a
vendor-recovered constant (the CM colour-temperature interpolation knots), and
interface, dispatch and re-derived tables — and the device-specific sensor
calibration is read from the camera's own `rmm` image at first boot; the
compiled-in set is itemised with its class in `docs/provenance.md`.
Test fixtures under `tests/` and `shim/` embed small expected values used to
verify the modules in isolation. See `CREDITS.md` and
`shim/integration/README.md`.

## Layout

| Path | Contents |
| --- | --- |
| `src/`, `include/` | clean-room 3A modules, register tier and runtime table locator |
| `src/framework/` | clean-room libisp framework tier (events, helper, tuning, manage, `isp`) and the Phase-2 UAPI binding (`isp_dev_uapi`) |
| `src/utils/` | clean-room media-utils units (frame size, pixel format, frame pool; bitmap, message queue, semaphore, time helpers, channel-copy helper) |
| `include/freeisp/` | ABI and table-interface headers |
| `shim/` | SDK-boundary adapters and the `rmm` table feed |
| `tests/`, `mk/` | unit tests and their build fragments |
| `spec/` | behaviour-only clean-room specifications, one per module (spec-team output) |
| `reimplementation/` | the register-tier re-production record and the implementation-role access rules |
| `docs/` | provenance and deviations (`docs/provenance.md`) |

## Build and test

```sh
make            # build every unit test and the table locator
make check      # build and run the unit tests
make clean
```

The clean modules, the register tier, the table locator and the tests build and
run with **no SDK and no vendor material**.

Two test expectations were taken from the private differential harness's golden
vectors, which are withheld from this repository by a standing owner default: the
merge-geometry cases in `tests/test_pltm.c` and the AWB tie-break case in
`tests/test_awb.c`. Those tests still run here because the expected values are
written into the test source; the harness and its goldens are not shipped.

The `shim/` layer presents the clean cores through the SDK's
`*_init`/`*_run` entry points, and includes the SDK-ABI adapter for the
register/config dispatch tier (`shim/module_cfg/`, which translates the
framework's real `struct isp_module_config` to and from the clean context). It
builds against the repo's own `include/freeisp/sdk_interop.h`, which declares the
required libisp ABI surface inline, so no Allwinner SDK headers are needed; it is
still **not** part of the default `check`. Build it separately (add
`CROSS=<prefix>` for the ARM targets):

```sh
make -C shim/ae                              # host test
make -C shim/ae CROSS=<prefix> arm           # ARM target
```

For a differential build against the vendor header instead, pass the Allwinner
libisp include directory and enable the override:

```sh
make -C shim/ae SDK_INC=<libisp include dir> FREEISP_SDK_INTEROP_VENDOR=1
```

The runtime table feed under `shim/integration/` is SDK-free and builds
standalone, but running its test needs stock `rmm` images, which are not shipped
(see `shim/integration/README.md`).

## Status and known limitations

- **Implemented:** AE, AWB, AFS, ISO, PLTM and GTM clean-room cores, the
  register/config tier, the runtime table locator, and the libisp framework tier
  (`src/framework/`: events, helper, tuning, manage, `isp` plus the Phase-2 UAPI
  binding `isp_dev_uapi`), each with unit tests. The media-utils units
  (`src/utils/`) are also implemented and unit-tested: frame size, pixel format
  and frame pool (differential-verified), plus the phase-2 rewrites — bitmap
  size, the message queue, the counting semaphore, the time/condition-wait
  helpers and the channel-copy helper — each differential-verified against the
  deployed object under qemu-arm. The dead media-utils exports and the two
  dead units (`mm_comm_venc`, `cedarx_avs_counter`) are omitted.
- **Not implemented:** standalone AF, rolloff and motion-detect algorithm
  modules — absent from the deployed RTOS blob and therefore out of scope for
  this reimplementation.
- **Experimental:** the `shim/` SDK-boundary layer (including `shim/module_cfg/`)
  and the `rmm` table feed. On camera, the **fully vendor-free `mediad`** (this
  tree's clean 3A, register tier, framework and media-utils, plus the clean
  `codec/` H.264 encoder — no vendor libisp or codec archive in the link)
  streams both channels, and both decode with zero macroblock errors
  (2026-09-20 (7), fc70). The clean tier divides with the deployed objects'
  hardware `sdiv`/`udiv` semantics through `include/freeisp/sdiv.h`; GCC's
  `__aeabi_idiv` helpers raise SIGFPE where the hardware returns `0`, which was
  the last bring-up blocker — see `spec/divide-semantics.md` for the
  contract, the 12 sites and the regression that reproduces the fault. The
  framework's `default_reg` reset image is injected rather than compiled in; the
  deployment keeps the zero-seed default (no reset image is shipped).

## Credits

See `CREDITS.md`.

## Legal

<details>
<summary>Non-affiliation, notices and provenance</summary>

- **Not affiliated.** This project is not affiliated with, authorised, or
  endorsed by Allwinner Technology or any camera vendor. Product names and
  trademarks are used only for identification and interoperability.
- **No vendor binaries.** No vendor object files, libraries, firmware images or
  extracted tuning tables are included in this repository. A small number of
  vendor tuning-image class labels (`"AH Light"`, `"Outlier Light"`) appear as
  lookup keys and anchors (`src/tables/rmm_tables.c` compiles in one as the
  locator anchor); there is no vendor tuning table in the tree. The only vendor
  material read at runtime is the camera owner's own stock `rmm` image. See
  `docs/provenance.md`.
- **Interoperability reimplementation.** The code is an independent
  reimplementation written for interoperability with hardware the authors own.
  See `CREDITS.md` for the provenance statement.
- **AI-assisted.** Parts of this codebase were produced with the assistance of
  large language models under a documented clean-room process.
- **Proof of concept.** Provided as-is for research and educational use, with no
  warranty; see `LICENSE`.

</details>

## Licence

AGPL-3.0-only. See `LICENSE` and `CREDITS.md`.
