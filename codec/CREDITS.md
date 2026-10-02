<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Credits — freecodec

An independent, clean implementation of the Allwinner codec blobs the Yi camera
media daemon (`mediad`) linked: the AAC-LC encoder and the H.264 hardware
encoder driver, for sun8iw19p1 / V831-class hardware.

Copyright (C) 2026 freewinner contributors.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, version 3 of the License. See the LICENSE file for the full
text.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

## Provenance

The interfaces and behaviour were recovered by reverse engineering the
*deployed* on-camera binaries (`libaacenc.a`, `libvenc_codec.a`, `libVE.a`,
ARMv7 EABI5) for interoperability with camera hardware we own. It was NOT
written from Allwinner source code, and no Allwinner source code is included.

No vendor binary is committed to this repository. The differential harnesses
live in the private analysis workspace, which also holds the withheld golden
vectors and the analysis projects built from the deployed objects; that
workspace is gitignored and is never published.

The VE register interface is additionally corroborated by public independent
implementations (jemk/cedrus, uboborov/h264_encoder_H3, linux-sunxi.org); see
`docs/provenance.md` §"Public prior art".

## Third-party components

FAAC (Freeware Advanced Audio Coder), Copyright (C) 1999-2026 the FAAC authors.
Licensed under the GNU Lesser General Public License, version 2.1 or later.
FAAC is used as a separate, unmodified library; it is not part of this
repository and is linked, not copied. LGPL-2.1-or-later is compatible with this
repository's AGPL-3.0-only. Its copyright and licence must accompany any binary
distribution; see FAAC's `COPYING`.

Note: neither AAC (including its use) nor H.264/AVC is royalty-free. The FAAC
project warns that patent royalties may apply to its use; that risk is
unaffected by this notice and is a distribution-time decision.

Trademarks and related names are the property of their respective owners and
are used only for identification and interoperability.
