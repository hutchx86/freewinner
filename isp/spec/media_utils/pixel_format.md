<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# `pixel_format` — V4L2 fourcc ↔ AW pixel-format mapping (media-utils phase 3b)

Clean-room behaviour spec for the live subset of the vendor media-utils
conversion unit. The clean replacement is `src/utils/pixel_format.c`;
this document is its only source of vendor
behaviour. No vendor comments, strings, file:line citations, or structural
transcription appear below — only interface facts, constants, and the exact
input→output policy. See `isp/docs/provenance.md` for the full provenance
picture.

## 1. Scope / live surface

The vendor unit exports seven functions. Exactly **two** are live and are the
whole scope of this spec:

| Symbol | Direction | Live caller class |
| --- | --- | --- |
| `map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E` | V4L2 fourcc → AW `PIXEL_FORMAT_E` | camera/VI path |
| `map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT` | AW `PIXEL_FORMAT_E` → V4L2 fourcc | OSD/VIPPDraw path |

Five exports are **dead** (zero references in the current link set) and must
**not** be reimplemented: the audio-file-format probe,
`map_V4L2_FIELD_to_VIDEO_FIELD_E`, `map_VIDEO_FIELD_E_to_V4L2_FIELD`,
`map_EPIXELFORMAT_to_PIXEL_FORMAT_E`, `map_PIXEL_FORMAT_E_to_EPIXELFORMAT`. Only
the four field/`EPIXELFORMAT` maps have prototypes in the vendor header;
the audio-file-format probe has none. Because the clean object replaces the vendor
object at link time, dropping these five is safe only while their reference
count stays zero (re-derive if the VDEC/VO/ISE/audio components re-enter the
link).

Both live functions are **pure, stateless, total**: every input value maps to a
value, no allocation, no `errno`, no globals, thread-safe. The only side effect
is a diagnostic log on the default branch (and, forward-only, on the compressed
cases); the log text is vendor expression and is **out of contract** (see §10).

## 2. Prototypes, linkage, ABI

```c
PIXEL_FORMAT_E map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(int v4l2PixFmt);
int            map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT(PIXEL_FORMAT_E format);
```

- External C linkage, non-`static`, exactly these symbol names (ABI: the VI/OSD
  glue is compiled against the vendor header and calls them by name). The ABI
  header must wrap them in `extern "C"` for C++ consumers.
- Forward takes the fourcc as **`int`**, reverse takes the enum **by value** and
  returns a fourcc as **`int`**.
- `PIXEL_FORMAT_E` is a 4-byte enum on the target; the fourcc is a 4-byte
  integer. Do not change either width.
- The clean tree publishes the file's symbols through its own clean ABI header
  (see §7); it must not include the vendor media header.

## 3. Required constants

### 3.1 AW-private fourccs (not kernel UAPI)

These four are defined only in the vendor camera header, not in
`linux/videodev2.h`. Clean code must declare them itself with these exact
numeric values; no UAPI header supplies them.

| Macro | fourcc chars | numeric (hex) | numeric (dec) |
| --- | --- | --- | --- |
| `V4L2_PIX_FMT_FBC` | `F` `C` `2` `1` | `0x31324346` | 825377606 |
| `V4L2_PIX_FMT_LBC_2_0X` | `L` `C` `2` `1` | `0x3132434C` | 825377612 |
| `V4L2_PIX_FMT_LBC_2_5X` | `L` `C` `2` `2` | `0x3232434C` | 842154828 |
| `V4L2_PIX_FMT_LBC_1_0X` | `L` `C` `2` `3` | `0x3332434C` | 858932044 |

Declaration form (using the clean header's existing fourcc macro):

```c
#define V4L2_PIX_FMT_FBC       V4L2_FOURCC('F', 'C', '2', '1')
#define V4L2_PIX_FMT_LBC_2_0X  V4L2_FOURCC('L', 'C', '2', '1')
#define V4L2_PIX_FMT_LBC_2_5X  V4L2_FOURCC('L', 'C', '2', '2')
#define V4L2_PIX_FMT_LBC_1_0X  V4L2_FOURCC('L', 'C', '2', '3')
```

`V4L2_FOURCC(a,b,c,d)` must be
`(uint32_t)(a) | ((uint32_t)(b)<<8) | ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24)`.
These are camera-pipeline compressed formats (frame buffer compression /
lossless bit compression), not decoder formats.

### 3.2 Standard fourccs consumed (all public UAPI values)

The clean unit uses 24 standard fourccs. They are ordinary kernel UAPI values;
the clean ABI header must carry them (pinned, not depending on a host header)
with these values.

| Macro | hex | dec | Macro | hex | dec |
| --- | --- | --- | --- | --- | --- |
| `V4L2_PIX_FMT_YUV420M` | `0x32314D59` | 842091865 | `V4L2_PIX_FMT_SBGGR10` | `0x30314742` | 808535874 |
| `V4L2_PIX_FMT_YVU420M` | `0x31324D59` | 825380185 | `V4L2_PIX_FMT_SRGGB10` | `0x30314752` | 808535890 |
| `V4L2_PIX_FMT_NV12M` | `0x32314D4E` | 842091854 | `V4L2_PIX_FMT_SGBRG10` | `0x30314247` | 808534599 |
| `V4L2_PIX_FMT_NV21M` | `0x31324D4E` | 825380174 | `V4L2_PIX_FMT_SGRBG10` | `0x30314142` | 808534338 |
| `V4L2_PIX_FMT_NV16M` | `0x36314D4E` | 909200718 | `V4L2_PIX_FMT_SBGGR12` | `0x32314742` | 842090306 |
| `V4L2_PIX_FMT_NV61M` | `0x31364D4E` | 825642318 | `V4L2_PIX_FMT_SRGGB12` | `0x32314752` | 842090322 |
| `V4L2_PIX_FMT_YUYV` | `0x56595559` | 1448695129 | `V4L2_PIX_FMT_SGBRG12` | `0x32314247` | 842089031 |
| `V4L2_PIX_FMT_RGB555` | `0x4F424752` | 1329743698 | `V4L2_PIX_FMT_SGRBG12` | `0x32314142` | 842088770 |
| `V4L2_PIX_FMT_RGB32` | `0x34424752` | 876758866 | `V4L2_PIX_FMT_SBGGR8` | `0x31384142` | 825770306 |
| `V4L2_PIX_FMT_MJPEG` | `0x47504A4D` | 1196444237 | `V4L2_PIX_FMT_SRGGB8` | `0x42474752` | 1111967570 |
| `V4L2_PIX_FMT_JPEG` | `0x4745504A` | 1195724874 | `V4L2_PIX_FMT_SGBRG8` | `0x47524247` | 1196573255 |
| `V4L2_PIX_FMT_H264` | `0x34363248` | 875967048 | `V4L2_PIX_FMT_SGRBG8` | `0x47425247` | 1195528775 |

### 3.3 `PIXEL_FORMAT_E` / `MM_PIXEL_FORMAT_*`

The enum is contiguous from 0 with only `MM_PIXEL_FORMAT_RGB_1BPP = 0` spelled
out; all later enumerators are `previous + 1`, ending `MM_PIXEL_FORMAT_BUTT = 49`.
The 26 values the two functions reference:

| Value | Enumerator | Value | Enumerator |
| --- | --- | --- | --- |
| 8 (`0x08`) | `MM_PIXEL_FORMAT_RGB_1555` | 37 (`0x25`) | `MM_PIXEL_FORMAT_RAW_SBGGR8` |
| 10 (`0x0A`) | `MM_PIXEL_FORMAT_RGB_8888` | 38 (`0x26`) | `MM_PIXEL_FORMAT_RAW_SGBRG8` |
| 20 (`0x14`) | `MM_PIXEL_FORMAT_YUV_PLANAR_420` | 39 (`0x27`) | `MM_PIXEL_FORMAT_RAW_SGRBG8` |
| 22 (`0x16`) | `MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422` | 40 (`0x28`) | `MM_PIXEL_FORMAT_RAW_SRGGB8` |
| 23 (`0x17`) | `MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420` | 41 (`0x29`) | `MM_PIXEL_FORMAT_RAW_SBGGR10` |
| 26 (`0x1A`) | `MM_PIXEL_FORMAT_YUYV_PACKAGE_422` | 42 (`0x2A`) | `MM_PIXEL_FORMAT_RAW_SGBRG10` |
| 30 (`0x1E`) | `MM_PIXEL_FORMAT_YVU_PLANAR_420` | 43 (`0x2B`) | `MM_PIXEL_FORMAT_RAW_SGRBG10` |
| 31 (`0x1F`) | `MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422` | 44 (`0x2C`) | `MM_PIXEL_FORMAT_RAW_SRGGB10` |
| 32 (`0x20`) | `MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420` | 45 (`0x2D`) | `MM_PIXEL_FORMAT_RAW_SBGGR12` |
| 33 (`0x21`) | `MM_PIXEL_FORMAT_YUV_AW_AFBC` | 46 (`0x2E`) | `MM_PIXEL_FORMAT_RAW_SGBRG12` |
| 34 (`0x22`) | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X` | 47 (`0x2F`) | `MM_PIXEL_FORMAT_RAW_SGRBG12` |
| 35 (`0x23`) | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X` | 48 (`0x30`) | `MM_PIXEL_FORMAT_RAW_SRGGB12` |
| 36 (`0x24`) | `MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X` | 49 (`0x31`) | `MM_PIXEL_FORMAT_BUTT` |

## 4. Forward map — `map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E`

Exact case order (source order matters only for readability; the cases are
disjoint constants). Every other `int` value hits `default`.

| # | source fourcc | src hex | src dec | → destination enumerator | dst value |
| --- | --- | --- | --- | --- | --- |
| 1 | `V4L2_PIX_FMT_NV21M` | `0x31324D4E` | 825380174 | `MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420` | 32 (`0x20`) |
| 2 | `V4L2_PIX_FMT_NV12M` | `0x32314D4E` | 842091854 | `MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420` | 23 (`0x17`) |
| 3 | `V4L2_PIX_FMT_YUV420M` | `0x32314D59` | 842091865 | `MM_PIXEL_FORMAT_YUV_PLANAR_420` | 20 (`0x14`) |
| 4 | `V4L2_PIX_FMT_YVU420M` | `0x31324D59` | 825380185 | `MM_PIXEL_FORMAT_YVU_PLANAR_420` | 30 (`0x1E`) |
| 5 | `V4L2_PIX_FMT_YUYV` | `0x56595559` | 1448695129 | `MM_PIXEL_FORMAT_YUYV_PACKAGE_422` | 26 (`0x1A`) |
| 6 | `V4L2_PIX_FMT_NV16M` | `0x36314D4E` | 909200718 | `MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422` | 22 (`0x16`) |
| 7 | `V4L2_PIX_FMT_NV61M` | `0x31364D4E` | 825642318 | `MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422` | 31 (`0x1F`) |
| 8 | `V4L2_PIX_FMT_FBC` | `0x31324346` | 825377606 | `MM_PIXEL_FORMAT_YUV_AW_AFBC` | 33 (`0x21`) |
| 9 | `V4L2_PIX_FMT_LBC_2_0X` | `0x3132434C` | 825377612 | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X` | 34 (`0x22`) |
| 10 | `V4L2_PIX_FMT_LBC_2_5X` | `0x3232434C` | 842154828 | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X` | 35 (`0x23`) |
| 11 | `V4L2_PIX_FMT_LBC_1_0X` | `0x3332434C` | 858932044 | `MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X` | 36 (`0x24`) |
| 12 | `V4L2_PIX_FMT_SBGGR8` | `0x31384142` | 825770306 | `MM_PIXEL_FORMAT_RAW_SBGGR8` | 37 (`0x25`) |
| 13 | `V4L2_PIX_FMT_SRGGB8` | `0x42474752` | 1111967570 | `MM_PIXEL_FORMAT_RAW_SRGGB8` | 40 (`0x28`) |
| 14 | `V4L2_PIX_FMT_SGRBG8` | `0x47425247` | 1195528775 | `MM_PIXEL_FORMAT_RAW_SGRBG8` | 39 (`0x27`) |
| 15 | `V4L2_PIX_FMT_SGBRG8` | `0x47524247` | 1196573255 | `MM_PIXEL_FORMAT_RAW_SGBRG8` | 38 (`0x26`) |
| 16 | `V4L2_PIX_FMT_SBGGR10` | `0x30314742` | 808535874 | `MM_PIXEL_FORMAT_RAW_SBGGR10` | 41 (`0x29`) |
| 17 | `V4L2_PIX_FMT_SRGGB10` | `0x30314752` | 808535890 | `MM_PIXEL_FORMAT_RAW_SRGGB10` | 44 (`0x2C`) |
| 18 | `V4L2_PIX_FMT_SGRBG10` | `0x30314142` | 808534338 | `MM_PIXEL_FORMAT_RAW_SGRBG10` | 43 (`0x2B`) |
| 19 | `V4L2_PIX_FMT_SGBRG10` | `0x30314247` | 808534599 | `MM_PIXEL_FORMAT_RAW_SGBRG10` | 42 (`0x2A`) |
| 20 | `V4L2_PIX_FMT_SBGGR12` | `0x32314742` | 842090306 | `MM_PIXEL_FORMAT_RAW_SBGGR12` | 45 (`0x2D`) |
| 21 | `V4L2_PIX_FMT_SRGGB12` | `0x32314752` | 842090322 | `MM_PIXEL_FORMAT_RAW_SRGGB12` | 48 (`0x30`) |
| 22 | `V4L2_PIX_FMT_SGRBG12` | `0x32314142` | 842088770 | `MM_PIXEL_FORMAT_RAW_SGRBG12` | 47 (`0x2F`) |
| 23 | `V4L2_PIX_FMT_SGBRG12` | `0x32314247` | 842089031 | `MM_PIXEL_FORMAT_RAW_SGBRG12` | 46 (`0x2E`) |
| 24 | `V4L2_PIX_FMT_MJPEG` | `0x47504A4D` | 1196444237 | `MM_PIXEL_FORMAT_BUTT` | 49 (`0x31`) |
| 25 | `V4L2_PIX_FMT_JPEG` | `0x4745504A` | 1195724874 | `MM_PIXEL_FORMAT_BUTT` | 49 (`0x31`) |
| 26 | `V4L2_PIX_FMT_H264` | `0x34363248` | 875967048 | `MM_PIXEL_FORMAT_BUTT` | 49 (`0x31`) |
| — | any other `int` | — | — | `MM_PIXEL_FORMAT_BUTT` | 49 (`0x31`) |

Table size: **26 labelled cases** (three of them compressed→`BUTT`) **+ default**,
covering **24 distinct destination values**. The three compressed cases share one
body but are three separate labels and must remain individually handled (they log
at a non-error level; the default logs at an error level). There is **no**
forward case for any RGB fourcc: `V4L2_PIX_FMT_RGB555`/`RGB32` forward to `BUTT`.

## 5. Reverse map — `map_PIXEL_FORMAT_E_to_V4L2_PIX_FMT`

Exact case order. Every other `PIXEL_FORMAT_E` value hits `default`.

| # | source enumerator | src value | → destination fourcc | dst hex | dst dec |
| --- | --- | --- | --- | --- | --- |
| 1 | `MM_PIXEL_FORMAT_YUV_PLANAR_420` | 20 (`0x14`) | `V4L2_PIX_FMT_YUV420M` | `0x32314D59` | 842091865 |
| 2 | `MM_PIXEL_FORMAT_YVU_PLANAR_420` | 30 (`0x1E`) | `V4L2_PIX_FMT_YVU420M` | `0x31324D59` | 825380185 |
| 3 | `MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420` | 23 (`0x17`) | `V4L2_PIX_FMT_NV12M` | `0x32314D4E` | 842091854 |
| 4 | `MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420` | 32 (`0x20`) | `V4L2_PIX_FMT_NV21M` | `0x31324D4E` | 825380174 |
| 5 | `MM_PIXEL_FORMAT_YUV_SEMIPLANAR_422` | 22 (`0x16`) | `V4L2_PIX_FMT_NV16M` | `0x36314D4E` | 909200718 |
| 6 | `MM_PIXEL_FORMAT_YVU_SEMIPLANAR_422` | 31 (`0x1F`) | `V4L2_PIX_FMT_NV61M` | `0x31364D4E` | 825642318 |
| 7 | `MM_PIXEL_FORMAT_YUYV_PACKAGE_422` | 26 (`0x1A`) | `V4L2_PIX_FMT_YUYV` | `0x56595559` | 1448695129 |
| 8 | `MM_PIXEL_FORMAT_RGB_1555` | 8 (`0x08`) | `V4L2_PIX_FMT_RGB555` | `0x4F424752` | 1329743698 |
| 9 | `MM_PIXEL_FORMAT_RGB_8888` | 10 (`0x0A`) | `V4L2_PIX_FMT_RGB32` | `0x34424752` | 876758866 |
| 10 | `MM_PIXEL_FORMAT_YUV_AW_AFBC` | 33 (`0x21`) | `V4L2_PIX_FMT_FBC` | `0x31324346` | 825377606 |
| 11 | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X` | 34 (`0x22`) | `V4L2_PIX_FMT_LBC_2_0X` | `0x3132434C` | 825377612 |
| 12 | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X` | 35 (`0x23`) | `V4L2_PIX_FMT_LBC_2_5X` | `0x3232434C` | 842154828 |
| 13 | `MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X` | 36 (`0x24`) | `V4L2_PIX_FMT_LBC_1_0X` | `0x3332434C` | 858932044 |
| 14 | `MM_PIXEL_FORMAT_RAW_SBGGR8` | 37 (`0x25`) | `V4L2_PIX_FMT_SBGGR8` | `0x31384142` | 825770306 |
| 15 | `MM_PIXEL_FORMAT_RAW_SRGGB8` | 40 (`0x28`) | `V4L2_PIX_FMT_SRGGB8` | `0x42474752` | 1111967570 |
| 16 | `MM_PIXEL_FORMAT_RAW_SGBRG8` | 38 (`0x26`) | `V4L2_PIX_FMT_SGBRG8` | `0x47524247` | 1196573255 |
| 17 | `MM_PIXEL_FORMAT_RAW_SGRBG8` | 39 (`0x27`) | `V4L2_PIX_FMT_SGRBG8` | `0x47425247` | 1195528775 |
| 18 | `MM_PIXEL_FORMAT_RAW_SBGGR10` | 41 (`0x29`) | `V4L2_PIX_FMT_SBGGR8` | `0x31384142` | 825770306 |
| 19 | `MM_PIXEL_FORMAT_RAW_SRGGB10` | 44 (`0x2C`) | `V4L2_PIX_FMT_SRGGB10` | `0x30314752` | 808535890 |
| 20 | `MM_PIXEL_FORMAT_RAW_SGBRG10` | 42 (`0x2A`) | `V4L2_PIX_FMT_SGBRG10` | `0x30314247` | 808534599 |
| 21 | `MM_PIXEL_FORMAT_RAW_SGRBG10` | 43 (`0x2B`) | `V4L2_PIX_FMT_SGRBG10` | `0x30314142` | 808534338 |
| 22 | `MM_PIXEL_FORMAT_RAW_SBGGR12` | 45 (`0x2D`) | `V4L2_PIX_FMT_SBGGR10` | `0x30314742` | 808535874 |
| 23 | `MM_PIXEL_FORMAT_RAW_SRGGB12` | 48 (`0x30`) | `V4L2_PIX_FMT_SRGGB12` | `0x32314752` | 842090322 |
| 24 | `MM_PIXEL_FORMAT_RAW_SGBRG12` | 46 (`0x2E`) | `V4L2_PIX_FMT_SGBRG12` | `0x32314247` | 842089031 |
| 25 | `MM_PIXEL_FORMAT_RAW_SGRBG12` | 47 (`0x2F`) | `V4L2_PIX_FMT_SGRBG12` | `0x32314142` | 842088770 |
| — | any other `PIXEL_FORMAT_E` | — | `V4L2_PIX_FMT_YUV420M` | `0x32314D59` | 842091865 |

Table size: **25 labelled cases + default**, producing **24 distinct fourcc
values** (`SBGGR8` is the destination of two different sources, rows 14 and 18).
There is **no** case for `MM_PIXEL_FORMAT_BUTT` and no compressed-format case.

## 6. Required asymmetries (reproduce, do not fix)

The forward map is correct; the reverse map deliberately (bug for bug) emits the
8-bit / 10-bit sibling for two 10/12-bit RAW formats. These are **required
behaviour** to reproduce, not defects to repair, because the vendor differential
compares outputs:

1. `MM_PIXEL_FORMAT_RAW_SBGGR10` (41) → `V4L2_PIX_FMT_SBGGR8` (`0x31384142`), not
   `V4L2_PIX_FMT_SBGGR10`.
2. `MM_PIXEL_FORMAT_RAW_SBGGR12` (45) → `V4L2_PIX_FMT_SBGGR10` (`0x30314742`), not
   `V4L2_PIX_FMT_SBGGR12`.

Consequences that the differential must accept (all other RAW pairs round-trip):

- Forward-then-reverse is non-identity for exactly five named fourcc inputs:
  `SBGGR10`, `SBGGR12`, `MJPEG`, `JPEG`, `H264` (the three compressed map to
  `BUTT`, which reverses to `YUV420M`), plus every unknown input.
- Reverse-then-forward is non-identity for exactly two named enum inputs:
  `MM_PIXEL_FORMAT_RAW_SBGGR10` and `MM_PIXEL_FORMAT_RAW_SBGGR12`, plus
  `BUTT` and every enum value with no reverse case (they reverse to `YUV420M`,
  which forwards to `MM_PIXEL_FORMAT_YUV_PLANAR_420`).
- RGB is one-way: `MM_PIXEL_FORMAT_RGB_1555`/`RGB_8888` reverse to the RGB
  fourccs, but no forward case consumes RGB, so a round trip returns `BUTT`.

Report the two RAW pairs to the `yi-mediad` maintainers as a real defect (a
RAW10/12 overlay would request the wrong fourcc); do not patch inside this unit.

## 7. ABI header requirement

The clean tree does **not** currently provide `PIXEL_FORMAT_E`/`MM_PIXEL_FORMAT_*`,
the M-plane/RAW standard fourccs, or the AW-private fourccs. `include/isp_dev_uapi.h`
defines `V4L2_FOURCC` and three `V4L2_PIX_FMT_RGB*` macros only. A clean media
ABI header is therefore **required** before `pixel_format.c` compiles, owned by
the media-utils tier (recommended: a new `include/media_utils_abi.h`, or
extend `isp_dev_uapi.h`). It must provide, pinned as literal numbers:

1. `PIXEL_FORMAT_E` as a **4-byte enum** (see §3.3) — the clean object is a
   media-utils object and must be built with the base CFLAGS, **not** the
   `-fshort-enums` used for the clean ISP-framework objects. `typedef enum { … }
   PIXEL_FORMAT_E;` compiles 4-byte on ARM/x86 by default; never add a
   short-enum flag to this translation unit.
2. The 24 standard fourccs of §3.2.
3. The four AW-private fourccs of §3.1, declared locally (no UAPI header has
   them).
4. The two prototypes of §2 with `extern "C"` guards.

This header is an interoperability fact (enum width, values, symbol names); its
comments/formatting are ours. It must not `#include` a vendor header.

## 8. Edge / unknown behaviour

| Input | Forward result | Reverse result |
| --- | --- | --- |
| Unknown fourcc / enum | `MM_PIXEL_FORMAT_BUTT` (49) | `V4L2_PIX_FMT_YUV420M` |
| `MM_PIXEL_FORMAT_BUTT` (49) | n/a (forward takes fourcc) | `V4L2_PIX_FMT_YUV420M` |
| `MM_PIXEL_FORMAT_UYVY_PACKAGE_422` (25) / `VYUY` (27) / A422 / A444 / planar 422/444 | n/a | `V4L2_PIX_FMT_YUV420M` (no case) |
| Compressed fourccs (`MJPEG`/`JPEG`/`H264`) | `MM_PIXEL_FORMAT_BUTT` (49) | n/a |

Both functions are total and never signal an error through a return code. The
default branch logs, but the log is not part of the contract. A negative `int`
fourcc (bit 31 set) is compared as-is; none of the constants in this unit have
bit 31 set, so such inputs fall to default on both sides. The reverse function's
parameter is a 4-byte enum, so any 32-bit value is a valid input.

## 9. Host differential plan

Link the clean object and the vendor object into one binary and compare the two
returns for every input. Reuse the existing differential shape (the private
differential harness): a `media_utils/` subdir with `vendor_tu`, `clean_tu`,
`diff_vfmt.c`, `Makefile`, `run.sh`; log to the withheld golden vector
`diff_vfmt.txt`.
Vendor and clean TUs must be built with the **same** enum convention (4-byte) and
the same fourcc numeric values.

Driver:

1. **Exhaustive forward sweep.** For `bits = 0 … 0xFFFFFFFF` (use
   `do { … } while (bits++ != 0xFFFFFFFFu)` to avoid the wrap bug), call
   `vendor_f((int)bits)` and `clean_f((int)bits)`, compare as `int`. Count
   mismatches, print the first 16 on mismatch, end with
   `== vfmt forward summary: N inputs, M with mismatches ==`. Expected
   **M = 0**.
2. **Exhaustive reverse sweep.** Same loop over all 2^32 values as
   `PIXEL_FORMAT_E`, comparing `int` returns. Expected **M = 0**.
3. **Named-vector assertions.** Assert both sides against the 26 forward rows
   (§4) and 25 reverse rows (§5) by symbolic name, independent of numeric
   comparison, so a table transposition that happens to be symmetric is caught.
4. **QUIRK assertions.** `reverse(RAW_SBGGR10) == V4L2_PIX_FMT_SBGGR8`,
   `reverse(RAW_SBGGR12) == V4L2_PIX_FMT_SBGGR10`, and the matching forward
   values `RAW_SBGGR10`/`RAW_SBGGR12`. A differential that reports 0 on the
   exhaustive sweep but fails here indicates a shared wrong constant.
5. **Round-trip census.** Count forward∘reverse and reverse∘forward identity
   passes; assert the exact non-identity sets of §6 (5 named fourccs, 2 named
   enums, all unknowns).
6. **Sensitivity self-test.** Perturb one clean table entry (e.g. swap two RAW
   destinations), re-run, and require the harness to report >0 mismatches and
   exit non-zero. A pass must mean the harness actually compared values.

Side effects: both TUs call a logging function on their default branches
(forward also on the compressed cases). Provide a no-op or capture-only stub in
the harness; do **not** compare log text. The comparison is return values only.

If the vendor `.c` cannot be compiled for the host against the SDK headers,
follow the existing qemu-arm pattern: build both TUs for the target and run the
differential under `qemu-arm`, exactly as the other
module harnesses do.

## 10. Ambiguities / hazards

1. **Quirk reproduction vs fix.** The two RAW reverse entries are wrong but
   required. Any "correction" makes the vendor differential fail and changes
   live OSD behaviour. Fix belongs in `yi-mediad`, not here.
2. **Log text is not portable.** The default/compressed branches log; the text
   is vendor expression. The clean unit may log its own message (or none), but
   no differential may compare log strings.
3. **`int` vs `uint32_t` fourcc.** The forward parameter is `int`. Keep the
   signed type for ABI parity; compare bit patterns. All constants here are
   below 2^31, so the sign bit never appears in a real case.
4. **Enum contiguity.** `PIXEL_FORMAT_E` has exactly one explicit initialiser
   (`RGB_1BPP = 0`); every other value is sequential. Do not introduce gaps or
   renumber; the numeric values in §3.3 are ABI.
5. **`-fshort-enums` must not reach this TU.** Media-utils objects are 4-byte
   enum (base CFLAGS). The clean ISP-framework flag (`-fshort-enums
   -DISP521_RTOS_ALGO=1`) applies to other objects only.
6. **Private fourcc name collisions.** `V4L2_PIX_FMT_LBC_2_0X` = `'L','C','2','1'`
   is not kernel UAPI; ensure the clean ABI header defines it once and no other
   header redefines `V4L2_PIX_FMT_FBC`/`LBC_*` with a different value.
7. **One-way RGB and compressed paths.** RGB is only MM→V4L2; compressed
   (`MJPEG`/`JPEG`/`H264`) is only V4L2→`BUTT`. Do not "balance" the tables.
8. **`BUTT` reverse default.** `MM_PIXEL_FORMAT_BUTT` has no reverse case and
   silently becomes `YUV420M`; a caller that round-trips a compressed fourcc
   through `BUTT` will get `YUV420M`, not the original fourcc. This is expected.
9. **Dead exports.** Do not emit the five dead symbols; the clean object replaces
   the vendor object, and re-adding them from this unit would pull in unrelated
   AW codec/field policy. Their reference count must stay zero.
10. **Host header drift.** The 24 standard fourccs are public UAPI and stable,
    but pinning the literals in the clean ABI header (rather than including a
    host kernel header) keeps the target and host harness bit-identical.
