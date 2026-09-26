<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# media-utils phase 2 — `media_helpers` (`MPP_CHN_S` copy)

Behaviour-only spec. Source unit: multimedia common helpers. **One** exported
symbol is live (`copy_MPP_CHN_S`); the other eleven (ISE/UVC/ADEC/VENC attribute
copies and codec-map switches) have no caller in the current link and are
**omitted** (phase 1).

## 1. ABI

`MPP_CHN_S` is carried by the shared clean ABI:
`{ MOD_ID_E mModId; int mDevId; int mChnId; }` with `MOD_ID_E` a 4-byte enum.
The clean unit is built with default 4-byte enums.

Exported:

```c
ERRORTYPE copy_MPP_CHN_S(MPP_CHN_S *pDst, MPP_CHN_S *pSrc);
```

## 2. Behaviour

`copy_MPP_CHN_S(pDst, pSrc)` is a whole-struct assignment `*pDst = *pSrc` and
returns `SUCCESS` (0). It performs no validation; NULL arguments are undefined
behaviour (matching the reference, which dereferences unconditionally).

## 3. Omitted (phase 1)

The channel/group attribute copies (ADEC, VENC, ISE group, ISE channel and UVC);
the payload-type ↔ codec-format mapping switches (video, audio and encoder
variants plus their inverses); and the VENC-channel bitrate query.

## 4. Verification plan

- Host unit test: copy a fully populated descriptor (including a non-zero
  sentinel pattern) and assert every field, and `SUCCESS`.
- Differential: link vendor vs clean under qemu-arm, copy a set of distinct
  descriptors and compare all three fields plus the return value.
