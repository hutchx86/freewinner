<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Credits — freewinner

## Provenance

Both subprojects are clean-room work. Behaviour-only specifications were written
from observing the *deployed* on-camera binaries; the implementations were
written from those specifications; and each module was then verified against the
deployed object under qemu-arm. No Allwinner implementation code or comments are
reproduced. The register tier's provenance caveat — an earlier draft written with
direct access to the vendor binary, later re-produced from a behaviour-only spec
and re-verified — is recorded in `isp/docs/provenance.md`.

The interoperability ABI surfaces (the generated `isp/include/fwi_*.h` records,
the UAPI headers below, and the codec's VE and encoder ABI declarations) restate
the layouts, enum values and entry points required to match the deployed
interface; those are interface facts, not copied implementation. The ISP
framework and media utilities (`isp/src/framework/`, `isp/src/utils/`) are the
r1 clean-room re-implementation, which replaced an earlier tier that overlapped
the vendor source (see `isp/docs/provenance.md`).

Fuller statements: `isp/CREDITS.md`, `codec/CREDITS.md` and
`isp/docs/provenance.md`.

## Third-party components

- **FAAC** (Freeware Advanced Audio Coder), Copyright (C) 1999-2026 the FAAC
  authors — GNU Lesser General Public License, version 2.1 or later. Used by the
  codec's AAC path as a separate, unmodified library: not vendored, linked from a
  builder-supplied `FAAC_DIR`. LGPL-2.1-or-later is compatible with this
  repository's AGPL-3.0-only; its copyright and licence must accompany any binary
  distribution (see FAAC's `COPYING`).
- **Linux kernel UAPI interface definitions.** Linux UAPI is *reproduced* in two
  headers that restate interface facts required to match the deployed ABI:
  `isp/include/isp_dev_uapi.h` (the V4L2 and media-controller structures, enums,
  ioctl numbers and `MEDIA_*` flags) and `isp/include/media_utils_abi.h` (the
  V4L2 fourcc macros and their values). They are reproduced under the terms the
  UAPI carries — the V4L2 macro and type declarations under **BSD-3-Clause**,
  the media-controller declarations under **GPL-2.0 WITH Linux-syscall-note** —
  and both notices are given below: the V4L2 notice in full, including its
  conditions and disclaimer, and the media-controller one as its SPDX
  identifier, copyright holder and the Linux-syscall-note grant, with the
  GPL-2.0 body referenced rather than reproduced because no GPL-2.0 program
  text is copied.
- **Intrusive-list helpers — our own implementation.** The intrusive-list node
  layout and the list operations in `isp/include/media_utils_abi.h`
  (`LIST_HEAD_INIT`, `INIT_LIST_HEAD`, `list_add`, `list_add_tail`, `list_del`,
  `list_empty`, the iteration macros and `container_of`) are this project's own
  expression of the same interface. No line of it that carries expression is
  byte-identical to any upstream list header; what does coincide is
  expression-free scaffolding only — blank lines, bare braces, comment and
  `#endif` markers, and declaration headers such as `static inline int`,
  `struct list_head {` and `#define container_of(ptr, type, member) \` — while
  every operation body is written independently here. Beyond that scaffolding,
  the only shared text is the identifiers the interface itself fixes (the
  `list_empty`/`list_for_each` names and the struct and field names), which carry
  no protectable expression. Separately, two
  source files
  `#include <linux/media-bus-format.h>` directly (it is *included*, not
  reproduced, so no notice is required for it). The build links the C standard
  library; `pthread` where the utility tests need it (`isp/mk/systime.mk`,
  `semaphore.mk`, `msgqueue.mk`, `frame_pool.mk`, `framework_isp.mk` and
  `codec/mk/ve.mk`); `libm`; and, on the integration shim's ARM tail, the GCC
  toolchain runtime `libgcc_eh` (`isp/shim/integration/Makefile:38`). When a
  FAAC tree is supplied it is compiled in
  from the builder's own copy (`codec/mk/aac.mk`), never vendored.

  **Upstream notice A — Linux V4L2 UAPI** (`include/uapi/linux/videodev2.h`;
  SPDX `((GPL-2.0+ WITH Linux-syscall-note) OR BSD-3-Clause)`, used here under
  `BSD-3-Clause`):

  Copyright (C) 1999-2012 the contributors

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
  3. The names of its contributors may not be used to endorse or promote products
     derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  **Upstream notice B — Linux media-controller UAPI**
  (`include/uapi/linux/media.h`; SPDX `GPL-2.0 WITH Linux-syscall-note`):

  Copyright (C) 2010 Nokia Corporation

  The media-controller structures, enums, `MEDIA_*` flags and `MEDIA_IOC_*`
  request numbers reproduced in `isp/include/isp_dev_uapi.h` are user-space
  interface definitions covered by that file's Linux-syscall-note exception, not
  by the BSD option above. They are reproduced as interface facts required to
  match the deployed ABI. No GPL-2.0 program text — as distinct from those
  interface declarations — is copied, so the GPL-2.0 body itself is referenced
  rather than reproduced. The exception, reproduced from the kernel's `COPYING`,
  reads:

  > NOTE! This copyright does *not* cover user programs that use kernel
  > services by normal system calls - this is merely considered normal use
  > of the kernel, and does *not* fall under the heading of "derived work".
  > Also note that the GPL below is copyrighted by the Free Software
  > Foundation, but the instance of code that it refers to (the Linux
  > kernel) is copyrighted by me and others who actually wrote it.
  >
  > Also note that the only valid version of the GPL as far as the kernel
  > is concerned is _this_ particular version of the license (ie v2, not
  > v2.2 or v3.x or whatever), unless explicitly otherwise stated.
  >
  > Linus Torvalds

  The GPL-2.0 body itself is at
  <https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>.

Note: neither AAC (including its use) nor H.264/AVC is royalty-free. The FAAC
project warns that patent royalties may apply to its use; that risk is unaffected
by this notice and is a distribution-time decision.

## Acknowledgements

- The framework-facing ABI records (`isp/include/fwi_*.h`) are generated in this
  project's own naming and format from layout facts of the deployed interface, so
  the tree builds against this repository alone; no Allwinner SDK header is
  included or required.

## References

These are works consulted for orientation only. **Nothing was copied from any of
them** — the specifications are behaviour-only and the implementations are
independently written (see the provenance statements above and in
`codec/docs/provenance.md`).

- **linux-sunxi** — public VE register documentation (`VE Register guide`),
  consulted as an orientation map for the video-engine register set.
- **Community Cedar H.264 encoders** — the Jemk proof-of-concept, ubobrov's H3
  port, the `libv` cedar encoder, and
  `carroarmato0/allwinner-cedar-tools`. These target older VE revisions; they
  were treated as prior art and revalidated against V831 behaviour.
- **The vendor V831 `cedar_ve.c` kernel driver** — Allwinner's own
  GPL-2.0-or-later kernel source, analysed read-only as the deployed reference
  for the vendor encoder IRQ and register semantics. It is not reproduced here
  and is not part of this repository.

All product names, trademarks and registered trademarks are the property of
their respective owners and are used only for identification and
interoperability.
