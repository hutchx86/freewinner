<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Credits — freewinner

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
surface in `include/freeisp/sdk_interop.h` restates the struct, enum, macro and
entry-point declarations required to match the deployed interface; these are
interface facts, not copied implementation.

The large constant tables the algorithms need are not compiled in: they are
located at runtime inside the camera's own stock `rmm` image and then cached to
the SD card, so only the first boot reads the vendor image. The library does
carry a small set of compiled-in tables — the platform's default configuration
seed (the 3A control values and the 9-entry AWB illuminant roster), the observed
PLTM preset bank and AF filter coefficients, the re-derived colour-effect
matrices, a vendor-recovered constant (the CM colour-temperature interpolation
knots), and a number of interface and dispatch/lookup tables — all itemised
with their provenance in `docs/provenance.md`. The device-specific sensor
calibration is read from the camera's own `rmm` image at first boot.
Test fixtures under `tests/` and `shim/` embed small expected values used to
verify the modules in isolation. See `shim/integration/README.md`.

## Third-party components

No third-party component is linked; the build links the C standard library,
`libm`, `pthread` and the GCC toolchain runtime `libgcc_eh` (the integration
shim's ARM tail links `-lgcc_eh` to resolve the unwind-table reference that the
musl build's `strcmp`/`memcmp` pull in), all from the host toolchain. The tree
reproduces Linux
kernel UAPI interface
definitions for interoperability (`include/isp_dev_uapi.h`,
`include/media_utils_abi.h`); the top-level `CREDITS.md` carries the upstream
notices that reproduction requires (BSD-3-Clause for the V4L2 declarations,
GPL-2.0 WITH Linux-syscall-note for the media-controller ones) and states which
parts are necessarily identical to upstream and which are this project's own.

## Acknowledgements

- `include/freeisp/sdk_interop.h` restates the libisp ABI declarations the shim
  layer needs, so it builds against this repository alone; no Allwinner SDK
  header is included or required.

All product names, trademarks and registered trademarks are the property of
their respective owners and are used only for identification and
interoperability.
