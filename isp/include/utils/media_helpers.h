/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media_helpers.h - multimedia common helper surface.
 *
 * Only copy_MPP_CHN_S is live in the current link; the attribute copies and
 * codec-map switches of the source unit are omitted (see
 * spec/media_utils/media_helpers.md section 3).
 */
#ifndef MEDIA_HELPERS_H
#define MEDIA_HELPERS_H

#include "media_utils_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

ERRORTYPE copy_MPP_CHN_S(MPP_CHN_S *pDst, MPP_CHN_S *pSrc);

#ifdef __cplusplus
}
#endif

#endif /* MEDIA_HELPERS_H */
