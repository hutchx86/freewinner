/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Encoder device interface (spec r2/05 part B): a device is one
 * `fwm_venc_device_t` table (declared with the part-B records in venc_types.h).
 * Single include root for a device: also pulls in ve_iface.h and
 * venc_base_abi.h. Devices export only data; link names are exact (B.1). */

#ifndef FREECODEC_VENCODER_H
#define FREECODEC_VENCODER_H

#include <stddef.h>
#include <stdint.h>

#include "freecodec/ve_iface.h"
#include "freecodec/venc_base_abi.h"
#include "freecodec/venc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The device table, its slot signatures and the B records are defined in
 * venc_types.h. This translation unit adds no further declarations. */

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_VENCODER_H */
