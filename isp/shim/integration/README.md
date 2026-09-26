<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Integration - on-device runtime table feed

Feeds the stock-`rmm` libisp tuning tables to the clean-room shims so the clean
modules do not compile the device tuning tables in.  The consuming `yi-mediad/`
tree is touched only by the proposal in `../INTEGRATION.md`; the locator is
reused as-is.

## API

`freeisp_shim_tables.h`:

| Entry point | Role |
| --- | --- |
| `int freeisp_shim_tables_from_rmm(const char *path)` | locate the tables in an `rmm` image, map them per module, install them |
| `int freeisp_shim_tables_from_memory(const void *image, size_t len)` | same, from a caller-owned buffer |
| `int freeisp_shim_tables_from_cache(const char *path)` | install from a pre-located bundle; never reads a vendor image |
| `int freeisp_shim_tables_save_cache(const char *path)` | persist the currently installed set as a bundle |
| `int freeisp_shim_tables_from_rmm_or_cache(rmm, bundle)` | **deploy entry point**: bundle first, locator fallback, then seed the bundle |
| `void freeisp_shim_tables_free(void)` | uninstall (shims revert to pilot defaults) and release owned copies; idempotent |
| `const void *freeisp_shim_tables_get(id)` | mapped per-module block (`<mod>_clean_tables_t`, `afs_clean_trig_t`), or NULL |

Installation goes through the existing per-module shim knobs:
`iso_shim_set_tables`, `ae_shim_set_tables`, `awb_shim_set_tables`,
`gtm_shim_set_tables`, `pltm_shim_set_tables`, `afs_shim_set_trig_tables`.
The shims latch these at their clean `*_init()`, so load before init (same rule
as the pilot default setters).

## Location

All location work is `src/tables/rmm_tables.c` (`freeisp_tables_locate`
/ `_load_rmm` / `_free`): two anchors (the re-derived `Ae_Log2` formula and the
vendor `"Outlier Light"` tuning-image class label, compiled in as the locator's
byte-search string) plus the offsets-only
`include/freeisp/tables_layout.h`, with fail-closed structural validators.
This layer only adapts the 38-member vendor `freeisp_tables` view — of which 36
members are located by `tables_layout.h` and `isp_cm_color_temp`/`module_attrs`
sit outside the contract — to the six clean shapes.

## Cache bundle (decoupling from the vendor image)

The deploy path is cache-first so `mediad` does not read the vendor `rmm` on
every boot (see `../INTEGRATION.md` §B).  After the locator has produced a full
set, `freeisp_shim_tables_save_cache()` writes it to a single self-describing
bundle; later boots install from that bundle through
`freeisp_shim_tables_from_cache()` and never open `rmm`.  The locator remains a
fallback for the first boot (or a lost cache), and re-seeds the bundle.

| Property | Value |
| --- | --- |
| Default path | `FREEISP_TABLE_BUNDLE_PATH` = `/tmp/sd/unifi/isp_cfg/freeisp_tables.bin` (same directory as yi-mediad's `RMM_TUNING_CACHE_DIR`) |
| Format | 32-byte header (`"FWTABL01"`, version, count, layout CRC32, total size, payload CRC32, header CRC32) + the located tables in layout order, little-endian |
| Portability | `layout_crc` covers only architecture-independent descriptor fields, so a host-written bundle is byte-identical to an ARM-written one (verified) |
| Atomic write | `<path>.tmp` + `rename()`; parent directory created on first write |
| Fail-closed load | bad magic/version/count/layout CRC/header CRC/payload CRC, wrong length, or any structural validator failure ⇒ nothing installed, `-1` |
| Fallback order | valid bundle → absent/corrupt bundle → locator from `rmm` (then re-seed) → nothing (shim defaults) |
| Kill switch | `MEDIAD_NO_RMM_TUNING` (applied in `main.c`) disables the whole feed, cache included |

Bundle bytes are the device's own located tuning, written to the SD card at
runtime; the clean modules do not ship the device's tuning tables.  The only
compiled-in configuration data that is not a device tuning table is the
platform default configuration seed (plus the interface, dispatch and re-derived
tables); see `isp/docs/provenance.md` for the full picture.  The module resolution table
(`freeisp_table_locs[]`) is the only thing that knows how to slice a bundle, and
it carries offsets/sizes only.

## Mapping

| Clean module | field | vendor table | shared with |
| --- | --- | --- | --- |
| AE | `log2` | `Ae_Log2` | |
| AE | `evtab` (writable) | `Ae_DeltaLvTbl` | |
| AE | `conv` | `AeConverData` | GTM, PLTM |
| AE | `touchprob` | `AeProbData` | |
| AE | `kernel` | `AeHistData` | GTM |
| AE | `pregamma` | `AeGammaPre` | GTM |
| AE | `blmask` | `AeBackLightWeight` | |
| AE | `wght_matrix/avg/center/over/under` | `Ae_LumWeight_win/avg/center`, `Ae_OverExp_LumWeight`, `Ae_UnderExp_LumWeight` | |
| AE | `net_in/net_bias/net_out` | `IW`/`b1`/`LW` | |
| AE | `fno_ladder` | `ae_fno_def` | |
| AE | `fno_def` | `ae_fno_def` | (injected default ladder) |
| AE | `table_default` | `AeTblDef` (whole `ae_desc_t` copy) | |
| AE | `auxprob` | `AwbProbData` | AWB |
| AWB | `trust` | `AwbProbData` | AE |
| AWB | `speed_w` | `AwbSpeedData` | |
| AWB | `class_label` | `AwbLightClassName` | |
| AWB | `std_trust` | `AwbStdTempWeight` | |
| AWB | `temp_bright` | `AwbTempLvWeightDef` | |
| AFS | `sine`/`cosine` | `afs_sin`/`afs_cos` | |
| ISO | `gain_index_table` | `TBL2GAIN` | |
| ISO | `gain_point_default`/`lum_point_default` | `iso_gain_point`/`iso_lum_point` | |
| ISO | `af_square_table` | `af_square_lut` | |
| GTM | `guide_linear/low/high` | `gd_curve_linear/low/high` | |
| GTM | `pre_gamma` | `AeGammaPre` | AE |
| GTM | `eq_kernel` | `AeHistData` | AE |
| GTM | `converge` | `AeConverData` | AE, PLTM |
| PLTM | `strength_bank` | `pltm_stren_tbl_buf` | |
| PLTM | `converge_bank` | `AeConverData` | AE, GTM |

Every field of every `*_clean_tables_t` is populated from the locator, except two
that the located feed deliberately leaves **NULL**: `awb_clean_tables_t.safe_gain`
(`isp/include/awb_clean.h:146`) and `iso_clean_tables_t.af_iir_s`
(`isp/include/iso_clean.h:332`). `freeisp_shim_tables.c:171,190` sets both to
`NULL`; on that path the shim latches the NULL-bearing struct
(`awb_shim_set_tables`/`iso_shim_set_tables`) and the clean cores skip their
NULL-guarded fallbacks (`awb_clean.c:1344` `if (sg != NULL)`, `iso_clean.c:893`
`if (e->tab->af_iir_s != NULL)`; ISO then programs zero IIR feedback), so these
two fields are **not** set by the integration feed. The shim's own built-in defaults
(`awb_shim.c:142` unity quadruple, `iso_shim.c:67` zero vector) apply only when
no external tables are installed at all (`*_shim_set_tables(NULL)`), not on the
located path.  The deployed locator also recovers `anti_gamma_table`, `rgb2yuv_matrix`,
`lsc_trig_cfg_def` and `msc_trig_cfg_def`; here they are not dropped. The
adapter installs them through `base_shim_set_locator`
(`isp/shim/base/base_shim.c:239-250`), which `freeisp_shim_tables.c:204-207`
calls, and the clean `base_tables_t` carries them as `anti_gamma` (`:546`),
`rgb2yuv_base[6]` (`:547`), `lsc_trig_def` (`:537`) and `msc_trig_def` (`:541`)
(`isp/include/base.h`); they are consumed in the register tier
(`isp/src/reg/base.c`, the LSC/MSC/anti-gamma paths). Two more it knows about,
`isp_cm_color_temp` and `module_attrs`, are not part of this tree's layout
contract (`include/freeisp/tables_layout.h`) at all, so nothing here locates
them.

## Build / test

```
make          # host  -> build/test_shim_tables + build/test_table_bundle
make arm      # static musl ARM -> build_arm/test_shim_tables.arm + test_table_bundle.arm
make run-arm  # qemu-arm both ARM binaries
```

`test_shim_tables` covers the locator path (132 checks against the two stock
images, unchanged).  `test_table_bundle` covers the cache path: bundle
round-trip equality with the located set, the corruption/fail-closed matrix,
and the cache→locator→seed fallback order (116 checks against the same two
images).

`rmm` images are vendor firmware and are not shipped with this repo. Supply
them as arguments or through `FREEWINNER_RMM_IMAGES` (colon-separated); with no
image the test performs no checks and exits 0:

```
FREEWINNER_RMM_IMAGES=/path/rmm_a.bin:/path/rmm_b.bin make check
```

`test_shim_tables.c` loads each supplied image through both entry points and checks
sampled values (`Ae_Log2[5] == 2585`, ISO/AFS breakpoints, AWB labels/weights,
shared-table pointer identity, `Ae_DeltaLvTbl` writability) plus that every
shim setter received exactly the published block and was reset on free.

## Gaps / caveats

- **Host test uses capture stubs** for the six setters, not the real shim
  objects: each shim defines the same `freeisp_get_tables()`, so no two can be
  linked into one binary.  The provider->setter call and the block it passes are
  verified; setter->clean-core plumbing is covered by each module's shim test.
- **Deployment link constraint**: the provider references all six setters, so a
  firmware image that links it must link all six shims.  That in turn hits the
  duplicate `freeisp_get_tables()` unless the consumer renames it per module
  (`-Dfreeisp_get_tables=<mod>_shim_get_tables` in both the clean and shim TUs).
  This is a property of the current shim layout, not of this layer.
- `AeTblDef` is copied into an owned `ae_desc_t` (size guarded == 252) rather
  than aliased, to keep the vendor pointer's `HW_S32` element type out of the
  clean struct.
- The provider/API enum is `-fshort-enums` sensitive on ARM; build caller and
  provider with the same flag (the project ARM flags already do).
