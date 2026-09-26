/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Encoder framework interface (spec r2/05 part B records, part B.5 entry
 * points).
 *
 * The daemon owns one `VideoEncoder *` context built by VideoEncCreate and
 * driven through these entry points; the device tables, records and value sets
 * are the part-B ones shared with vencoder.h (venc_types.h). The support
 * library calls the framework composes sit in venc_base_abi.h.
 *
 * Every symbol below is a fixed link name; the mixed capitalisation is part of
 * the name, not a typo to be normalised. */

#ifndef FREECODEC_FENC_ABI_H
#define FREECODEC_FENC_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "freecodec/venc_base_abi.h"
#include "freecodec/venc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The framework context is opaque to its caller. */
typedef void *VideoEncoder;

/* Engine-relative IOMMU request (8 bytes): a dma-buf in, its engine address
 * out (spec r2/05 A.5, the same record as fc_ve_iommu_req in ve_iface.h). */
struct user_iommu_param {
    int          fd;
    unsigned int engine_addr;
};

VideoEncoder *VideoEncCreate(VENC_CODEC_TYPE codecType);
int  VideoEncInit(VideoEncoder *encoder, VencBaseConfig *config);
int  VideoEncUnInit(VideoEncoder *encoder);
void VideoEncDestroy(VideoEncoder *encoder);
int  VideoEncSetParameter(VideoEncoder *encoder, VENC_INDEXTYPE indexType, void *param);
int  VideoEncGetParameter(VideoEncoder *encoder, VENC_INDEXTYPE indexType, void *param);
int  VideoEncodeOneFrame(VideoEncoder *encoder);

int  AddOneInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer);
int  AlreadyUsedInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer);

int  ValidBitstreamFrameNum(VideoEncoder *encoder);
int  GetOneBitstreamFrame(VideoEncoder *encoder, VencOutputBuffer *buffer);
int  FreeOneBitStreamFrame(VideoEncoder *encoder, VencOutputBuffer *buffer);

int  VideoEncoderReset(VideoEncoder *encoder);
unsigned int VideoEncoderGetUnencodedBufferNum(VideoEncoder *encoder);

int  AllocInputBuffer(VideoEncoder *encoder, VencAllocateBufferParam *param);
int  GetOneAllocInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer);
int  FlushCacheAllocInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer);
int  ReturnOneAllocInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer);
int  ReleaseAllocInputBuffer(VideoEncoder *encoder);

void VideoEncoderGetVeIommuAddr(VideoEncoder *encoder, struct user_iommu_param *param);
void VideoEncoderFreeVeIommuAddr(VideoEncoder *encoder, struct user_iommu_param *param);
int  VideoEncoderSetFreq(VideoEncoder *encoder, int nFreq);
void VideoEncoderSetDdrMode(VideoEncoder *encoder, int nDdrMode);

/* ========================================================= layout checks */

#if defined(__arm__)
_Static_assert(sizeof(VideoEncoder) == 4, "VideoEncoder handle is 4 bytes");
_Static_assert(sizeof(struct user_iommu_param) == 8, "user_iommu_param is 8 bytes");
_Static_assert(offsetof(struct user_iommu_param, fd) == 0, "user_iommu_param.fd");
_Static_assert(offsetof(struct user_iommu_param, engine_addr) == 4, "user_iommu_param.engine_addr");
#endif

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_FENC_ABI_H */
