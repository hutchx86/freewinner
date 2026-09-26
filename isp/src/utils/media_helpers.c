// SPDX-License-Identifier: AGPL-3.0-only
/* Copyright (C) 2026 freewinner contributors */
#include "utils/media_helpers.h"

/* Field-by-field copy: no libc call, no diagnostic hook, no NULL guard
 * (24 §0, §4, Q-U2-2). Safe when pDst == pSrc. */
ERRORTYPE copy_MPP_CHN_S(MPP_CHN_S *pDst, MPP_CHN_S *pSrc)
{
	pDst->mModId = pSrc->mModId;
	pDst->mDevId = pSrc->mDevId;
	pDst->mChnId = pSrc->mChnId;
	return SUCCESS;
}
