<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# media-utils phase 2 — `bitmap` (`BITMAP_S` size helper)

Behaviour-only spec. Source unit: the platform bitmap helper. One exported
symbol is live (`BITMAP_S_GetdataSize`); the flip helper is dead in the current
link and is **omitted** (phase 1).

## 1. ABI

`BITMAP_S` is carried by the shared clean ABI (`include/media_utils_abi.h`):
`{ PIXEL_FORMAT_E mPixelFormat; unsigned int mWidth; unsigned int mHeight;
void *mpData; }` — 4-byte enum, `mpData` last. The clean unit is built with the
base flags (default 4-byte enums), never `-fshort-enums`.

Exported:

```c
int BITMAP_S_GetdataSize(const BITMAP_S *pBitmap);
```

## 2. Behaviour

`BITMAP_S_GetdataSize(p)` returns the payload byte count for the pixel format:

| Condition | Return |
| --- | --- |
| `p == NULL` | `0` |
| `p->mPixelFormat == MM_PIXEL_FORMAT_RGB_1555` | `mWidth * mHeight * 2` |
| `p->mPixelFormat == MM_PIXEL_FORMAT_RGB_8888` | `mWidth * mHeight * 4` |
| any other format | `0` |

- The product is computed in 32-bit unsigned arithmetic and returned as a signed
  `int` (so a large image wraps; reproduce that, do not widen).
- NULL and unknown-format cases return `0` (the unknown case also emits a
  diagnostic through the shared `media_utils_log_error` seam; the message text is
  not part of the contract).
- `mWidth`/`mHeight` are used exactly as stored; no alignment or stride is
  applied.

## 3. Omitted (phase 1)

The flip helper (horizontal/vertical 16/32-bpp pixel swap) has no caller in
the current link and is not reimplemented.

## 4. Verification plan

- Host unit test: NULL, both supported formats, one unsupported format, a
  zero-dimension bitmap, and an overflow-width case.
- Differential: link the vendor object and the clean object under qemu-arm, drive
  an exhaustive format/value sweep, compare the returned size (the two are
  deterministic and must be byte-equal).
