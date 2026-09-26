/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Encoder device interface (spec r2/05 part B).
 *
 * A codec device is one `fwm_venc_device_t` table reached through an opaque handle;
 * the four exported tables are declared in venc_types.h. The records and value
 * sets of part B live there too, because the encoder framework declares the
 * same ones, and this header is the single include root a device implementation
 * needs: it also pulls in the engine driver interface (ve_iface.h) and the
 * support library (venc_base_abi.h) the device's configuration points at.
 *
 * Nothing here is a function: a device exports only data, and the link symbols
 * keep their exact spelling (venc_types.h B.1). */

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
