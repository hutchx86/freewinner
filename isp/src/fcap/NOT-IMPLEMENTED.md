<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 freewinner contributors -->

# fcap_ (package A): what is deliberately not implemented

This package is a drop-in for the SDK's MPP capture layer (`mpi_sys.c`,
`mpi_vi.c`, `mpi_region.c`, `ChannelRegionInfo.c`, the VI component/registry
TUs, `videoIn/*` and `cdx_log.c`). It defines **exactly** the 19 symbols of
`cleanroom/middleware/fcap/SPEC.md` §1.1 — the 3 `AW_MPI_SYS_*` and 16
`AW_MPI_VI_*` entry points — plus the documented companion seam below. Verified
with `nm -g --defined-only build/fcap/fcap.o`: 21 text/data symbols, no
`AW_MPI_ISP_*`, nothing else.

## Left out on purpose

| Area | Behaviour here |
| --- | --- |
| The 25 `AW_MPI_ISP_*` symbols | Package B (`fisp_`, `src/fisp/`). Package A calls only `AW_MPI_ISP_Init` / `AW_MPI_ISP_Exit` from `SYS_Init` / `SYS_Exit` and `fisp_set_video_resolver`; it defines none of them. |
| `AW_MPI_VI_Init` / `AW_MPI_VI_Exit` | Kept private: `SYS_Init` / `SYS_Exit` do the equivalent work (SPEC §1.2, §3.1). |
| Region / OSD / mask (`AW_MPI_VI_{Set,Delete}Region`, `UpdateOverlayBitmap`, `UpdateRegionChnAttr`, `AW_MPI_VENC_*Region`, `AW_MPI_RGN_*`, the `videoInputHw_*` region helpers, `DrawOSD`, `ChannelRegionInfo_*`, all of `VIPPDrawOSD_V5.c`) | Not provided. The daemon draws OSD into the encoder bitstream (`osd.c`), not through the MPP region API. `overlay_set_fmt` / `orl_set_fmt` / `overlay_update` remain available in the clean device layer and are *not* called by this package (no region lifecycle exists to call them from). |
| `AW_MPI_VI_Debug_StoreFrame`, `AW_MPI_VI_RegisterCallback`, the channel-mask `AW_MPI_VI_SetShutterTime`, `AW_MPI_VI_SetVIFreq`, `AW_MPI_VI_GetVippMirror/Flip` | Not provided; nothing in the deployed build references them. |
| `AW_MPI_SYS_*` beyond the three (`GetConf`, `Init_S1/S2/S3`, `Bind`, `UnBind`, `InitPtsBase`, component/plugin/ion/timer helpers) | Not provided. |
| The generic component framework and registry (`COMP_*`, `ComponentsRegistryTable.c`, `mm_component.c`, every `VideoVi*` symbol) | Collapsed to the four internal objects of SPEC §4.1: one component facade per channel with a command queue and a worker thread, only for state transitions. No tunnelling, no registry, no generic framework. |
| `cdx_log.c` | Not reimplemented. This package logs to stderr directly; no caller needs `log_set_level`. |
| ISP picture-control / AWB / ISO / hue / scene setters | Package B, by symbol. |

## Documented companions (beyond the 19)

- `fcap_clock` / `fcap_set_clock()` (declared in `src/fcap/fcap_priv.h`) — the
  SPEC §7.2 clock seam. It replaces the capture worker's 2000 ms buffer wait and
  the per-channel frame wait so a host test need not sleep. A NULL argument (or a
  NULL field) restores the built-in implementation (`video_wait_buffer` and the
  clean `cdx_sem_*`).
- **Resolver installation.** `SYS_Init` calls `fisp_set_video_resolver()` with a
  resolver that scans this package's `/dev/media0` handle's `video_dev[]` for the
  device whose `video_to_isp_id()` matches. This resolves package B's open
  question 2 (its default resolver scans the framework's `media_params.video_dev[]`,
  which package A does not populate): without it every `AW_MPI_ISP_*` picture
  control returns FAILURE. `SYS_Exit` restores the default.

## Discrepancies recorded

- **Fourcc labels.** SPEC §8.3 labels the 2.0×/1.0× LBC fourccs `LC20` / `LC10`;
  the clean media-utils mapping (`src/utils/pixel_format.c`,
  `include/media_utils_abi.h`) uses `LC21` (`V4L2_PIX_FMT_LBC_2_0X`) and `LC23`
  (`V4L2_PIX_FMT_LBC_1_0X`). SPEC §4.2 says to use the clean util, so the capture
  path calls `map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E()` and the util's table wins. The
  2.5× (`LC22`) and NV21/NV12 (`NM21`/`NM12`) rows agree with the spec.
- **VI error ids.** SPEC §8.1 lists VI-private ids starting at 65 (`UNEXIST`=71,
  `EXIST`=72) and also the generic `EXIST`=4 / `UNEXIST`=5. SPEC §8.1 says the
  worked values in §7 are the contract, so `ERR_VI_EXIST` = `0xA0108004` and
  `ERR_VI_UNEXIST` = `0xA0108005` are used everywhere.
- **`PIXEL_FORMAT_E` values.** Taken from the shared clean header
  `include/media_utils_abi.h` (LBC_2_5X = 35, YVU_SEMIPLANAR_420 = 32, …), not
  restated. The spec names the enumerators but not their numbers.

## Choices where the spec was silent

1. **`GetVippAttr` reads the cached format.** SPEC §3.2.4 names `video_get_fmt`,
   which does not issue `VIDIOC_G_FMT`; the negotiated values come from the last
   `video_set_fmt`. Vector 6 is therefore exercised after a `SetVippAttr`.
2. **`EnableVipp` "re-reads the format" via `video_set_fmt`.** The only
   clean-layer call that issues `VIDIOC_G_FMT` is `video_set_fmt`, so `EnableVipp`
   re-applies the stored attribute to produce the G_FMT that vector 8 observes
   between REQBUFS and QBUF.
3. **`video_fmt.index` = the vipp index** (`VIDIOC_S_INPUT`). `VI_ATTR_S` carries
   no index and the spec does not state one; the vipp index is the only sensible
   source. Untested against the driver.
4. **Efuse mapping.** The clean `video_set_fmt` returns −1 for every rejection, so
   the SPEC §3.2.3 efuse mapping is selected by the same predicate the clean layer
   uses (width ≥ 3840, height ≥ 2160, fps > 25). Not covered by a vector.
5. **`mField`.** The clean `video_buffer` exposes no V4L2 `field`, so `mField` is
   taken from the negotiated format's `field` (SPEC §3.4.3 says "the buffer field";
   the field is not otherwise observable through the clean layer).
6. **NULL frame pointer.** `GetFrame` / `ReleaseFrame` reject NULL with
   `ERR_VI_NULL_PTR` (SPEC open question 3; the vendor path does not guard it).
   Deliberate divergence to avoid the crash.
7. **Manual shutter writes.** For PREVIEW/NIGHT_VIEW the mode is set with
   `V4L2_CID_EXPOSURE_AUTO=1` then `V4L2_CID_AUTOGAIN=0` ("set manual
   exposure/gain"), then the values with `V4L2_CID_EXPOSURE_ABSOLUTE` then
   `V4L2_CID_GAIN` ("write exposure and gain"). Current gain/exposure are read
   from `GAIN` and `EXPOSURE_ABSOLUTE`.
8. **`ERR_VI_BUSY` gate** is applied in `SetVippShutterTime` itself (the spec
   names the non-exported channel-mask variant); `iTime == 0` is rejected before
   the gate.
9. **Long-exposure channel logic** (SPEC §3.2.7, second half) is implemented
   approximately: the channel records reset mode / fps / frame count on entry
   (draining queued frames), and the first frame whose PTS interval exceeds
   `1000/fps*framecount + 500` ms clears the active flag and the vipp's
   long-exposure flag. The "scheduled reset event" for `AUTO_DELAY` is represented
   by that flag clear rather than a distinct event. No vector covers it (SPEC open
   question 5).
10. **Bounded shutdown wait.** The capture worker waits for the channel list to
    empty for at most 100 × 10 ms before releasing held frames and exiting, because
    `DisableVipp` is documented to proceed with a surviving virchn (SPEC §3.2.5);
    an unbounded wait would hang the join.
11. **`ERR_VI_EIS_EFUSE_ERR`** has no value in the spec; it is
    `DEF_ERR(VIU, 4, 25)` = `0xA0108019`.
12. **`MOD_ID_VIU`** has no value in `media_utils_abi.h`; a documented constant 16
    is stored in the component's `MPP_CHN_S`. Nothing reads it.
13. **Missing-object returns.** `GetVippAttr` / `SetVippAttr` / `EnableVipp` /
    `DisableVipp` / `DestoryVipp` on an unconstructed vipp return
    `ERR_VI_UNEXIST`. `DestoryVipp` also destroys any channels still attached
    (logging), per SPEC §9.
14. **The sensor device is reached via `media_params.isp_dev[]`**, the framework
    tier's global (package B's `isp_dev_uapi` sensor calls need
    `struct hw_isp_device *`). This is the compile-time-pinned integration point;
    the host seam defines the same global.

## Open questions raised for the parent / analyst

1. The `mkfcTmpDir` default (`/tmp`, SPEC §3.1) is not observable from the
   exported surface, so vector 2's "/tmp" clause is not host-testable. The
   defaulting code runs; only `SUCCESS` and the `NOT_PERM` rule are asserted.
2. Vector 18 ("push 21 frames without releasing") is only reachable with more
   than 20 driver buffers, because occupancy is gated per driver buffer index
   (SPEC §3.4.1). The host test requests `nbufs = 21` to exercise FIFO depth 20;
   the daemon's `nbufs = 3` cannot reach it. Confirm this reading of the vector.
3. `mStride` semantics (SPEC open question 2) are reproduced as the plane mapped
   length; no caller in the deployed build treats it as a line stride.
