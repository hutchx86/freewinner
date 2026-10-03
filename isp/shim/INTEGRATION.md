<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Integrating the clean-room ISP shims into yi-mediad (`mediad`)

Status: **proposal, ready to apply.** Written 2026-09-15 for the `yi-mediad`
project. Nothing in the `yi-mediad` checkout was modified to produce it (the
patch is authored read-only); the only file created is this one.

> **Update 2026-09-26.** This is the historical integration and verification
> record. Since then the vendor framework is gone (the r1 clean-room framework
> replaced it) and the shims no longer speak the SDK ABI: each 3A shim hands the
> framework its `fwi_*_core_ops_t` vtable directly, `base`/`module_cfg` take
> `struct fwi_isp_ctx` / `struct fwi_hw_module_cfg`, and
> `include/freeisp/sdk_interop.h` and the SDK-header build option were removed.
> The SDK type names below describe the interface as it was verified.

Target projects and trees:

| Role | Path |
| --- | --- |
| Target project (`mediad` build) | the `yi-mediad` checkout |
| Clean-room shims | this repository (`shim/` + `src/` + `include/`) |
| Table locator (AGPL-3.0-only) | this repository |
| Vendor archive (unchanged) | `work/media_daemon/prebuilt/libisp_algo_rtos.a` |

Apply all hunks below from your `yi-mediad` checkout root.

---

## Findings that drive the diffs

1. **The archive cannot be dropped entirely.** The daemon needs
   `isp_base.o` (`config_gamma`, `isp_apply_settings`, `isp_handle_stats`, …),
   `isp_module_cfg.o` (`isp_hardware_update`, `isp_map_addr`) and, transitively,
   `isp521_reg_cfg.o` (the register writers, now clean-roomed as `isp_reg_*`)
   from `libisp_algo_rtos.a`. These are the
   framework register/config writers, not 3A algorithms. Only the six algorithm
   members `isp_3a_ae.o`, `isp_3a_afs.o`, `isp_3a_awb.o`, `isp_iso_config.o`,
   `isp_pltm.o`, `isp_tone_mapping.o` are replaced.
2. **Those six members are referenced only for their 12 public entry points**
   (`{iso,ae,awb,afs,gtm,pltm}_{init,exit}`) — verified from the defined and
   undefined symbol sets of every object in `build-rtos-v/`. The clean shims export exactly
   those 12 names, so with the shim objects linked ahead of the archive the six
   members are never pulled. `isp_3a_af.o` / `isp_motion_detect.o` /
   `isp_rolloff.o` are unreferenced (`ISP_LIB_USE_AF/MD/ROLLOFF = 0`).
3. **`freeisp_get_tables` is defined by five shim objects** (ISO, AE, AWB, GTM,
   PLTM; each clean core calls it, each shim defines it). Linking more than one
   reproduces a `multiple definition` error. It must be renamed per module on
   *both* the clean core and its shim object. AFS uses a different provider
   (`afs_clean_set_trig_provider` / `afs_shim_set_trig_tables`) and needs no
   rename. This is an integration-only requirement: the per-module Makefiles
   build standalone and do not perform it.
4. **ABI**: the shims must be built with the same `-fshort-enums
   -DISP521_RTOS_ALGO=1` ABI as the Melis archive and the `RTOS_ENUM_SRC`
   framework objects, plus `-fwrapv` and `gnu99` to match the modules' own
   builds.
5. ISO is the pilot and its shim lives at the shim **root**
   (`shim/iso_shim.c`); the other five are `shim/<mod>/<mod>_shim.c`.
6. The integration layer (`shim/integration/freeisp_shim_tables.c`) reuses the
   freewinner locator (`src/tables/rmm_tables.c`, AGPL-3.0-only); the located
   tables are fed in at runtime rather than compiled into it. See
   `integration/README.md` for the cache-bundle decoupling and
   `docs/provenance.md` for the full picture.

---

## (a) Build and link changes — `work/media_daemon/Makefile`

### A1. Add the clean-room roots (after `CEDARC := $(LIB)/libcedarc`, line 32)

```diff
--- a/work/media_daemon/Makefile
+++ b/work/media_daemon/Makefile
@@
 CEDARC := $(LIB)/libcedarc
+
+# Clean-room ISP algorithm shims (the freewinner project, a sibling checkout).
+# The six 3A modules are rebuilt from clean-room sources and linked ahead of the
+# vendor archive; the archive is retained only for its framework members
+# (isp_base.o / isp_module_cfg.o / isp521_reg_cfg.o).  See
+# shim/INTEGRATION.md.
+FREEISP   ?= /path/to/freewinner
+FW_GIT    ?= $(FREEISP)
+SHIM_DIR  := $(FREEISP)/shim
+CSRC_DIR  := $(FREEISP)/src
+CINC_DIR  := $(FREEISP)/include
+INTEG_DIR := $(SHIM_DIR)/integration
```

### A2. Source lists (after `SRC_INIPARSER := ...`, line 240)

```diff
@@
 SRC_INIPARSER := $(wildcard $(ISP)/iniparser/src/*.c)
+
+# Clean-room 3A shims: one clean core + one SDK-boundary shim per module, plus
+# the integration layer that feeds the camera's own rmm tables in.  ISO was the
+# pilot and lives at the shim root; the other five are shim/<mod>/<mod>_shim.c.
+CLEAN_CORE_SRC := \
+    $(CSRC_DIR)/iso/iso_clean.c \
+    $(CSRC_DIR)/ae/ae_clean.c \
+    $(CSRC_DIR)/awb/awb_clean.c \
+    $(CSRC_DIR)/afs/afs_clean.c \
+    $(CSRC_DIR)/gtm/gtm_clean.c \
+    $(CSRC_DIR)/pltm/pltm_clean.c
+CLEAN_SHIM_SRC := \
+    $(SHIM_DIR)/iso_shim.c \
+    $(SHIM_DIR)/ae/ae_shim.c \
+    $(SHIM_DIR)/awb/awb_shim.c \
+    $(SHIM_DIR)/afs/afs_shim.c \
+    $(SHIM_DIR)/gtm/gtm_shim.c \
+    $(SHIM_DIR)/pltm/pltm_shim.c
+CLEAN_INTEG_SRC := \
+    $(INTEG_DIR)/freeisp_shim_tables.c \
+    $(FW_GIT)/src/tables/rmm_tables.c
```

### A3. Include them in the build (after `ALL_CXX_SRC := ...`, line 256)

```diff
@@
 ALL_CXX_SRC := $(SRC_MEDIA_UTILS_CXX)
+
+# The clean shims speak the RTOS-521 short-enum ABI; build them only for the
+# ALGO_RTOS deploy target (the default target links the non-RTOS libisp-*/out
+# prebuilts and keeps the vendor objects).
+ifeq ($(ALGO_RTOS),1)
+ALL_C_SRC += $(CLEAN_CORE_SRC) $(CLEAN_SHIM_SRC) $(CLEAN_INTEG_SRC)
+endif
```

`ALL_C_SRC` feeds both `vpath` (line 286) and `OBJS` (line 258), so the
pattern rule builds them and they are linked as explicit objects **before**
`LDFLAGS`/the archive.

### A4. Per-object flags (after the `RTOS_ENUM_OBJS` block, line 284)

```diff
@@
 $(RTOS_ENUM_OBJS): CFLAGS += -fshort-enums -DISP521_RTOS_ALGO=1
 endif
+
+ifeq ($(ALGO_RTOS),1)
+# Same short-enum ISP521 ABI as the Melis archive and the framework objects they
+# share isp_lib_context with; -fwrapv/gnu99 mirror the modules' own Makefiles.
+CLEAN_CORE_OBJS  := $(addprefix $(BUILD)/,$(notdir $(patsubst %.c,%.o,$(CLEAN_CORE_SRC))))
+CLEAN_SHIM_OBJS  := $(addprefix $(BUILD)/,$(notdir $(patsubst %.c,%.o,$(CLEAN_SHIM_SRC))))
+CLEAN_INTEG_OBJS := $(addprefix $(BUILD)/,$(notdir $(patsubst %.c,%.o,$(CLEAN_INTEG_SRC))))
+CLEAN_INC_FLAGS  := -I$(CINC_DIR) -I$(INTEG_DIR) -I$(FW_GIT)/include -I$(FW_GIT)/src
+$(CLEAN_CORE_OBJS) $(CLEAN_SHIM_OBJS) $(CLEAN_INTEG_OBJS): CFLAGS += \
+    $(CLEAN_INC_FLAGS) -fshort-enums -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 \
+    -fwrapv -std=gnu99
+
+# Clean cores: rename the public entry points so the shim can export the SDK
+# names (mirrors each module Makefile's CLEAN_DEFS).
+$(BUILD)/iso_clean.o:  CFLAGS += -Diso_init=clean_iso_init -Diso_exit=clean_iso_exit -Diso_get_params=clean_iso_get_params -Diso_set_params=clean_iso_set_params -Diso_run=clean_iso_run
+$(BUILD)/ae_clean.o:   CFLAGS += -Dae_init=clean_ae_init -Dae_exit=clean_ae_exit -Dae_get_params=clean_ae_get_params -Dae_set_params=clean_ae_set_params -Dae_run=clean_ae_run -Dae_isr=clean_ae_isr -Dae_stats_t=clean_ae_stats_t -Dae_result_t=clean_ae_result_t
+$(BUILD)/awb_clean.o:  CFLAGS += -Dawb_init=clean_awb_init -Dawb_exit=clean_awb_exit -Dawb_get_params=clean_awb_get_params -Dawb_set_params=clean_awb_set_params -Dawb_run=clean_awb_run -Dawb_isr=clean_awb_isr
+$(BUILD)/afs_clean.o:  CFLAGS += -Dafs_clean_init=clean_afs_init -Dafs_clean_exit=clean_afs_exit -Dafs_clean_get_params=clean_afs_get_params -Dafs_clean_set_params=clean_afs_set_params -Dafs_clean_run=clean_afs_run -Dafs_clean_isr=clean_afs_isr
+$(BUILD)/gtm_clean.o:  CFLAGS += -Dgtm_init=clean_gtm_init -Dgtm_exit=clean_gtm_exit -Dgtm_get_params=clean_gtm_get_params -Dgtm_set_params=clean_gtm_set_params -Dgtm_run=clean_gtm_run
+$(BUILD)/pltm_clean.o: CFLAGS += -Dpltm_init=clean_pltm_init -Dpltm_exit=clean_pltm_exit -Dpltm_get_params=clean_pltm_get_params -Dpltm_set_params=clean_pltm_set_params -Dpltm_run=clean_pltm_run -Dpltm_set_default_result=clean_pltm_set_default_result
+
+# Five shims each define freeisp_get_tables() (their clean core calls it); only
+# one may exist per process, so rename it per module on BOTH the clean core and
+# its shim.  Without this the link fails with multiple definitions.
+$(BUILD)/iso_clean.o  $(BUILD)/iso_shim.o:  CFLAGS +=
+$(BUILD)/ae_clean.o   $(BUILD)/ae_shim.o:   CFLAGS +=
+$(BUILD)/awb_clean.o  $(BUILD)/awb_shim.o:  CFLAGS +=
+$(BUILD)/gtm_clean.o  $(BUILD)/gtm_shim.o:  CFLAGS +=
+$(BUILD)/pltm_clean.o $(BUILD)/pltm_shim.o: CFLAGS +=
+endif
```

### A5. Archive link selection (lines 303–307 + a new generated archive)

Recommended form — generate a framework-only archive from the untouched vendor
archive (`ar d`), so the six replaced members can never be pulled:

```diff
@@
 ifeq ($(ALGO_RTOS),1)
-ISP_ALGO_LIBS := $(MD)/prebuilt/libisp_algo_rtos.a
+# The six 3A modules are now the clean-room shims (explicit objects, linked
+# before this archive).  Drop the six replaced members so they cannot be pulled
+# in and duplicate the shim entry points; keep the framework members.
+ISP_ALGO_RTOS_ARCHIVE   := $(MD)/prebuilt/libisp_algo_rtos.a
+ISP_ALGO_RTOS_FRAMEWORK := $(BUILD)/libisp_algo_rtos_framework.a
+ISP_ALGO_RTOS_DROP      := isp_3a_ae.o isp_3a_afs.o isp_3a_awb.o \
+                           isp_iso_config.o isp_pltm.o isp_tone_mapping.o
+ISP_ALGO_LIBS := $(ISP_ALGO_RTOS_FRAMEWORK)
 else
 ISP_ALGO_LIBS := -lisp_ae -lisp_af -lisp_afs -lisp_awb -lisp_base -lisp_gtm -lisp_iso -lisp_md -lisp_pltm -lisp_rolloff
 endif
+
+ifeq ($(ALGO_RTOS),1)
+# Generated from the untouched prebuilt archive; make clean removes it.
+$(ISP_ALGO_RTOS_FRAMEWORK): $(ISP_ALGO_RTOS_ARCHIVE) | $(BUILD)
+	cp $< $@
+	$(AR) d $@ $(ISP_ALGO_RTOS_DROP)
+endif
```

And make the link wait for the generated archive (after the `$(TARGET)` rule,
line 354):

```diff
@@
 $(TARGET): $(OBJS) $(RMM_LAYOUT_H)
 	$(CXX) $(OBJS) $(LDFLAGS) -o $@
+
+# ALGO_RTOS: the framework archive is a generated artifact, not in OBJS.
+ifeq ($(ALGO_RTOS),1)
+$(TARGET): $(ISP_ALGO_RTOS_FRAMEWORK)
+endif
```

**Link ordering (why it is correct).** The rule is
`$(CXX) $(OBJS) $(LDFLAGS)`, so the clean shim objects (in `OBJS`) precede the
archive inside `-Wl,--start-group`. Explicit objects are always included; an
archive member is only extracted for a still-undefined symbol. The 12 public
entries are already defined by the shims, so the six members are not extracted;
`isp_base.o`/`isp_module_cfg.o`/`isp521_reg_cfg.o` are (their symbols are
undefined). Renaming `freeisp_get_tables` removes the only inter-shim link
collision.

**Minimal alternative** (one-line-text change, also validated): leave
`ISP_ALGO_LIBS := $(MD)/prebuilt/libisp_algo_rtos.a` exactly as it is, do not
add the generated archive or the extra `$(TARGET)` prerequisite. Object ordering
alone keeps the six members out. The generated-archive form is preferred because
it is robust against future reordering; the archive-only form is a smaller diff.

### A6. Let `main.c` see the integration header (in `INC`, ~line 126)

```diff
@@
     -I$(ISP)/isp_cfg \
+    -I$(INTEG_DIR) \
     -I$(LIB)/libISE/include \
```

### A7. Trimmed vendor archive — needed vs dead members

With the six clean shims linked ahead, intersecting the undefined symbol set of
every `build-rtos-v/*.o` with the archive members' defined set gives the exact
reference set (verified 2026-09-15):

| Member | Referenced? | Referenced symbols |
| --- | --- | --- |
| `isp_base.o` | yes | 12 entry points: `config_gamma`, `config_band_step`, `config_dig_gain`, `config_lens_center`, `config_lens_table`, `config_msc_table`, `config_wdr`, `isp_apply_colormatrix`, `isp_apply_settings`, `isp_handle_stats`, `isp_handle_stats_sync`, `__isp_stat_dynamic_judge` |
| `isp_module_cfg.o` | yes | `isp_hardware_update`, `isp_map_addr`; its 31-entry `module_attrs[]` dispatch additionally pulls the 31 `isp_reg_prepare_*` routines plus their 29 paired `isp_reg_enable_*` |
| `isp521_reg_cfg.o` | transitively | 67 `isp_reg_set_*`/`isp_reg_*` register writers **reached from the two members above** (a vendor-reach figure, not the object's export count — the object exports 69 routines) |
| `isp_3a_af.o` | no | dead stub (`af_init`/`af_exit`), referenced 0 times |
| `isp_motion_detect.o` | no | dead stub (`md_init`/`md_exit`), referenced 0 times |
| `isp_rolloff.o` | no | dead stub (`rolloff_init`/`rolloff_exit`), referenced 0 times |

So the archive can be trimmed further than A5 does — the three dead stubs can be
dropped as well (`ISP_ALGO_RTOS_DROP` gains `isp_3a_af.o isp_motion_detect.o
isp_rolloff.o`), or by direct edit:

    ar d libisp_algo_rtos.a isp_3a_af.o isp_motion_detect.o isp_rolloff.o

equivalently, build the framework-only archive with the three dead stubs *and*
the six replaced members removed. The result still needs `isp_base.o`,
`isp_module_cfg.o` and `isp521_reg_cfg.o` until the register/config layer is
clean-roomed as well; the archive as a whole cannot be dropped yet. This
subsection is proposal text only — no `yi-mediad` file is changed by it.

**Superseded by A8** for the `isp_module_cfg.o` / `isp521_reg_cfg.o` members:
that layer is now clean-roomed too.

### A8. Clean-room register/hardware tier now replaces two more archive members

The register-writer tier and the per-frame module-dispatch tier are now
clean-roomed as well:

| New source | Role | Exports |
| --- | --- | --- |
| `src/reg/reg_writers.c` | register/hardware writers | the 69 `isp_reg_*` writers (`isp_reg_map_load_addr` counted among them) + the clean-room helper `isp_reg_set_dg_bypass` (70 routines in the file; the vendor object exports the 69 writers) |
| `src/reg/module_cfg.c` | module dispatch + payload builder | `isp_hardware_update`, `isp_map_addr`, the 31 `isp_reg_prepare_*` plus the 29 paired `isp_reg_enable_*`, and the 31-entry `isp_module_attrs[]` dispatch |
| `include/reg_writers.h`, `include/module_cfg.h` | headers | — |

Both translation units are built with the same device ABI flags already used for
the 3A shims (`-fshort-enums -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 -fwrapv
-std=gnu99`) and linked ahead of the vendor archive, so the framework resolves
the clean `isp_hardware_update`/`isp_map_addr`, which in turn call the clean
`isp_reg_*` writers; the vendor `isp_module_cfg.o` and `isp521_reg_cfg.o`
are never extracted.

Differential-verified against the deployed vendor objects (2026-09-15):

```
the private register differential  -> the withheld golden
    reg summary: 680 cases (68 writers + `isp_reg_map_load_addr`, i.e. all 69 writers), 0 with mismatches, 0/68 writers mismatch
    reg sensitivity: detected
the private module_cfg differential -> the withheld golden
    reg_cfg summary: 1481 cases, 0 mismatches, 0/70 routines mismatch, 0 payload diffs
    reg_cfg sensitivity: detected (gca input perturb -> 1 reg words)
```

Add the two sources alongside the 3A shims (extends A2), under the same
`ALGO_RTOS` gate:

```diff
@@
 CLEAN_INTEG_SRC := \
     $(INTEG_DIR)/freeisp_shim_tables.c \
     $(FW_GIT)/src/tables/rmm_tables.c
+
+# Register/hardware tier: the isp_reg_* writers and the module_cfg
+# dispatch (isp_hardware_update/isp_map_addr + the 31 isp_reg_prepare_* / 29 isp_reg_enable_*).
+CLEAN_REG_SRC := \
+    $(CSRC_DIR)/reg/reg_writers.c \
+    $(CSRC_DIR)/reg/module_cfg.c
```

```diff
@@
 ifeq ($(ALGO_RTOS),1)
-ALL_C_SRC += $(CLEAN_CORE_SRC) $(CLEAN_SHIM_SRC) $(CLEAN_INTEG_SRC)
+ALL_C_SRC += $(CLEAN_CORE_SRC) $(CLEAN_SHIM_SRC) $(CLEAN_INTEG_SRC) $(CLEAN_REG_SRC)
 endif
```

Per-object flags (extends A4). This stage needs no per-object `-D` renames:
`module_cfg.c` keeps the KEEP `isp_hardware_update`/`isp_map_addr` entry-point
spellings, and `reg_writers.c` exports the renamed `isp_reg_*` writers — neither
collides with the vendor archive:

```diff
@@
 $(CLEAN_CORE_OBJS) $(CLEAN_SHIM_OBJS) $(CLEAN_INTEG_OBJS): CFLAGS += \
     $(CLEAN_INC_FLAGS) -fshort-enums -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 \
     -fwrapv -std=gnu99
+
+CLEAN_REG_OBJS := $(addprefix $(BUILD)/,$(notdir $(patsubst %.c,%.o,$(CLEAN_REG_SRC))))
+$(CLEAN_REG_OBJS): CFLAGS += $(CLEAN_INC_FLAGS) -fshort-enums \
+    -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 -fwrapv -std=gnu99
```

**Updated archive accounting (supersedes the "still needed" line of A7).** With
both clean tiers linked ahead, the archive no longer needs `isp_module_cfg.o`
*or* `isp521_reg_cfg.o`:

| Member | Still needed? | Why |
| --- | --- | --- |
| `isp_base.o` | no | replaced by the clean `src/reg/base.c` + `comp_ref.c` through the SDK-ABI shim `shim/base/base_shim.c` (A10); the base tier was re-produced from the behaviour-only spec and is differential-green (see A10 below and `../docs/provenance.md:29-37`) |
| `isp_module_cfg.o` | no | replaced by `src/reg/module_cfg.c` **through the SDK-ABI shim** `shim/module_cfg/module_cfg_shim.c` (A9) |
| `isp521_reg_cfg.o` | no | replaced by `src/reg/reg_writers.c` |
| `isp_3a_af.o`, `isp_motion_detect.o`, `isp_rolloff.o` | no | dead stubs, referenced 0 times (A7) |

Updated `ar d` drop list — A5's `ISP_ALGO_RTOS_DROP` widened by the two members
at this A8 stage (A10 widens it further with `isp_base.o`):

```diff
@@
 ISP_ALGO_RTOS_DROP      := isp_3a_ae.o isp_3a_afs.o isp_3a_awb.o \
                            isp_iso_config.o isp_pltm.o isp_tone_mapping.o
+ISP_ALGO_RTOS_DROP      += isp_module_cfg.o isp521_reg_cfg.o
```

The resulting framework-only archive at this A8 stage holds `isp_base.o` plus the
three dead stubs:

    isp_base.o  isp_3a_af.o  isp_motion_detect.o  isp_rolloff.o

(A10 then drops `isp_base.o` as well; A7 optionally drops the three stubs, so the
final archive carries none of these.)

**Runtime coupling / link-order requirement to verify.** The clean
`isp_hardware_update`/`isp_map_addr` are the framework entry points (called by
`isp_manage`), and the clean `isp_reg_*` writers are their
implementations. It is the **vendor `isp_module_cfg.o`** (not `isp_base.o`) that
carries the undefined writer references; those must resolve to the
clean `reg_writers.o`, not to `isp521_reg_cfg.o`. `isp_base.o`'s own undefined
set is the SDK's matrix helpers (`create_matrix`, `multiply_matrices`,
`add_matrices`, `destroy_matrix`), the SDK's interpolation helper
(`isp_interp_s32`), the libm functions `cos`/`sin`/`pow`/`sqrt`, and the two
SDK-side helpers `isp_ae_set_params_helper` and `isp_lib_log_param` (defined in
`build-rtos-v/*.o`) — none are `isp_reg_*`.

Verification performed 2026-09-15 (the private A8 link-validation harness),
relinking the already-built `build-rtos-v/*.o` set with the 16 clean objects
ahead of the archive (partial `ld -r`, both archive forms):

- **PASS** — with the framework-only archive (the A8 `ar d` drop list) and with
  the **full** vendor archive, the result has no undefined `isp_reg_*` writers.
- **PASS** — `ld --trace-symbol` attribution in the reduced link:
  `isp_hardware_update`/`isp_map_addr` → `module_cfg.o`,
  `isp_reg_set_af_en`/`isp_reg_map_load_addr` → `reg_writers.o`,
  `iso_init` → `iso_shim.o`.
- **PASS** — in the full-archive link, explicit-object ordering alone kept every
  one of the eight replaced members out: **only `isp_base.o` was extracted**
  (`isp_3a_af.o`/`isp_motion_detect.o`/`isp_rolloff.o` are unreferenced). So the
  `ar d` step is *not* strictly required for correctness; it is retained only as
  robustness against future link reordering.

This subsection is proposal text only — no `yi-mediad` file is changed by it.

### A9. `module_cfg` SDK-ABI shim — fixes the black-video ABI mismatch

The clean `src/reg/module_cfg.c` operates on `include/module_cfg.h`'s
private `fwi_mod_config_t` (**67204 B = 0x10684 on the 32-bit ARM target**;
payload tables embedded as arrays — a 64-bit host compiler sees 67224 B = 0x10698,
the same struct with an `unsigned long`-sized lead member and a trailing `void *`
widened, not a different ABI). The
framework calls `isp_hardware_update(&isp_gen->module_cfg)` /
`isp_map_addr(&isp_gen->module_cfg, …)` with the real SDK `struct isp_module_config`
(**37040 B on the 32-bit target**; the payload tables are `void *`, so a 64-bit host
sees 37112 B — the same struct at wider pointers, not a different ABI). Handing the
SDK pointer to the clean TU reinterprets every field and corrupts the per-frame ISP
programming → **black video**. 37040 B is compile-asserted on the 32-bit target
(`shim/module_cfg/module_cfg_shim.c`) and matches the deployed object's layout.

Fix: a thin SDK-ABI adapter, `shim/module_cfg/module_cfg_shim.c`, is linked
with the clean TU. It exports `isp_hardware_update`/`isp_map_addr` in the SDK
signatures, translates the SDK struct ↔ the clean struct (scalars/sub-structs field by
field; embedded payload tables to/from the SDK pointer targets; `mode_cfg.d3d_mode`
from `tdf_cfg.k3d_increase_mode`; `satu_src`/`msc_table` bound to the SDK pointers),
calls the renamed clean dispatch, and writes the produced table buffers back.
`reg_writers.c` is unchanged.

Also fixed in `module_cfg.c`: `isp_reg_prepare_linear` copied the wrong way
(`linear_src ← fe_table`); the deployed direction is source = SDK `linear_table`
(clean `linear_src`), destination = SDK `fe_table` (clean `fe_table`). The old `reg_cfg`
differential missed it because its harness aliased both vendor pointers to one buffer.

Extend A8's `CLEAN_REG_SRC` with the shim. The clean `module_cfg.o` still defines
the two SDK entry points (`isp_hardware_update`/`isp_map_addr`, KEEP spellings), so
the shim renames only those two on the clean object and itself exports them under
the SDK names:

```diff
@@
 CLEAN_REG_SRC := \
     $(CSRC_DIR)/reg/reg_writers.c \
     $(CSRC_DIR)/reg/module_cfg.c
+CLEAN_REG_SRC += $(SHIM_DIR)/module_cfg/module_cfg_shim.c
@@
 $(CLEAN_REG_OBJS): CFLAGS += $(CLEAN_INC_FLAGS) -fshort-enums \
     -DISP521_RTOS_ALGO=1 -DISP_VERSION=521 -fwrapv -std=gnu99
+
+# The clean module_cfg.o must not export the SDK entry points; the shim does.
+# (Mirrors shim/module_cfg/Makefile's CLEAN_DEFS.)
+$(BUILD)/module_cfg.o: CFLAGS += -Disp_hardware_update=clean_isp_hardware_update \
+    -Disp_map_addr=clean_isp_map_addr
```

`shim/module_cfg/Makefile` is the authority for this list: its `CLEAN_DEFS`
(`grep -n CLEAN_DEFS shim/module_cfg/Makefile`) holds exactly those two renames
and nothing else. After the rename the clean tier's per-module routines
(`isp_reg_prepare_*`, 31, and `isp_reg_enable_*`, 29) do not collide with the SDK,
so they need no `-D` rename. (The counts were measured 2026-09-20 with
`grep -oE 'isp_reg_prepare_[a-z0-9_]+' isp/src/reg/module_cfg.c | sort -u | wc -l`
→ 31 and the same for `isp_reg_enable_` → 29, with identical counts in
`isp/include/module_cfg.h`; the earlier "31 + 30" figure was wrong.) Earlier drafts
of this section proposed a per-module `-D` flag list for those routines; no
Makefile ever implemented it, and it is dropped.

Verified: the private SDK-ABI module_cfg harness links the deployed
`isp_module_cfg.o` next to the clean tier + shim, drives both with the real SDK
`struct isp_module_config` (distinct, separately-sentineled `linear_table`/`fe_table`
buffers), and compares the register image, every pointer-target buffer, the embedded
outputs and `table_update`: **540 cases, 0 mismatches, 0 pointer-buffer diffs**;
sensitivity detected. Reverting the `isp_reg_prepare_linear` fix makes it fail (76720
pointer-buffer diffs) — the harness catches the bug the old `reg_cfg` missed.

### A10. `base` SDK-ABI shim — replaces the last vendor object (`isp_base.o`)

The clean `src/reg/base.c` + `comp_ref.c` are differential-green against
`isp_base.o` but use a private `fwi_base_ctx_t`; the framework calls the 12
`isp_base.h` entry points with the real SDK `struct isp_lib_context` (0x42cc0 on the
32-bit target; a 64-bit host sees 0x42e00 for the same struct), while the clean tier
expects its own `fwi_base_ctx_t` (0x218a0 = 137376 B on the 32-bit target;
0x218c8 = 137416 B on a 64-bit host — same struct, wider pointers). `shim/base/base_shim.c` is the
adapter, mirroring A9:
it exports the SDK entry points, translates the SDK context → clean context, calls
the renamed clean entry points, and copies the produced `module_cfg`/context fields
back. It installs a `base_tables_t` provider from the runtime tuning plus the
locator's per-temperature defaults.

Build it alongside the clean base (extends A8/A9), and drop `isp_base.o`:

```diff
@@
 CLEAN_REG_SRC := \
     $(CSRC_DIR)/reg/reg_writers.c \
     $(CSRC_DIR)/reg/module_cfg.c
+CLEAN_REG_SRC += $(CSRC_DIR)/reg/base.c $(CSRC_DIR)/reg/comp_ref.c \
+                 $(SHIM_DIR)/base/base_shim.c
@@
 # base.c's 12 entry points are exported by base_shim.c in SDK signatures.
+$(BUILD)/base.o: CFLAGS += -Dconfig_band_step=clean_config_band_step \
+    -Dconfig_lens_center=clean_config_lens_center -Dconfig_dig_gain=clean_config_dig_gain \
+    -Dconfig_gamma=clean_config_gamma -Dconfig_wdr=clean_config_wdr \
+    -Disp_apply_colormatrix=clean_isp_apply_colormatrix \
+    -D__isp_stat_dynamic_judge=clean___isp_stat_dynamic_judge \
+    -Disp_handle_stats=clean_isp_handle_stats \
+    -Disp_handle_stats_sync=clean_isp_handle_stats_sync \
+    -Disp_apply_settings=clean_isp_apply_settings \
+    -Dconfig_lens_table=clean_config_lens_table -Dconfig_msc_table=clean_config_msc_table
```

```diff
@@
-ISP_ALGO_RTOS_DROP      := … $(if $(filter 1,$(VENDOR_REG)),,isp_module_cfg.o isp521_reg_cfg.o) \
+ISP_ALGO_RTOS_DROP      := … $(if $(filter 1,$(VENDOR_REG)),,isp_module_cfg.o isp521_reg_cfg.o isp_base.o) \
```

**Runtime hook (the integration feed):** in `shim/integration/freeisp_shim_tables.c`,
once the located `freeisp_tables_t *v` is populated, call
`base_shim_set_locator(v->anti_gamma_table, v->rgb2yuv_matrix, v->lsc_trig_cfg_def,
v->msc_trig_cfg_def, v->isp_cm_color_temp);` (include `base/base_shim.h`, link
`base_shim.o`) before the first `isp_base` entry point runs.

Verified: the private SDK-ABI base harness links the deployed `isp_base.o`
next to the clean `base.o` + shim, drives both with the real SDK `struct
isp_lib_context` (113 cases) and compares the register image + `module_cfg` payloads
+ context write-backs: **113 cases, 0 mismatches**, sensitivity detected. The 12
entry points' signatures compile-check against the real `isp_base.h`; `config_blc`
is a documented no-op because the deployed object does not define it and the
framework never calls it.

**AE helper seam (fixed 2026-09-18).** The deployed `isp_apply_settings` calls the
framework `isp_ae_set_params_helper` (defined in `isp_manage.o`, T), whose SDK
`isp_ae_entity_context` layout is `{ae_param* at +0, ae_stats at +4, ae_result at +8,
ops* at +0x1c8, ae_entity* at +0x1cc}` (464 bytes) and which does
`if (!ae_entity||!ops||!ae_param) return; ae_param->type=cmd;
ops->isp_ae_set_params(ae_entity, ae_param, &ae_result);`. The clean context has
none of `ops`/`ae_entity`, so `base.c` cannot call that helper with its own
context (the A10 on-camera hang: the helper walked the mismatched layout).

Resolution: `base.c` now calls the new hook `freeisp_ae_set_params(&ctx->ae_entity_ctx,
cmd)` (declared in `base.h`), which the shim implements. `freeisp_ae_set_params`
takes the current SDK context (the shim records it around `isp_apply_settings`),
translates the clean parameter update onto the SDK `ae_param`
(`cmd==ISP_AE_UPDATE_AE_TABLE`: copy `ae_ini` gain ranges; `cmd==
ISP_AE_BUILD_TOUCH_WEIGHT`: `memcpy(ae_setting, &s->ae_settings, 0xd0)`), then
calls the real `isp_ae_set_params_helper` with the **real SDK entity context** —
so the symbol is no longer imported by our objects and drops from the vendor
archive. `config_wdr`/`config_lens_table`/`config_msc_table` read
`ae_entity_ctx.ae_param->nor_cmd_mode`, so the shim points the clean handle at the
clean embedded `ae_param` (previously it pointed at the SDK block, which the clean
tier then read with the wrong layout). The base_sdk harness now models the real
helper's layout/dispatch (null-check `ae_entity`/`ops`/`ae_param`, `type=cmd`,
`ops->isp_ae_set_params` callback that flags a mismatched entity context), drives
it with the full entity context (`ops` + `ae_entity` set) and records the callback
sequence; it observes cmd 1 (×12) and cmd 3 (×2) with no bad dispatch.

Known gap (documented, not hidden): `base_tables_t.gamma_trig`'s all-zero fallback —
the deployed `config_gamma` uses a compiled-in default when `gamma_trig_cfg[0]==0`,
and there is no located default for it in the 36-table set, so the clean provider
falls back to the (zero) tuning array. Non-zero tuning is unaffected. Supply a
located default or accept the divergence.

### Sources and flags checklist (every file — 20 objects: 6 clean cores, 6 SDK shims, 2 integration/locator, 6 register-tier)

| Source | Role | Module-specific flags |
| --- | --- | --- |
| `src/iso/iso_clean.c` | clean core | `-Diso_init=clean_iso_init -Diso_exit=clean_iso_exit -Diso_get_params=clean_iso_get_params -Diso_set_params=clean_iso_set_params -Diso_run=clean_iso_run` |
| `shim/iso_shim.c` | SDK shim | — |
| `src/ae/ae_clean.c` | clean core | `-Dae_init=clean_ae_init -Dae_exit=clean_ae_exit -Dae_get_params=clean_ae_get_params -Dae_set_params=clean_ae_set_params -Dae_run=clean_ae_run -Dae_isr=clean_ae_isr -Dae_stats_t=clean_ae_stats_t -Dae_result_t=clean_ae_result_t` |
| `shim/ae/ae_shim.c` | SDK shim | — |
| `src/awb/awb_clean.c` | clean core | `-Dawb_init=clean_awb_init -Dawb_exit=clean_awb_exit -Dawb_get_params=clean_awb_get_params -Dawb_set_params=clean_awb_set_params -Dawb_run=clean_awb_run -Dawb_isr=clean_awb_isr` |
| `shim/awb/awb_shim.c` | SDK shim | — |
| `src/afs/afs_clean.c` | clean core | `-Dafs_clean_init=clean_afs_init -Dafs_clean_exit=clean_afs_exit -Dafs_clean_get_params=clean_afs_get_params -Dafs_clean_set_params=clean_afs_set_params -Dafs_clean_run=clean_afs_run -Dafs_clean_isr=clean_afs_isr` |
| `shim/afs/afs_shim.c` | SDK shim | — |
| `src/gtm/gtm_clean.c` | clean core | `-Dgtm_init=clean_gtm_init -Dgtm_exit=clean_gtm_exit -Dgtm_get_params=clean_gtm_get_params -Dgtm_set_params=clean_gtm_set_params -Dgtm_run=clean_gtm_run` |
| `shim/gtm/gtm_shim.c` | SDK shim | — |
| `src/pltm/pltm_clean.c` | clean core | `-Dpltm_init=clean_pltm_init -Dpltm_exit=clean_pltm_exit -Dpltm_get_params=clean_pltm_get_params -Dpltm_set_params=clean_pltm_set_params -Dpltm_run=clean_pltm_run -Dpltm_set_default_result=clean_pltm_set_default_result` |
| `shim/pltm/pltm_shim.c` | SDK shim | — |
| `shim/integration/freeisp_shim_tables.c` | rmm→shim table feed | — |
| `src/tables/rmm_tables.c` | table locator (AGPL-3.0-only) | — |
| `src/reg/reg_writers.c` | register writers (A8) | — |
| `src/reg/module_cfg.c` | module dispatch (A8) | `-Disp_hardware_update=clean_isp_hardware_update -Disp_map_addr=clean_isp_map_addr` (A9; `shim/module_cfg/Makefile`'s `CLEAN_DEFS` renames only the two KEEP entry points — the 31 `isp_reg_prepare_*`/29 `isp_reg_enable_*` routines need no rename) |
| `shim/module_cfg/module_cfg_shim.c` | SDK-ABI adapter for the module_cfg tier (A9) | — (exports the SDK entry points) |
| `src/reg/base.c` | base tier (A10) | the 12 `-Dconfig_*=clean_config_*` / `-Disp_apply_*=clean_isp_apply_*` / `-D__isp_stat_dynamic_judge=…` / `-Disp_handle_stats*=…` renames in A10, plus `-Dfreeisp_ae_set_params` hook |
| `src/reg/comp_ref.c` | radial-distance reference table for the MSC builders (A10) | — |
| `shim/base/base_shim.c` | SDK-ABI adapter for the base tier (A10) | — (exports the 12 SDK entry points; provides `freeisp_ae_set_params`/`base_shim_set_locator`) |

The consumer build carries per-module `-Dfreeisp_get_tables=clean_<mod>_get_tables`
renames on the five `*_clean.o`/`*_shim.o` object pairs (the yi-mediad
`work/media_daemon/Makefile`), so within that build each clean core and its shim
call a module-unique symbol. Independently, this tree resolves the collision in
the integration translation unit: each clean core *calls* `freeisp_get_tables()`
and each shim *defines* it, so the five per-module declarations would collide;
`shim/integration/freeisp_shim_tables.c` shadows each with
`#define freeisp_get_tables freeisp_shadow_<mod>_get` around the `#include` and
`#undef`s it afterwards, which is how the collision is resolved without a new
symbol name leaking from this tree.

Common flags for all 20 objects: `-I$(FREEISP)/include -I$(FREEISP)/shim/integration
-I$(FW_GIT)/include -I$(FW_GIT)/src -fshort-enums
-DISP521_RTOS_ALGO=1 -DISP_VERSION=521 -fwrapv -std=gnu99` (plus the SDK `INC`
already in `CFLAGS`). The register-tier sources need only the
`-I$(FREEISP)/include` subset of those include paths; A8 adds no `-D` rename, and
A9 renames the two clean `module_cfg.o` entry points only.

---

## (b) Startup wiring — feed `rmm` tables into the shim provider

Target: `work/media_daemon/main.c`. The shims latch their tables in
`clean_<mod>_init()`, which the framework calls from `isp_ctx_algo_init()`
(SDK `isp_manage`) inside `isp_init()` inside
`AW_MPI_ISP_Run()` (in `main.c`). The install therefore must happen before
`AW_MPI_ISP_Run()`.

### B1. Include (after `#include "rmm_tuning.h"`, line 55)

```diff
--- a/work/media_daemon/main.c
+++ b/work/media_daemon/main.c
@@
 #include "rmm_tuning.h"
+#include "freeisp_shim_tables.h"
```

### B2. Helper (after `load_vendor_profile()`, line 297)

```diff
@@
     return rmm_tuning_probe_wdr(rmm, g_sensor);
 }
+
+/* Install the camera's own libisp constant tables (AE/AWB/AFS/ISO/GTM/PLTM) into
+ * the clean-room shims.  The shims latch their table pointers in
+ * clean_<mod>_init(), called by isp_ctx_algo_init() during AW_MPI_ISP_Run(), so
+ * this must run first.
+ *
+ * Cache-first: a bundle written on a previous boot is installed as-is and the
+ * vendor `rmm` image is not read at all; only a missing/corrupt cache falls
+ * back to locating the tables in `rmm`, which then (re)writes the bundle for
+ * the next boot.  See shim/integration/README.md for the format.
+ *
+ * Failure is NOT fatal: the shims keep their built-in pilot defaults and the
+ * capture path still starts (degraded tuning).  MEDIAD_NO_RMM_TUNING disables
+ * the whole feed (cache included); MEDIAD_TABLE_BUNDLE overrides the cache path
+ * (empty string = never cache, pure locator). */
+static void load_shim_tables(void)
+{
+    const char *rmm, *bundle;
+
+    if (getenv("MEDIAD_NO_RMM_TUNING"))
+        return;
+    rmm = getenv("MEDIAD_RMM_PATH");
+    if (!rmm)
+        rmm = "/home/app/rmm";
+    bundle = getenv("MEDIAD_TABLE_BUNDLE");   /* NULL = FREEISP_TABLE_BUNDLE_PATH */
+    if (freeisp_shim_tables_from_rmm_or_cache(rmm, bundle) != 0)
+        fprintf(stderr, "mediad: clean-shim tables unavailable (bundle/rmm %s); "
+                        "using shim defaults\n", rmm);
+    else
+        fprintf(stderr, "mediad: clean-shim tables installed (cache-first)\n");
+}
```

### B3. Call site (after `apply_capabilities()`, line 964)

```diff
@@
     g_vendor_wdr = load_vendor_profile();
     if (g_vendor_wdr >= 0)
         fprintf(stderr, "mediad: vendor profile wdr=%d\n", g_vendor_wdr);
     apply_capabilities();
+    load_shim_tables();
     printf("mediad: %d channel(s)\n", nc);
```

Notes:

- Placement is before `AW_MPI_SYS_Init()` (979), the `vi_start()` loop (996) and
  `AW_MPI_ISP_Run()` (1001): safely ahead of `isp_ctx_algo_init()`.
- The call does not depend on `g_sensor`: the locator is sensor-agnostic (two
  anchors + an offsets-only layout). It does not read the vendor
  tuning; `rmm_tuning_load()` inside the overridden `parser_ini_info()` handles
  the separate `isp_ini_cfg` blob.
- **Failure handling**: the resolver is fail-closed — on a missing anchor,
  out-of-range slice, failed validator, bad bundle checksum or OOM it installs
  nothing and returns `-1`. `load_shim_tables()` logs and continues, leaving
  each shim's built-in pilot defaults in place. To fail the boot instead,
  change the branch to `return 1;` from `main()`; not recommended, since the
  camera should still produce video.
- On clean shutdown, `freeisp_shim_tables_free()` may be called before
  `AW_MPI_ISP_Exit()`; it is optional (the process is exiting).
- **The vendor `rmm` is needed only until the bundle exists.** The shims never
  embed the vendor tables. On the *first* boot with no bundle,
  `load_shim_tables()` locates them in the stock `rmm` image (present and
  untouched at `/home/app/rmm` on y623) and writes the bundle to
  `/tmp/sd/yi-protect/isp_cfg/freeisp_tables.bin` (the same directory yi-mediad
  already uses for its extracted vendor blobs). Every boot thereafter installs
  from the bundle and does not open `rmm`; a lost/corrupt bundle silently falls
  back to the locator and re-seeds. Today's running daemon is `mediad` (not
  `rmm`) and it still *contains* the tables because it links
  `prebuilt/libisp_algo_rtos.a` — once this patch replaces that archive it no
  longer does, which is exactly why the bundle exists. Do not point the locator
  at `mediad` itself once the shims are in — with the archive swapped out it
  would find nothing.

---

## (c) Rollback to the vendor archive

The vendor archive is never modified; `work/media_daemon/prebuilt/libisp_algo_rtos.a`
is untouched and `build.sh` will re-download it if missing. To return to the
vendor algorithms:

1. `git revert <integration-commit>` (or apply the reverse of the hunks above),
   which removes: the clean-room source lists/objects and per-object flags from
   the Makefile — the six-pair 3A tier (A2/A4) *and* the register tier
   `CLEAN_REG_SRC` / `CLEAN_REG_OBJS` (`reg_writers.c`, `module_cfg.c`, A8) —
   including the generated-archive rule and the A8 widening of
   `ISP_ALGO_RTOS_DROP` (`isp_module_cfg.o`, `isp521_reg_cfg.o`), and restores
   the A7/A8-dependent `isp_base.o` link behaviour. It also removes
   `-I$(INTEG_DIR)` from `INC`, and the `#include "freeisp_shim_tables.h"` +
   `load_shim_tables()` call from `main.c`. Reverting A5 restores
   `ISP_ALGO_LIBS := $(MD)/prebuilt/libisp_algo_rtos.a`.
2. `rm -f work/media_daemon/build-rtos-v/libisp_algo_rtos_framework.a` (or
   `make -C work/media_daemon BUILD=build-rtos-v TARGET=mediad_rtos_v clean`).
3. Rebuild the deploy target:
   `make -C work/media_daemon BUILD=build-rtos-v TARGET=mediad_rtos_v ALGO_RTOS=1`.
4. Runtime soft-rollback without rebuilding: `MEDIAD_NO_RMM_TUNING=1` in
   mediad's environment makes `load_shim_tables()` a no-op (the shims then run
   their pilot defaults); `ISP_TUNE_OFF=1` additionally disables the vendor
   `isp_ini_cfg` import. For a true vendor revert, use step 1.

---

## Validation status (explicit)

| Item | State | Evidence |
| --- | --- | --- |
| Combined ARM link (clean shims + integration + full vendor archive) | **Validated** | 14 objects cross-compiled, relinked over `build-rtos-v/*.o`; `LINK OK`; the link map shows no vendor-internal AWB run helpers, tuning-table symbols or ISO-auto-adjust symbols, `config_gamma`/`isp_hardware_update`/`isp_reg_set_af_en` present, `iso_init`/`ae_init`/… exported; no duplicate `freeisp_get_tables` |
| Combined ARM link, reduced framework archive (`ar d` form) | **Validated** | `LINK OK`, remaining members `isp521_reg_cfg.o isp_3a_af.o isp_base.o isp_module_cfg.o isp_motion_detect.o isp_rolloff.o` |
| Combined ARM link, **A8 register tier** (clean `reg_writers.c`+`module_cfg.c`; archive reduced by the eight replaced members) | **Validated** | the private A8 link-validation harness -> `RESULT: A8 LINK VALIDATION PASS`. Both reduced and full-archive forms link; no undefined `isp_reg_*` writers; scope attribution `module_cfg.o`/`reg_writers.o`; full-archive link extracts **only `isp_base.o`** |
| ISO shim SDK-boundary differential + full result write-back | **Validated** | the private ISO differential harness, 9 cases, 0 mismatches, sensitivity-checked (`shim/DESIGN.md`) |
| AWB shim SDK-boundary differential + result write-back | **Validated** | the private AWB differential harness, 18 cases, 0 mismatches, sensitivity-checked (withheld golden); six clean-core divergences fixed (`shim/awb/DESIGN.md`) |
| GTM shim SDK-boundary differential + result write-back | **Validated** | the private GTM differential harness, 28 cases, 0 mismatches, sensitivity-checked (withheld golden); two clean-core divergences fixed (`shim/gtm/DESIGN.md`) |
| Register-writer tier differential | **Validated** | the private register differential, 680 cases (68 writers + `isp_reg_map_load_addr`, i.e. all 69 writers), 0 mismatches, sensitivity detected (withheld golden) |
| module_cfg tier differential (layout-agnostic) | **Validated** | the private module_cfg differential, 1481 cases, 0 mismatches, 0/70 routines, 0 payload diffs, sensitivity detected (withheld golden) |
| module_cfg tier, **real SDK ABI** (shim, A9) | **Validated (host/qemu)** | the private SDK-ABI module_cfg harness, 540 cases, 0 mismatches, 0 pointer-buffer diffs (distinct `linear_table`/`fe_table`), sensitivity detected (withheld golden); catches the `isp_reg_prepare_linear` direction bug the layout-agnostic differential missed. No on-camera run yet. |
| AE / PLTM WDR path | **Validated at the SDK boundary (host/qemu); on-camera NOT run** | the private WDR-flow harness (clean AE + vendor `config_*`, real per-frame flow, 19 cases, 0 mismatches) found and fixed the missing `ae_result.ae_wdr_ratio` feedback (clean core kept a private result block); the private advanced-PLTM harness found and fixed the out-of-bounds strength/converge bank indices in the strength-curve build, the convergence decision and the speed-row walk, and the tile divisor. |
| AFS shim | **NOT SDK-differentially verified** | ABI-adapted; standalone host + ARM shim test passes; no SDK-boundary differential against the vendor object |
| `freeisp_shim_tables_from_rmm` (locator path) | **Validated host+qemu+on-device** | `test_shim_tables`: host 130 checks / 0 failures against the two stock `rmm` images (ARM+qemu 132 / 0 before the vendor-value aperture/TABLEDEF checks became structural ones); on-camera y623 `/home/app/rmm` (2243776 B, md5 `1738b853…`) → 65 checks / 0 failures. Note: reads the device's stock `rmm` **file** (the daemon actually running on y623 is `mediad`); see the table-source note in (b). |
| Cached bundle (`freeisp_shim_tables_save_cache` / `freeisp_shim_tables_from_cache` / `freeisp_shim_tables_from_rmm_or_cache`) | **Validated host+qemu (+ cross-arch)** | `test_table_bundle`: host 116 checks / 0 failures and ARM+qemu 116 / 0 against the same two images — bundle round-trip equals the located set, the missing/bad-magic/bad-version/bad-count/bad-layout-CRC/corrupt-payload/truncated/header-only/empty matrix is rejected fail-closed, and locator fallback re-seeds the cache. Cross-arch: a 64-bit-host-written bundle loads on 32-bit short-enum ARM under qemu-arm and is byte-identical to the ARM-written one. |
| On-camera run of the shim build | **NOT run** | link-only on this host; the on-device run above exercised the table-feed test binary, not a shim-built `mediad`. |
| Enum-ABI warnings (`uses variable-size enums … 32-bit enums`) | Pre-existing | identical to the current vendor-archive link |

Not yet validated: the five non-ISO clean cores' runtime behaviour against live
SDK stats, and the pass-through of their results into the framework. Treat a
camera deploy of this patch as a bring-up requiring the ISO module first, then
one non-ISO module at a time.

## Cross-project notes

- This makes `yi-mediad` depend on a sibling checkout of this repository
  (freewinner, AGPL-3.0-only). If that coupling
  is unwanted, `rmm_tables.c` /
  `tables_layout.h` and the six shim directories would need to be vendored into
  `yi-mediad` (or built into a static `libfreeisp_shim.a`).
- The `freeisp_get_tables` per-module rename is required only when the modules
  are co-linked; keep it in the integration build, not in the per-module
  Makefiles (standalone tests must keep the plain symbol).
