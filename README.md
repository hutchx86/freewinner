<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# freewinner — a vendor-free ISP and H.264 codec for Yi/V833 IP cameras

Two independently written replacements for the proprietary Allwinner media stack
on sun8iw19p1 / V833-class Yi IP cameras: the **libisp 3A core** (AE, AWB, AFS,
ISO, PLTM and GTM, plus the register and framework tiers) and the **H.264 encoder
and VE driver**. Most modules were written clean-room from behaviour-only
specifications; the register tier is a disclosed exception (an earlier version
was written with direct access to the vendor binary and later re-produced from a
behaviour-only spec and re-verified — see `isp/docs/provenance.md`). Together
they build a `mediad` that links no vendor library at all and still streams from
the camera.

<div align="center">

<a href="LICENSE"><img src="https://img.shields.io/github/license/hutchx86/freewinner" alt="License"></a>

</div>

> [!WARNING]
> **Early stage — not production firmware.** Both subprojects build and pass
> their unit tests, and the fully vendor-free `mediad` has streamed both channels
> on real hardware with zero decode errors. But this is a proof of concept: the
> SDK-boundary shim layer is experimental, standalone AF / rolloff /
> motion-detect are out of scope, and on-camera bring-up can wedge the sensor
> pipeline and require a reboot.

> [!NOTE]
> Not affiliated with Allwinner Technology or any camera vendor. **No vendor
> firmware, object files or binaries are distributed here** — the differential
> harnesses, captured register vectors and extracted tables live in a private
> workspace and are never published. Built with AI assistance under human
> review. See [Legal](#legal).

## At a glance

|  |  |
| --- | --- |
| **What** | Reimplementations of the Allwinner libisp 3A core and the H.264 encoder / VE driver |
| **Hardware** | Yi IP cameras on sun8iw19p1 / V833 (y623 verified) |
| **Language** | C (ARMv7, musl) |
| **Status** | Proof of concept — both components verified on real hardware |
| **License** | AGPL-3.0-only (+ section 7 vendor-encoder linking permission) |

## How it works

The camera's stock `mediad` links four proprietary archives
(`libisp_algo_rtos.a`, `libaacenc.a`, `libvenc_codec.a` and `libVE.a`). This
project replaces them:

- `isp/` reproduces the 3A algorithms, the register/config tier, the framework
  tier and the runtime table locator.
- `codec/` reproduces the H.264 encoder and the VE driver that talks to
  `/dev/cedar_dev`.

Both are brought in through an SDK-boundary shim layer that presents the vendor
entry points, so the rest of `mediad` is unchanged. Neither component ships the
camera's larger tuning tables: those are located at runtime inside the camera's
own stock `rmm` image and cached to the SD card, so only the first boot reads
the vendor image. The ISP library does carry a small set of compiled-in tables —
the platform default configuration seed, two observed platform-default tables
(the PLTM presets and the AF filter defaults), a vendor-recovered constant (the
CM colour-temperature interpolation knots), plus the interface, dispatch and
re-derived tables itemised in
[`isp/docs/provenance.md`](isp/docs/provenance.md); the codec's own compiled-in
literals are recorded in [`codec/docs/provenance.md`](codec/docs/provenance.md).

## Repository layout

```
isp/     reimplemented libisp 3A core, register and framework tiers, table locator, shim, specs
codec/   reimplemented H.264 encoder and VE driver, specs
```

Each subproject is self-contained: its own README, LICENSE, build and spec set.

## Quick start

```sh
make -C isp check      # build and run the ISP unit tests
make -C codec check    # build and run the codec tests
```

Both build with no SDK and no vendor material. The codec's AAC unit needs a FAAC
source tree; if `FAAC_DIR` does not point at one, that unit is skipped and the
rest still runs.

## Verification

- `make -C isp check` — the ISP unit tests (the `mk/*.mk` host binaries; no
  differential suites exist in the ISP tree). `make -C codec check` — the codec
  unit **and differential** tests; the differential goldens are not shipped, so
  without them they report `SKIP`, and `ALLOW_GOLDENS=1` turns a missing
  vector back into a hard failure.
- On camera: the vendor-free `mediad` streamed both channels and both decoded
  with zero macroblock errors.

## Roadmap / known limitations

- **Working:** the clean 3A cores and register tier, the clean H.264 encoder and
  VE driver, the runtime table locator, and a full `mediad` link containing no
  vendor archive.
- **Not implemented:** standalone AF, rolloff and motion-detect — absent from the
  deployed blob, so there is no algorithm to reimplement from.
- **Experimental:** the SDK-boundary shim layer and the `rmm` table feed.
  On-camera bring-up can wedge the sensor pipeline after repeated runs; recovery
  is a reboot.
- **Documented exceptions:** the two libraries carry a small set of compiled-in
  tables, itemised with their recovery basis in `isp/docs/provenance.md` and
  `codec/docs/provenance.md`: the platform default configuration seed (the 3A
  control values and the 9-entry AWB illuminant roster), two observed
  platform-default tables (the PLTM presets and the AF filter defaults), a
  vendor-recovered constant (the CM colour-temperature interpolation knots), and
  interface, dispatch and re-derived tables. No vendor *tuning* table is compiled
  in — the device's tuning (lens, msc, PLTM and AF banks) is located at runtime in
  the camera's own `rmm` image and cached. The OTP golden-ratio path
  (`otp_enable` / `pmsc_table` / `pwb_table`) is dead in the deployed image and no
  path here populates it either, so those fields keep their initial values.

## Credits

See [CREDITS.md](CREDITS.md), and each subproject's own `CREDITS.md`.

<a id="legal"></a>
<details>
<summary><b>Legal</b></summary>

- **Not affiliated with, or endorsed by, Allwinner Technology** or any camera
  vendor. Product names and trademarks are used only for identification and
  interoperability.
- **No vendor firmware or binaries are distributed here.** The differential
  harnesses, captured register vectors and extracted tuning tables live in a
  private workspace. The only vendor material read at runtime is the camera
  owner's own stock `rmm` image, plus whatever SDK tree the builder supplies.
- **Interoperability reimplementation**, written for hardware the authors own.
- **AI-assisted.** Parts of this codebase were produced with large language
  models under a documented reimplementation process.

</details>

<details>
<summary><b>Disclaimer</b></summary>

**This is a proof-of-concept project, not a production-ready system.** Large
parts were produced with LLMs under human supervision and testing — review and
verify everything yourself. **The software is provided "as is", without warranty
of any kind.** It performs invasive operations on embedded hardware, so you can
**brick the device, lose data and void warranties**; recovery may require
physical access and is never guaranteed. By using it you accept full
responsibility. **The authors and contributors are not liable for any loss or
damage arising from its use.**

</details>

## Security

Report vulnerabilities privately through GitHub Security Advisories. The build
takes no credentials; keep SDK and device paths out of tracked files.

## License

AGPL-3.0-only. See [LICENSE](LICENSE). The network clause is deliberate: these
components are designed to be linked into a device daemon that serves video over
a network.

An additional permission under GNU AGPL section 7 allows combining these
components with the optional Allwinner vendor encoder libraries
(`libvenc_codec.so`, `libVE.so`) that a consuming daemon may load as a fallback:
see [LICENSE-EXCEPTION](LICENSE-EXCEPTION).
