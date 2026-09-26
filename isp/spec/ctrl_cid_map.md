<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `isp_cid_array` — exact CID table + public-V4L2 cross-check

Phase-2 UAPI interface-facts reference. Extracted from the vendor framework
device file:

- `libisp/isp_dev/isp_dev.c` (`struct isp_cid`, `isp_cid_array[]`)
- Compile flag for the deployed binding: `ISP_VERSION=521` → `ISP_LIB_USE_FLASH=0`
  (`include/isp_3a_ae.h`; only `ISP_VERSION==522` sets it to 1).
- Only `isp_cid_array` exists as a name↔id table in this file (grep for
  array-of-struct initialisers returned nothing else).

## 0. Struct

```c
struct isp_cid {
	char name[64];
	int id;
};
```

`name` is only ever consumed by the `ISP_PRINT` in the event handler; `id` is the
subscription/dispatch key. The name strings are the literal macro names
`"V4L2_CID_*"`, not descriptive lowercase labels.

## 1. Table (source order, verbatim)

Conditional block `#if ISP_LIB_USE_FLASH` at `isp_dev.c`.

| idx (522) | idx (521) | name string | macro | id (hex) | id (dec) | class |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 0 | `"V4L2_CID_BRIGHTNESS"` | `V4L2_CID_BRIGHTNESS` | 0x00980900 | 9963776 | user |
| 1 | 1 | `"V4L2_CID_CONTRAST"` | `V4L2_CID_CONTRAST` | 0x00980901 | 9963777 | user |
| 2 | 2 | `"V4L2_CID_SATURATION"` | `V4L2_CID_SATURATION` | 0x00980902 | 9963778 | user |
| 3 | 3 | `"V4L2_CID_HUE"` | `V4L2_CID_HUE` | 0x00980903 | 9963779 | user |
| 4 | 4 | `"V4L2_CID_AUTO_WHITE_BALANCE"` | `V4L2_CID_AUTO_WHITE_BALANCE` | 0x0098090c | 9963788 | user |
| 5 | 5 | `"V4L2_CID_EXPOSURE"` | `V4L2_CID_EXPOSURE` | 0x00980911 | 9963793 | user |
| 6 | 6 | `"V4L2_CID_AUTOGAIN"` | `V4L2_CID_AUTOGAIN` | 0x00980912 | 9963794 | user |
| 7 | 7 | `"V4L2_CID_GAIN"` | `V4L2_CID_GAIN` | 0x00980913 | 9963795 | user |
| 8 | 8 | `"V4L2_CID_POWER_LINE_FREQUENCY"` | `V4L2_CID_POWER_LINE_FREQUENCY` | 0x00980918 | 9963800 | user |
| 9 | 9 | `"V4L2_CID_HUE_AUTO"` | `V4L2_CID_HUE_AUTO` | 0x00980919 | 9963801 | user |
| 10 | 10 | `"V4L2_CID_WHITE_BALANCE_TEMPERATURE"` | `V4L2_CID_WHITE_BALANCE_TEMPERATURE` | 0x0098091a | 9963802 | user |
| 11 | 11 | `"V4L2_CID_SHARPNESS"` | `V4L2_CID_SHARPNESS` | 0x0098091b | 9963803 | user |
| 12 | 12 | `"V4L2_CID_CHROMA_AGC"` | `V4L2_CID_CHROMA_AGC` | 0x0098091d | 9963805 | user |
| 13 | 13 | `"V4L2_CID_COLORFX"` | `V4L2_CID_COLORFX` | 0x0098091f | 9963807 | user |
| 14 | 14 | `"V4L2_CID_AUTOBRIGHTNESS"` | `V4L2_CID_AUTOBRIGHTNESS` | 0x00980920 | 9963808 | user |
| 15 | 15 | `"V4L2_CID_BAND_STOP_FILTER"` | `V4L2_CID_BAND_STOP_FILTER` | 0x00980921 | 9963809 | user |
| 16 | 16 | `"V4L2_CID_ILLUMINATORS_1"` | `V4L2_CID_ILLUMINATORS_1` | 0x00980925 | 9963813 | user |
| 17 | 17 | `"V4L2_CID_ILLUMINATORS_2"` | `V4L2_CID_ILLUMINATORS_2` | 0x00980926 | 9963814 | user |
| 18 | 18 | `"V4L2_CID_EXPOSURE_AUTO"` | `V4L2_CID_EXPOSURE_AUTO` | 0x009a0901 | 10094849 | camera |
| 19 | 19 | `"V4L2_CID_EXPOSURE_ABSOLUTE"` | `V4L2_CID_EXPOSURE_ABSOLUTE` | 0x009a0902 | 10094850 | camera |
| 20 | 20 | `"V4L2_CID_EXPOSURE_AUTO_PRIORITY"` | `V4L2_CID_EXPOSURE_AUTO_PRIORITY` | 0x009a0903 | 10094851 | camera |
| 21 | 21 | `"V4L2_CID_FOCUS_ABSOLUTE"` | `V4L2_CID_FOCUS_ABSOLUTE` | 0x009a090a | 10094858 | camera |
| 22 | 22 | `"V4L2_CID_FOCUS_RELATIVE"` | `V4L2_CID_FOCUS_RELATIVE` | 0x009a090b | 10094859 | camera |
| 23 | 23 | `"V4L2_CID_FOCUS_AUTO"` | `V4L2_CID_FOCUS_AUTO` | 0x009a090c | 10094860 | camera |
| 24 | 24 | `"V4L2_CID_AUTO_EXPOSURE_BIAS"` | `V4L2_CID_AUTO_EXPOSURE_BIAS` | 0x009a0913 | 10094867 | camera |
| 25 | 25 | `"V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE"` | `V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE` | 0x009a0914 | 10094868 | camera |
| 26 | 26 | `"V4L2_CID_WIDE_DYNAMIC_RANGE"` | `V4L2_CID_WIDE_DYNAMIC_RANGE` | 0x009a0915 | 10094869 | camera |
| 27 | 27 | `"V4L2_CID_IMAGE_STABILIZATION"` | `V4L2_CID_IMAGE_STABILIZATION` | 0x009a0916 | 10094870 | camera |
| 28 | 28 | `"V4L2_CID_ISO_SENSITIVITY"` | `V4L2_CID_ISO_SENSITIVITY` | 0x009a0917 | 10094871 | camera |
| 29 | 29 | `"V4L2_CID_ISO_SENSITIVITY_AUTO"` | `V4L2_CID_ISO_SENSITIVITY_AUTO` | 0x009a0918 | 10094872 | camera |
| 30 | 30 | `"V4L2_CID_EXPOSURE_METERING"` | `V4L2_CID_EXPOSURE_METERING` | 0x009a0919 | 10094873 | camera |
| 31 | 31 | `"V4L2_CID_SCENE_MODE"` | `V4L2_CID_SCENE_MODE` | 0x009a091a | 10094874 | camera |
| 32 | 32 | `"V4L2_CID_3A_LOCK"` | `V4L2_CID_3A_LOCK` | 0x009a091b | 10094875 | camera |
| 33 | 33 | `"V4L2_CID_AUTO_FOCUS_START"` | `V4L2_CID_AUTO_FOCUS_START` | 0x009a091c | 10094876 | camera |
| 34 | 34 | `"V4L2_CID_AUTO_FOCUS_STOP"` | `V4L2_CID_AUTO_FOCUS_STOP` | 0x009a091d | 10094877 | camera |
| 35 | 35 | `"V4L2_CID_AUTO_FOCUS_RANGE"` | `V4L2_CID_AUTO_FOCUS_RANGE` | 0x009a091f | 10094879 | camera |
| 36 | — | `"V4L2_CID_TAKE_PICTURE"` | `V4L2_CID_TAKE_PICTURE` | 0x00981956 | 9967958 | **sunxi+6** |
| 37 | — | `"V4L2_CID_FLASH_LED_MODE"` | `V4L2_CID_FLASH_LED_MODE` | 0x009c0901 | 10225921 | flash |
| 38 | — | `"V4L2_CID_FLASH_LED_MODE_V1"` | `V4L2_CID_FLASH_LED_MODE_V1` | 0x00981967 | 9967975 | **sunxi+23** |
| 39 | 36 | `"AE windows x1"` | `V4L2_CID_AE_WIN_X1` | 0x0098195f | 9967967 | **sunxi+15** |
| 40 | 37 | `"AE windows y1"` | `V4L2_CID_AE_WIN_Y1` | 0x00981960 | 9967968 | **sunxi+16** |
| 41 | 38 | `"AE windows x2"` | `V4L2_CID_AE_WIN_X2` | 0x00981961 | 9967969 | **sunxi+17** |
| 42 | 39 | `"AE windows Y2"` | `V4L2_CID_AE_WIN_Y2` | 0x00981962 | 9967970 | **sunxi+18** |
| 43 | 40 | `"AF windows x1"` | `V4L2_CID_AF_WIN_X1` | 0x00981963 | 9967971 | **sunxi+19** |
| 44 | 41 | `"AF windows y1"` | `V4L2_CID_AF_WIN_Y1` | 0x00981964 | 9967972 | **sunxi+20** |
| 45 | 42 | `"AF windows x2"` | `V4L2_CID_AF_WIN_X2` | 0x00981965 | 9967973 | **sunxi+21** |
| 46 | 43 | `"AF windows y2"` | `V4L2_CID_AF_WIN_Y2` | 0x00981966 | 9967974 | **sunxi+22** |

Notes:
- Case of the window names is literal: `"AE windows Y2"` (capital Y) and
  `"AF windows y2"` (lowercase y). `AE`/`AF` prefixes have one space, no colon.
- `sunxi+N` = `V4L2_CID_USER_SUNXI_CAMERA_BASE + N`, with
  `V4L2_CID_USER_SUNXI_CAMERA_BASE = V4L2_CID_USER_BASE + 0x1050`
  (`sunxi_camera_v2.h`) = 0x00981950.

## 2. Entry count

| build | `ISP_LIB_USE_FLASH` | entries | indices |
| --- | --- | --- | --- |
| `ISP_VERSION=521` (**deployed**) | 0 | **44** | 0-35 core, then windows 36-43 |
| `ISP_VERSION=522` | 1 | 47 | 0-35 core, 36-38 flash, 39-46 windows |

The `array_size()` macro (`isp_dev.c`) is a compile-time
`sizeof/sizeof`, so the loop bound follows the table automatically.

## 3. Usage sites

Only two (`grep isp_cid_array`):

1. `isp_dev.c` in `int isp_dev_start(struct hw_isp_device *isp)`:
   for every entry, `VIDIOC_SUBSCRIBE_EVENT` on `isp->subdev.fd` with
   `v4l2_event_subscription{ .id = isp_cid_array[i].id, .type = V4L2_EVENT_CTRL }`
   and `flags` left 0. A failed ioctl is fatal (`return ret`), so **all 44 ids
   must be accepted by the subdev** for `isp_dev_start` to succeed.
2. `isp_dev.c` in `isp_subdev_handle_event`: on a
   `V4L2_EVENT_CTRL`, linear scan match of `event.id`, then
   `ISP_PRINT("name is %s, value is 0x%x\n", isp_cid_array[i].name, ...)`.
   No match is not an error (falls through to `ops->ctrl_process`).

## 4. Cross-check against public `V4L2_CID_*`

Measurement: compiled the table's macros against both the vendor-shipped
`include/V4l2Camera/linux/v4l2-controls.h` and the system
`/usr/include/linux/v4l2-controls.h`. **The two agree on every standard control
id in this table**, so the vendor ids are ordinary public values and the clean
binding must use the same numbers.

- **Standard, matches public exactly (36 of 44):** all 36 core entries — the
  `user`-class entries plus the `camera`-class `EXPOSURE_*`, `FOCUS_*`,
  `AUTO_EXPOSURE_BIAS`, `AUTO_N_PRESET_WHITE_BALANCE`, `WIDE_DYNAMIC_RANGE`,
  `IMAGE_STABILIZATION`, `ISO_*`, `EXPOSURE_METERING`, `SCENE_MODE`, `3A_LOCK`
  and `AUTO_FOCUS_{START,STOP,RANGE}`.
- **Non-standard / vendor-private (8 of 44):** the 8 AE/AF window ids
  (`sunxi+15..+22`). These have no public Linux equivalent; they are expressed
  relative to `V4L2_CID_USER_BASE` (see §1).
- The flash-conditional `V4L2_CID_TAKE_PICTURE` (`sunxi+6`) and
  `V4L2_CID_FLASH_LED_MODE_V1` (`sunxi+23`) are also vendor-private, but they are
  **not** part of the deployed 44-entry table (`ISP_LIB_USE_FLASH=0`); they appear
  only when flash is compiled in (§2) and are otherwise routed but absent from the
  array (§5).
- No id in the vendor table collides with a different public control.

## 5. Clean-binding reconstruction errors — resolved

An earlier revision of the clean binding rebuilt the control ids with pre-4.x
kernel numbering (7 wrong: `ILLUMINATORS_1`/`_2` off by one, and
`AUTO_N_PRESET_WHITE_BALANCE`/`ISO_SENSITIVITY`/`ISO_SENSITIVITY_AUTO`/
`EXPOSURE_METERING`/`SCENE_MODE` on the old slots) and carried a different
44-entry set. Both defects are fixed in the tree:

- `include/isp_dev_uapi.h:695-760` now carries the public/vendor numbering. The
  seven ids above now match: `V4L2_CID_ILLUMINATORS_1 = BASE+37` (:724),
  `V4L2_CID_ILLUMINATORS_2 = BASE+38` (:725),
  `V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE = CAM+20` (:738),
  `V4L2_CID_ISO_SENSITIVITY = CAM+23` (:741),
  `V4L2_CID_ISO_SENSITIVITY_AUTO = CAM+24` (:742),
  `V4L2_CID_EXPOSURE_METERING = CAM+25` (:743),
  `V4L2_CID_SCENE_MODE = CAM+26` (:744).
- `src/framework/isp_dev_uapi.c:1232-1277` is the 44-entry `isp_cid_array`
  (`ISP_CID_NUM == 44`, `include/isp_dev_uapi.h:59`). Its set matches the vendor
  set: it includes `WIDE_DYNAMIC_RANGE` (:1259), `IMAGE_STABILIZATION` (:1260),
  `3A_LOCK` (:1265), `AUTO_FOCUS_{START,STOP,RANGE}` (:1266-1268), and the eight
  `AE/AF_WIN_*` ids (:1269-1276); every `name` is the literal macro name.
- `src/framework/isp_dev_uapi.c:1638-1647` is the `ISP_CID_NUM` subscription loop
  that uses those ids.

## 6. Correspondence with `__isp_ctrl_process` (isp.c)

Dispatch is not a `switch` over cids: `cid_routes[]` (`isp/src/framework/isp.c:1167-1201`)
is a **33-entry** table of `{cid, setter, arg-mode}`, scanned linearly by
`__isp_ctrl_process` (`:1203-1238`), which then switches on the arg-mode
(`:1220-1233`). Of those 33 entries, **32 carry a setter and one is
`CID_ARG_DROP`** — `V4L2_CID_3A_LOCK`, with a `NULL` setter (`:1194`) — so 32 ids
reach a setter and 1 is recognised but discarded.

The route table is **not** the same set as the 44-entry subscribed table. **14 of
the 44 table entries have no route** and fall through to the
`unhandled v4l2 control` log (`:1237`): the 8 `AE/AF_WIN_*` ids,
`CHROMA_AGC`, `COLORFX`, `EXPOSURE_AUTO_PRIORITY`, `HUE_AUTO`,
`IMAGE_STABILIZATION` and `WIDE_DYNAMIC_RANGE` (verified by set-difference of
`isp_cid_array` against `cid_routes`). Conversely, `TAKE_PICTURE`,
`FLASH_LED_MODE` and `FLASH_LED_MODE_V1` are routed in `isp.c` but compiled out of
the table at `ISP_VERSION=521`. Exact route↔setter mapping is not duplicated here;
see `isp.c` and the r1 framework spec (`20-framework.md` §11).

## 7. Which ids must be live

Only `isp_dev.c` consumes the table, so its two live paths are `isp_dev_start`
(subscribe) and `isp_subdev_handle_event` (log/dispatch). For the deployed 521
build all **44** ids must be live: step 3 of `isp_dev_start` (`isp_dev.c`)
aborts the whole start on the first failed `VIDIOC_SUBSCRIBE_EVENT`, and the 8
`AE/AF_WIN_*` + the 36 standard ids are all subscribed (36 + 8 = 44). The 7 ids
listed in §5 are the ones a wrong reconstruction silently breaks — the two
user-class `ILLUMINATORS_*` (`BASE+37/38`) and the five camera-class renumbered
ones (`AUTO_N_PRESET_WHITE_BALANCE`, `ISO_SENSITIVITY`, `ISO_SENSITIVITY_AUTO`,
`EXPOSURE_METERING`, `SCENE_MODE`) — because subscription may succeed against the
wrong control and `ctrl_process` then never fires for the real id. The three
flash-class ids are not live at 521.
