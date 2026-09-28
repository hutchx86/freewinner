<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 freewinner contributors -->

# fenc_: what is deliberately not implemented

This package is a drop-in for the SDK's encoder framework `vencoder.c`. It
defines the 23 `VideoEnc*` entry points the middleware links against and
nothing else. The following are left out on purpose; each is
in the spec's scope-and-non-goals list.

| Area | Behaviour here |
| --- | --- |
| JPEG and VP8 codecs | `VideoEncCreate` accepts `FWM_VENC_CODEC_H264`, `FWM_VENC_CODEC_H264_VER2` and `FWM_VENC_CODEC_H265`; the remaining codec types are rejected with NULL before anything is allocated. The clean codec's error-returning device stubs (`video_encoder_jpeg`, and a VP8 slot if present) are never opened by this package. |
| Still-JPEG path (`AWJpecEnc`) and its ION import/export helpers | Not provided. Nothing in the media daemon calls it, and no ION symbol is referenced from this package. |
| Create-time SDK version banner | Not emitted. The clean implementation prints no vendor version text. |
| Frame-rate / time accounting facility | Not reimplemented. The original's timing log was behind a compile-time switch that is off in this build; this package has no timing code and no `gettimeofday` use. |
| Codec-internal behaviour (register programming, rate control, GOP, headers, encoder-internal ISP, bit-writer) | Owned by the codec device, not this package. |
| `FWM_VENC_PARAM_*` values the caller never uses | Not interpreted here; every index and pointer is forwarded to the device unchanged, which decides. |

The framework performs no waits, sleeps or retries, opens no device node, and
reads no input file. The only synchronisation it performs is the video-engine
lock around the create-time IC-version read and around each device encode.
