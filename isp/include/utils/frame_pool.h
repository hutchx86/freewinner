// SPDX-License-Identifier: AGPL-3.0-only
#ifndef UTILS_FRAME_POOL_H
#define UTILS_FRAME_POOL_H
#include "media_utils_abi.h"

VideoBufferManager *VideoBufMgrCreate(int frmNum, int frmSize);
void VideoBufMgrDestroy(VideoBufferManager *mgr);

void  media_utils_log_error(const char *reason, unsigned int detail);
void *media_utils_pool_alloc(size_t size);
void  media_utils_pool_free(void *ptr);

#endif
