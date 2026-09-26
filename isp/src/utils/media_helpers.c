/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media_helpers.c - clean-room copy_MPP_CHN_S.
 *
 * Behaviour source: spec/media_utils/media_helpers.md.  A whole
 * struct assignment with no validation; NULL arguments are undefined, matching
 * the reference which dereferences unconditionally.
 */
#include "utils/media_helpers.h"

ERRORTYPE copy_MPP_CHN_S(MPP_CHN_S *pDst, MPP_CHN_S *pSrc)
{
	*pDst = *pSrc;
	return SUCCESS;
}
