<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 freewinner contributors -->

# fisp_: what is deliberately not implemented

This package is a drop-in for the SDK's ISP API layer: it defines the 25
`AW_MPI_ISP_*` symbols the middleware links against and
nothing else (plus the two documented companions below). It owns the
`AW_MPI_ISP_*` symbols even where the vendor tree physically placed them in
`mpi_vi.c`; `fcap_` must not define any of them.

## Left out on purpose

| Area | Behaviour here |
| --- | --- |
| The 42 defined-but-unreferenced `AW_MPI_ISP_*` symbols | Not provided: generic config (`Set/GetAe`, `Set/GetModuleOnOff`, `SwitchIspCofig`, `SetSaveCTX`, `SetLocalExposureArea`), unused telemetry (`GetEvLvAdj`, `GetTemperature`), the unused AE/AWB setters/getters, and the picture/orientation getters and hue/scene/mirror/flip setters. Nothing in the deployed build references them. |
| Attribute-form setters taking `ISP_SHARPEN_ATTR_S *`, `ISP_NR_ATTR_S *`, `ISP_3NR_ATTR_S *`, `ISP_AE_S *`, `ISP_DYNAMIC_PLTM_S *`, `ISP_VISUAL_ANGLE_S *`, `ISP_DYNAMIC_GTM_S *`, `ISP_COLOR_MATRIX_ATTR_S *` | Not provided; they are compiled out of the vendor build and are not linkable. |
| The whole `mpi_ae` / `mpi_awb` unit | Not reimplemented; every vendor function there is inside a disabled block. |
| Orientation (`mirror` / `flip`) | A VI control owned by `fcap_` (`AW_MPI_VI_SetVippMirror` / `SetVippFlip`), not by this package. The `AW_MPI_ISP_*` mirror/flip symbols exist in the vendor tree but nothing calls them. |
| Per-frame cadence and the 3A loop | Owned by the clean framework (`isp_thread`, `isp_run`/`isp_stop`); `fisp_` creates no thread and never calls the 3A pipeline directly. |
| The load-reg `--wrap=isp_set_load_reg` shim | Belongs to the consumer (`mediad`), not this package. `fisp_` provides the documented bit-edit companion `fisp_load_reg_set_3dnr()` and the contract it must honour; the `--wrap` glue, buffer overlay and snapshot stay in `mediad`. |
| The `isp_set_cfg`/`isp_update`/`isp_ctx_config_*` tuning path | Not routed through by the wrappers. `isp_config.c` calls those directly; the picture-control and AE setters go through the V4L2 control layer, and WDR/NR/3DNR through `isp_set_attr_cfg`. |

## Documented companions (beyond the 25)

- `fisp_video_resolver` / `fisp_set_video_resolver()` — the ISP id → VI video
  device hand-off. Default scans the clean device layer's
  `media_params.video_dev[]` and matches `video_to_isp_id()`; `fcap_` may install
  its own.
- `fisp_load_reg_set_3dnr()` — the register-buffer bit edit (offset
  `0x1a0`, D3D bit `1 << 5`).

## Discrepancies recorded

- The public header's trailing comments advertise brightness/contrast/sharpness
  ranges that differ from the enforced ones. The enforced
  ranges are the behaviour: brightness −126..126, contrast −64..64, saturation
  −256..512, sharpness −32..32. Out-of-range brightness/contrast return
  `SUCCESS` and write nothing; saturation/sharpness return `FAILURE` and write
  nothing.

## Open questions (gaps hit during implementation)

1. **Negative error-macro numerics.** The value of the VI invalid-id /
   invalid-channel error macro, of `EN_ERR_EFUSE_ERROR` and of the ISP efuse
   error macro is not known. This
   package uses a documented placeholder `AW_ERR_VI_INVALID_CHN = (-2)` and
   `EN_ERR_EFUSE_ERROR == AW_ERR_ISP_EFUSE_ERROR == (-2)`. Callers only test
   `!= 0` / `< 0`, and the clean framework never returns the efuse result, so
   the branch is inert; the mapping is value-preserving. *Settles it:* the two
   public error headers.
2. **Resolver shape.** It was open whether `fcap_` exports a resolver
   or the clean device layer exposes the VI media-device global. This package
   implements the second option (scan `media_params.video_dev[]`) behind a
   settable seam; `fcap_` installs its own resolver (see
   `src/fcap/NOT-IMPLEMENTED.md`). Unresolved whether that
   array is the same one the vipp opens.
3. **Id-validation split.** The vendor validates the controls/getters against
   `HW_ISP_DEVICE_NUM` (1) yet the V4L2 path against `VI_ISP_NUM_MAX` (2).
   This package treats the more specific V4L2 rule as authoritative: the
   V4L2-backed entry points accept ids 0..1 and let the resolver decide,
   returning the invalid-id macro for ids outside `[0,2)`;
   lifecycle and the attribute path use `HW_ISP_DEVICE_NUM`.
4. **`Run()` with a non-efuse `isp_init` error.** Only the efuse result is
   special-cased. Implemented literally: only `EN_ERR_EFUSE_ERROR` short-
   circuits; any other `isp_init` result still falls through to `isp_run`. No
   test vector covers this.
5. **Per-frame load-reg invocation count.** The "once at init, once per frame"
   observation and the `reg->size`/`reg->addr` equality are framework behaviour
   (already covered by the framework tier's own tests); the host test here
   covers the buffer edit contract directly.
