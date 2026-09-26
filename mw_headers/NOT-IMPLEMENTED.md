<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Package D (`headers`) - NOT-IMPLEMENTED list

Clean-room interface tree for the `mediad` app translation units.  Tree root:
`cleanroom/middleware/headers/include/` (private; see "Include roots" below).
Source of truth: `SPEC.md` section 5 plus the deviations and gaps recorded here.

## 1. Whole headers deliberately not carried

| Vendor path | Why absent |
| --- | --- |
| `$(MW)/include/utils/plat_defines.h`, `plat_math.h`, `aw_type.h` | no clean consumer (SPEC 2.10); empty stubs are carried instead so the spellings resolve |
| `$(SW)/system/public/include/utils/cdx_list_type.h` | dragged in only by the vendor `plat_type.h`; the clean one does not include it |
| `$(ISP)/isp_dev/media.h` | no clean consumer (SPEC 5) |
| `$(ISP)/include/V4l2Camera/sunxi_camera_v2.h`, `.../linux/videodev2.h`, `v4l2-common.h`, `v4l2-controls.h`, `v4l2-mediabus.h` | replaced by the toolchain's `<linux/videodev2.h>` (SPEC 3.4): `v4l2_pix_format_mplane` is byte-identical (192 B, `plane_fmt` 20, `num_planes` 180) and carries the three `V4L2_*` constants used |
| `$(CEDARC)/include/veInterface.h`, `sc_interface.h` | the two pointer-only `VencBaseConfig` fields are declared `void *`; no forward declaration of the vendor op structs is needed |
| `mpi_videoformat_conversion.h` | `map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT` is called by `main.c` with no visible declaration in the vendor include set either (SPEC 7.1); the clean tree matches that and does not declare it |

Empty stubs **are** carried for: `utils/plat_errno.h`, `utils/plat_defines.h`,
`utils/plat_math.h`, `utils/aw_type.h`, `media/mm_comm_rc.h`, `isp_debug.h`,
`device/video.h`.

## 2. Declarations deliberately absent

- **MPP**: every module other than VI/ISP/SYS/VENC-region.  All audio
  (`mm_comm_aio/aenc/adec/clock`, `mpi_ai/aenc/adec/ao/clock`), decode, mux/demux,
  ISE/EIS, UVC, VO/region-private, TENc, VB, RC; every `MPP_EVENT_*`; the whole
  component framework beyond `COMP_HANDLETYPE` / `MM_COMPONENTTYPE` /
  `ComponentInit`.  `MPP_VERSION_S`, `POINT_S`, `SIZE_S`, `RECT_S`,
  `CROP_INFO_S`, `ROTATE_E`, `BORDER_S`, `MOD_ID_E` values, `PROFILE_TYPE_E`,
  `PAYLOAD_TYPE_E`, `MEDIA_FILE_FORMAT_E`, `AUDIO_*_E`, `MM_INVALID_*`.
- **SYS**: `SYS_VIRMEM_INFO_S`, `MppBindControl`, `handle_set`, `ERR_SYS_*`.
- **VI**: `VI_OsdMaskRegion`, `ANTISHAKE_*`, all `awVI_*` low-level enums and
  records, the interrupt/status/capture records, `VI_Params`, `struct rect`.
  Uncalled `AW_MPI_VI_*`: `GetVippMirror`, `GetVippFlip`, `GetVirChnAttr`,
  `SetShutterTime`, `SetVIFreq`, `RegisterCallback`.
- **ISP**: the register/tone/colour tuning surface not embedded in a used record;
  `isp_tuning_init`; all `*_helper` / `*_core_ops` functions;
  `isp_get_imageparams`, `isp_get_lv`, `isp_get_ev_lv_adj`; the media-device and
  video-node layers; AF/AFS/MD/roll-off/ISO algorithm entry points other than as
  record members required by `isp_lib_context`; `FROM_REPO_BRANCH` /
  `FROM_REPO_COMMIT` / `isp_get_version`.  `AW_MPI_ISP_Init`/`Exit` are not
  declared (the daemon calls only `Run`/`Stop`/`Exit`; SPEC 2.5).
- **VENC (`vencoder.h`)**: JPEG (`JpegEncInfo`, `EXIFInfo`, `AWJpecEnc`), H.265,
  VP8, ROI copy, SVC/skip, motion, VUI/EXIF, `VencSaveBSFile`, `VencThumbInfo`,
  `VeProcSet`/`VeProcEncInfo`, `VencBrightnessS`, `VencEncodeTimeS`,
  `VencSmartFun`, `VencHVS`, `VencHighPassFilter`, MB-info records, the
  allocate-buffer group, `VideoEncoderSetFreq`, `VideoEncoderSetDdrMode`,
  `VideoEncoderGetUnencodedBufferNum`, `VideoEncoderSetVbvInfo`, the two
  `*IommuAddr` entry points and `struct user_iommu_param`, `VENC_DEVICE`,
  `VideoEncUnInit`, `VideoEncoderGetUnencodedBufferNum`.
- **All vendor comments, string literals, log macros and file banners.**  Each
  header carries only its own SPDX block and the declarations.

## 3. Records reproduced only to their measured size (SPEC 4 gap)

`SPEC.md` section 4 measures these records only by total size and the offsets of
the fields the daemon uses; it does not enumerate their other members.  They are
reproduced as opaque *aligned integer arrays* of exactly the measured size (so
the record's alignment and the measured offsets hold without a separate padding
member), never as whole-record byte arrays, and no daemon translation unit reads
them:

- `RGN_ATTR_S.unAttr` (16 B) and `RGN_CHN_ATTR_S.unChnAttr` (56 B) union members;
- `VENC_DATA_TYPE_U` (4 B), `VENC_PACK_INFO_S` (12 B);
- `VencAdvancedRefParam` (16 B), `VencFixQP` (12 B), `VencMBModeCtrl` (8 B),
  `VencOverlayCoverYuvS` (4 B, 2-byte aligned so it sits at `+10`);
- the unnamed `VencRcParam` gap fields (`+4..+56`, `+88..+128`).

If package A/C ever needs a real field of one of these, `SPEC.md` section 4 must
be extended with that record's measured layout first.

`struct hw_isp_device` is declared **opaque** in `device/isp_dev.h`: the six app
translation units only pass its pointer to `isp_set_load_reg`.  The measured
956-byte record is owned by the device layer (`freeisp/isp_dev_uapi.h`), not by
this package.

## 4. Deviations from SPEC section 3.2's file mapping

- The shared base declarations (SPEC 3.2 maps `ISP_REG_TBL_LENGTH`,
  `ISP_GAMMA_TBL_LENGTH`, `isp_rgb2rgb_gain_offset`, `isp_sensor_info_t` and the
  module-config helper records to `isp_comm.h`) live in **`isp_base.h`**.  The
  base records and the module-config records need them in both directions and C
  headers have no partial ordering; `isp_comm.h` includes `isp_base.h`, so every
  consumer of `isp_comm.h` still sees the same declarations unchanged.
- `isp_af_settings_t` is declared in `isp_3a_af.h` (SPEC 3.2) rather than in
  `isp_manage.h`.
- `isp_image_params_t` (the only fragment of `sunxi_camera_v2.h` retained) is
  declared in `isp_manage.h`, because `struct isp_lib_context` embeds it and the
  measured context size depends on it.
- `utils/plat_type.h` pulls `<stddef.h>` / `<stdlib.h>`: the vendor header set
  provided `NULL` and `getenv` transitively and the sources rely on that.

## 5. Include roots this tree expects

Mirroring the vendor relative paths means four `-I` roots replace the nine
dropped vendor roots:

| Clean root | Vendor root(s) it replaces |
| --- | --- |
| `<tree>/include` | `$(MW)/include`, `$(MEDIA)/include`, `$(CEDARC)/include`, `$(ISP)/include`, `$(ISP)/isp_tuning`, `$(ISP)` |
| `<tree>/include/media` | `$(MW)/include/media` |
| `<tree>/include/utils` | `$(MW)/include/utils` |
| `<tree>/include/component` | `$(MEDIA)/include/component` |

## 6. Verification not performed by the implementer

- SPEC section 6.5 (verbatim-line copy detector against
  `repos/lindenis-v833-softwinner/`) was **not** run by the implementer: the
  clean-room firewall forbids the implementer from opening the vendor tree.
- **Run since (independent `verifier`, 2026-09-23), with a result the reader
  must not misread as "no overlap":**
  - **0** vendor comments/prose, **0** vendor include guards (all ours are
    `FMW_*`), **0** function bodies, **0** executable statements, **0**
    function-like macro bodies. Every `#define` body is a bare literal or
    `(1<<n)`.
  - **But the declaration text does overlap heavily:** ~73.6% of this tree's
    non-blank lines are normalised-identical to a vendor header line, and the
    overlap preserves the vendor's arbitrary blank-line placement and tab
    indentation (e.g. the `ISP_DENOISE_*` enum block). That is textual
    derivation of the declarations, not independent re-authoring.
  - The names/values/order are the ABI the deployed libisp archive and the
    clean 3A tier use, so they must be reproduced; reproducing the vendor's
    *formatting* was not required. This is within the project's documented
    "interoperability ABI surfaces" exception (`CREDITS.md:15-18`, the same
    technique as `isp/include/freeisp/sdk_interop.h`), but it is a **judgement
    call under `AGENTS.md`'s flat "no vendor code"** and is flagged for Hutch.
    Remedy if the strict reading is adopted: re-author the declarations
    (comments + independent formatting, renaming anything not ABI-bound as
    `sdk_interop.h`/`*_abi.h` already do).
- SPEC section 6.4 (end-to-end link + on-camera ABI oracle) belongs to the
  daemon owner.

## 7. ISP headers removed (2026-09-26)

- All 18 `isp*.h` (`isp.h`, `isp_3a_*.h`, `isp_base.h`, `isp_comm.h`,
  `isp_manage.h`, `isp_tuning*.h`, `isp_type.h`, ...) are deleted. Since the r1
  framework, mediad reaches the ISP only through `framework_isp.h` /
  `fwi_isp_api.h`; the last two includers (`isp_config.c`,
  `stub_audio_components.c`) needed only the `HW_ISP_CFG_*` ids and
  `ISP_REG_TBL_LENGTH`, now public in `fwi_isp_api.h`. These headers were the
  bulk of section 6's formatting overlap (similarity scan 2026-09-26: ~35k
  matching tokens). mediad builds byte-identical with or without them.
