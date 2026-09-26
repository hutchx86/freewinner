<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# media_utils / frame_size — Behaviour Specification

Spec-team artefact (may read the vendor source). Observable behaviour and
interoperability facts only: no vendor comments, string literals, or
implementation structure. The entry-point name, type tags and field names below
are the required interoperability surface (the clean unit is called by existing
VI glue through them and must present the same ABI). See
`isp/docs/provenance.md` for the full provenance picture.

Clean tree target file: `src/utils/frame_size.c`
(`frame_size`). Target ABI: 32-bit ARM EABI5,
4-byte enums, no `-fshort-enums`.

---

## 1. Required interoperability surface

One exported function, C linkage:

```c
typedef struct VideoFrameBufferSizeInfo {
    int mYSize;
    int mUSize;
    int mVSize;
} VideoFrameBufferSizeInfo;

ERRORTYPE getVideoFrameBufferSizeInfo(VIDEO_FRAME_INFO_S *pFrame,
                                      VideoFrameBufferSizeInfo *pSizeInfo);
```

- `ERRORTYPE` is `typedef int`; `SUCCESS` is `0`, `FAILURE` is `(-1)`.
- Return value is 0 on a handled format, -1 on any rejection.
- The prototype must be visible to callers with exactly these names/signature
  (the consumer is compiled against the same declaration). `PARAM_IN` /
  `PARAM_OUT` are empty macros and have no ABI effect.
- The clean implementation may decompose internally however it likes; only the
  symbol name, signature, and observable behaviour are fixed.

Inputs read (exact paths and types):

| Path | Type | Meaning |
| --- | --- | --- |
| `pFrame->VFrame.mPixelFormat` | `PIXEL_FORMAT_E` | selects the policy |
| `pFrame->VFrame.mWidth` | `unsigned int` | plane width, pixels |
| `pFrame->VFrame.mHeight` | `unsigned int` | plane height, pixels |
| `pFrame->VFrame.mStride[0]` | `unsigned int` | AW only: Y-plane byte size |
| `pFrame->VFrame.mStride[1]` | `unsigned int` | AW only: U-plane byte size |
| `pFrame->VFrame.mStride[2]` | `unsigned int` | AW only: V-plane byte size |

No other field is read. `mField`, `mVideoFormat`, `mCompressMode`, `mPhyAddr`,
`mpVirAddr`, `mHeader*` and `mId` are **not** consulted.

Outputs written (all three always written together on success):

| Offset | Type | Field | Meaning |
| --- | --- | --- | --- |
| 0 | `int` | `mYSize` | Y (or sole) plane byte count |
| 4 | `int` | `mUSize` | U/UV plane byte count, else 0 |
| 8 | `int` | `mVSize` | V plane byte count, else 0 |

---

## 2. ABI (deployed 32-bit target)

Measurement method: no musl cross toolchain and no `qemu-arm` are present on the
build host. The layout below was measured by compiling the **actual vendor
headers** (`mm_comm_video.h` and the frame-size header) with the available
32-bit ARM EABI cross compiler (`arm-buildroot-linux-uclibcgnueabi-gcc 6.4.0`,
default 4-byte enums), then reading the emitted debug type information. ARM EABI
(AAPCS) layout is independent of the C
library and identical across the uclibc/musl toolchains and across ARMv5/v7 for
this struct; an independent hand computation of the target layout agrees.

### 2.1 Enum sizes (4 bytes each, required)

| Type | Size | Note |
| --- | --- | --- |
| `PIXEL_FORMAT_E` | 4 | confirmed by the object's type information |
| `VIDEO_FIELD_E` | 4 | |
| `VIDEO_FORMAT_E` | 4 | |
| `COMPRESS_MODE_E` | 4 | |

A clean ABI header must declare these enums as the default 4-byte enum. Do not
build this unit with `-fshort-enums`.

### 2.2 `PIXEL_FORMAT_E` values handled / rejected

Values are the enumerated sequence from `MM_PIXEL_FORMAT_RGB_1BPP = 0`; only the
ones the function acts on are listed. All are exact ABI constants.

| Value | Constant | Policy group |
| --- | --- | --- |
| 20 | `MM_PIXEL_FORMAT_YUV_PLANAR_420` | planar 420 |
| 23 | `MM_PIXEL_FORMAT_YUV_SEMIPLANAR_420` | semi-planar 420 |
| 30 | `MM_PIXEL_FORMAT_YVU_PLANAR_420` | planar 420 |
| 32 | `MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420` | semi-planar 420 |
| 33 | `MM_PIXEL_FORMAT_YUV_AW_AFBC` | AW AFBC/LBC |
| 34 | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_0X` | AW AFBC/LBC |
| 35 | `MM_PIXEL_FORMAT_YUV_AW_LBC_2_5X` | AW AFBC/LBC |
| 36 | `MM_PIXEL_FORMAT_YUV_AW_LBC_1_0X` | AW AFBC/LBC |
| 37 | `MM_PIXEL_FORMAT_RAW_SBGGR8` | RAW 8 |
| 38 | `MM_PIXEL_FORMAT_RAW_SGBRG8` | RAW 8 |
| 39 | `MM_PIXEL_FORMAT_RAW_SGRBG8` | RAW 8 |
| 40 | `MM_PIXEL_FORMAT_RAW_SRGGB8` | RAW 8 |
| 41 | `MM_PIXEL_FORMAT_RAW_SBGGR10` | RAW 10 |
| 42 | `MM_PIXEL_FORMAT_RAW_SGBRG10` | RAW 10 |
| 43 | `MM_PIXEL_FORMAT_RAW_SGRBG10` | RAW 10 |
| 44 | `MM_PIXEL_FORMAT_RAW_SRGGB10` | RAW 10 |
| 45 | `MM_PIXEL_FORMAT_RAW_SBGGR12` | RAW 12 |
| 46 | `MM_PIXEL_FORMAT_RAW_SGBRG12` | RAW 12 |
| 47 | `MM_PIXEL_FORMAT_RAW_SGRBG12` | RAW 12 |
| 48 | `MM_PIXEL_FORMAT_RAW_SRGGB12` | RAW 12 |
| 49 | `MM_PIXEL_FORMAT_BUTT` | rejected (default) |

Everything else — including the RGB/`RGB_BAYER_*` values 0–19, the YUV values
21,22,24–29,31 and any value ≥ 49 — falls to the default and is rejected. The
twelve `MM_PIXEL_FORMAT_RAW_*` values are handled; the earlier
`MM_PIXEL_FORMAT_RGB_BAYER_*` values (12–16) are **not**.

### 2.3 `VIDEO_FRAME_S`

Size **144** bytes, alignment **8** (the two `uint64_t` fields force 8-byte
alignment; the tail is padded 140→144). On ILP32 the fields are:

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 4 | `unsigned int` | `mWidth` |
| 4 | 4 | `unsigned int` | `mHeight` |
| 8 | 4 | `VIDEO_FIELD_E` | `mField` |
| 12 | 4 | `PIXEL_FORMAT_E` | `mPixelFormat` |
| 16 | 4 | `VIDEO_FORMAT_E` | `mVideoFormat` |
| 20 | 4 | `COMPRESS_MODE_E` | `mCompressMode` |
| 24 | 12 | `unsigned int[3]` | `mPhyAddr` |
| 36 | 12 | `void *[3]` | `mpVirAddr` |
| 48 | 12 | `unsigned int[3]` | `mStride` |
| 60 | 12 | `unsigned int[3]` | `mHeaderPhyAddr` |
| 72 | 12 | `void *[3]` | `mpHeaderVirAddr` |
| 84 | 12 | `unsigned int[3]` | `mHeaderStride` |
| 96 | 2 | `short` | `mOffsetTop` |
| 98 | 2 | `short` | `mOffsetBottom` |
| 100 | 2 | `short` | `mOffsetLeft` |
| 102 | 2 | `short` | `mOffsetRight` |
| 104 | 8 | `uint64_t` | `mpts` |
| 112 | 4 | `unsigned int` | `mExposureTime` |
| 116 | 4 | `unsigned int` | `mFramecnt` |
| 120 | 4 | `int` | `mEnvLV` |
| 124 | 4 | `unsigned int` | `mWhoSetFlag` |
| 128 | 8 | `uint64_t` | `mFlagPts` |
| 136 | 4 | `unsigned int` | `mFrmFlag` |

### 2.4 `VIDEO_FRAME_INFO_S`

Size **152**, alignment **8**:

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 144 | `VIDEO_FRAME_S` | `VFrame` |
| 144 | 4 | `unsigned int` | `mId` |
| 148 | 4 | — | tail padding |

### 2.5 `VideoFrameBufferSizeInfo`

Size **12**, alignment **4**: `mYSize` @0, `mUSize` @4, `mVSize` @8, each `int`.

### 2.6 Clean-tree gap

The clean tree (`include/`, `src/`) does
**not** currently define `VIDEO_FRAME_S`, `VIDEO_FRAME_INFO_S`, `PIXEL_FORMAT_E`,
`VIDEO_FIELD_E`, `VIDEO_FORMAT_E`, `COMPRESS_MODE_E` or
`VideoFrameBufferSizeInfo`. The implementation must therefore either

1. reuse the SDK interface headers `mm_comm_video.h` + the frame-size header
   verbatim (the approach recommended by the media-utils plan: no private
   shim is needed), or
2. add a clean ABI header that reproduces exactly the sizes, offsets and enum
   values in §2.1–2.5 (the approach the generated `include/fwi_*.h` headers
   take).

Whichever is chosen, the union of the used declarations must match §2 exactly;
the unit itself must not be compiled `-fshort-enums`.

---

## 3. Behaviour

```
if (pFrame == NULL || pSizeInfo == NULL)  -> log; return FAILURE
switch (pFrame->VFrame.mPixelFormat):
    group match -> compute Y/U/V, write all three, return SUCCESS
    default     -> log format value; return FAILURE (output untouched)
```

`W = pFrame->VFrame.mWidth`, `H = pFrame->VFrame.mHeight`. Every product is
computed in 32-bit `unsigned int` and the result is stored into a signed `int`
field. Division is unsigned integer division (truncating toward zero).

| Format group | `mYSize` | `mUSize` | `mVSize` |
| --- | --- | --- | --- |
| semi-planar 420 (23, 32) | `W*H` | `W*H/2` | `0` |
| planar 420 (20, 30) | `W*H` | `W*H/4` | `W*H/4` |
| AW AFBC/LBC (33–36) | `mStride[0]` | `mStride[1]` | `mStride[2]` |
| RAW 8 (37–40) | `W*H` | `0` | `0` |
| RAW 10 (41–44) | `W*H*10/8` | `0` | `0` |
| RAW 12 (45–48) | `W*H*12/8` | `0` | `0` |
| default (any other) | not written | not written | not written |

Notes on the arithmetic:

- For semi-planar the chroma pair is one interleaved plane; it is reported as
  `mUSize`, and `mVSize` is 0.
- For planar 420 both chroma planes are quarter-size and both are reported.
- RAW 10/12 expressions are evaluated left-to-right as `(W*H*10)/8` and
  `(W*H*12)/8`; do not pre-divide. Equivalent to `floor(W*H*5/4)` and
  `floor(W*H*3/2)` respectively.
- The AW formats do **not** use `W`/`H` at all. For those four formats the three
  `mStride[]` entries are repurposed by the caller as the **plane byte sizes**
  (Y, U, V) of the compressed frame, and the function returns them verbatim
  into `mYSize`/`mUSize`/`mVSize` respectively. No fraction, no clipping, no
  zeroing.
- Success writes all three fields even when two of them are 0.

---

## 4. Return values, null/edge behaviour, quirks

| Condition | Return | Output struct |
| --- | --- | --- |
| `pFrame == NULL` | `FAILURE` (-1) | untouched; diagnostic logged |
| `pSizeInfo == NULL` | `FAILURE` (-1) | not addressable; diagnostic logged |
| both non-NULL, handled format | `SUCCESS` (0) | all three fields written |
| both non-NULL, unhandled format | `FAILURE` (-1) | untouched; diagnostic logged |

- Null check is `pFrame == NULL || pSizeInfo == NULL`; either one rejects.
- On `FAILURE` the output struct is never partially written; a caller that
  pre-fills it and checks the return can rely on it being unchanged.
- `W == 0` or `H == 0` is not validated: the product is 0 and the call still
  returns `SUCCESS` for a handled format.
- `mStride[]` values are not validated, aligned, or range-checked; arbitrary
  values (including 0 and values with the top bit set) are returned verbatim.
- There is no dependence on `mCompressMode`: an AFBC/LBC frame is recognised
  only by its `mPixelFormat` value. An implementation must **not** gate the AW
  branch on `COMPRESS_MODE_*`, and must not infer `mStride[]` semantics from it.
- The failure paths emit a diagnostic log (the only side effect besides the
  output writes). The log text/level is not part of the contract; a host
  differential must stub the logger.
- Vendor quirk (caller side, not this function): the live callers ignore the
  return value and immediately use the output struct for `fwrite` lengths, so a
  rejected format leaves them reading an uninitialised local. The function must
  still leave the struct untouched on failure; do not "helpfully" zero it.

---

## 5. Host differential plan

Two layers, because target-offset correctness and behaviour are different
questions.

**Harness.** Compile the vendor function and the clean function into one test
binary under two distinct harness-local symbol names, both compiled against the
**same** struct/enum definitions, 4-byte enums. Stub the vendor logger to a no-op so the
log path is harmless. Pre-fill the output struct with a sentinel (e.g.
`mYSize=mUSize=mVSize=0xA5A5A5A5`) before each call so "untouched" is visible.
This behaves identically on a 64-bit host build (both sides share one layout) and
on the ARM32 target under `qemu-arm`; the ARM32 run additionally exercises the
exact target offsets. Preferred: ARM32 under `qemu-arm` as the other
differentials do.

**Matrix** — call both functions on identical inputs and compare the return value
plus all three `int` outputs, reporting the first differing field:

| Axis | Values |
| --- | --- |
| format | 20, 23, 30, 32; 33, 34, 35, 36; 37–48 (all 12 RAW); plus rejected probes 0, 12, 16, 21, 22, 24, 29, 31, 49, 0x100, 0xFFFFFFFF |
| dimensions | (0,0), (1,1), (2,2), (13,7), (16,16), (640,480), (1920,1080), (2592,1520), (1920,1), (1,1080), (65536,65536), (100000,100000) |
| strides | AW: (0x11111111,0x22222222,0x33333333), (0,0,0), (0xFFFFFFFF,0x80000000,0x00000001); non-AW: (0xDEADBEEF,0xCAFEBABE,0x12345678) |

- Non-AW dimensions × a non-zero stride triple prove `mStride[]` is ignored for
  every non-AW group.
- The large dimensions pin the 32-bit truncation/overflow rounding of the RAW
  10/12 and `W*H` products (e.g. `65536*65536 = 2^32 -> 0` as `unsigned int`,
  stored into `int`).
- Odd dimensions (13×7) pin the integer-division truncation of `/2`, `/4`,
  `/8`.
- Null cases are run separately and compared on return value only:
  `(NULL, &out)`, `(&in, NULL)`, `(NULL, NULL)`; each must yield `FAILURE` and
  leave the sentinel intact. Guard the logger stub so vendor does not crash.

**Target ABI check (separate).** The §2 sizes/offsets were measured once from the
vendor headers; the clean ABI header/declarations should be re-measured the same
way (from the type information of a 32-bit ARM EABI object) and the two tables
compared exactly before accepting the unit.

Pass criterion: zero mismatches in return value and all three output fields over
the full matrix, and an identical §2 layout table.

---

## 6. Ambiguities and hazards

1. **`mStride[]` meaning for AW is a caller convention.** The function does not
   compute or verify compressed plane sizes; it forwards the three values. A
   caller that stores pixel pitch rather than byte size gets a wrong-but-
   faithful answer. Reproduce the pass-through exactly.
2. **`mCompressMode` is irrelevant.** Do not key the AFBC/LBC branch off it.
3. **32-bit arithmetic is load-bearing.** Products are `unsigned int` and stored
   into `int`; use `int`/`unsigned int` (not `long`/`int64_t`) so overflow and
   truncation match the target.
4. **Evaluation order** of `W*H*10/8` / `W*H*12/8` (multiply before divide);
   pre-dividing changes results for odd sizes.
5. **Only 20 formats are handled.** The RGB and `RGB_BAYER_*` families, and all
   YUV formats except the four 420 planar/semi-planar entries, are rejected.
6. **4-byte enums are ABI.** Compiling the clean unit `-fshort-enums` breaks
   `mPixelFormat` offsets and the switch constant widths.
7. **On a 64-bit host the target offsets do not hold** (`mpVirAddr`/`mStride`
   shift). The behavioural differential is valid only if both functions share
   one struct definition; the §2 table is a 32-bit-target fact, not a host fact.
8. **`VIDEO_FRAME_S` has a 4-byte tail pad** (144 not 140) due to 8-byte
   `uint64_t` alignment. A clean ABI header that drops the padding breaks the
   `VIDEO_FRAME_INFO_S` size and every enclosing struct.
9. **Callers ignore the return value.** The untouched-on-failure guarantee is the
   only protection; do not zero or partially fill the output on rejection.
10. **No status for "uninitialised stride".** AW formats with zero strides return
    `SUCCESS` and zero sizes; there is no advisory path.
