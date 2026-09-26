/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * bitmap.h - payload byte count for a BITMAP_S pixel format.
 *
 * The exported symbol is the platform bitmap size helper.  Only this one
 * entry point is live in the current link; the flip helper is omitted (see
 * spec/media_utils/bitmap.md section 3).
 */
#ifndef BITMAP_H
#define BITMAP_H

#include "media_utils_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

int BITMAP_S_GetdataSize(const BITMAP_S *pBitmap);

#ifdef __cplusplus
}
#endif

#endif /* BITMAP_H */
