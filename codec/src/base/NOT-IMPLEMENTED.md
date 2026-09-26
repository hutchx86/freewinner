<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# libvenc_base: what is deliberately not implemented

This library is a drop-in replacement for the platform's `libvenc_base.so`. It
exports the 41 functions in `include/freecodec/venc_base_abi.h`: every one the
media daemon and the freecodec H.264 encoder use. The following are left out on
purpose.

| Area | Behaviour here |
| --- | --- |
| Non-IOMMU physical addressing (real physical addresses through the ION custom command) | Only IOMMU mode is supported. Engine addresses always come from the video engine's IOMMU mapping. |
| Secure / protected-memory allocation, trusted-environment addresses | The `palloc_secure` slot returns NULL; `setup` and `shutdown` return -1. |
| Length-prefixed NAL output mode of the bitstream ring | Not provided. The ring carries the engine's Annex-B output as written. |
| Allocator `close()` with buffers still allocated | Only forgets their records and does not free them. Callers free what they allocate. |
| Other symbols of the platform library that no caller here uses | Not exported. This covers the video-engine lock/reset/interrupt helpers, register-dump helpers, DRAM-type and performance helpers, message-queue and system-info utilities, the peek-only bitstream query, and the ION mmap/unmap/TEE helpers. |

A binary that needs any of these fails to load against this library with an
unresolved-symbol error. It never falls back silently.
