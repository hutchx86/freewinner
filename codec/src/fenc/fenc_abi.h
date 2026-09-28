/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Encoder framework interface (spec r2/05 part B.5): the daemon drives one
 * `fwm_venc_handle_t *` from VideoEncCreate; records are venc_types.h, support
 * library calls venc_base_abi.h. Every symbol is a fixed link name, mixed case
 * included. */

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
typedef void *fwm_venc_handle_t;

/* Engine-relative IOMMU request (8 bytes): a dma-buf in, its engine address
 * out (spec r2/05 A.5, the same record as fc_ve_iommu_req in ve_iface.h). */
struct user_iommu_param {
    int          fd;
    unsigned int engine_addr;
};

fwm_venc_handle_t *VideoEncCreate(fwm_venc_codec_e codec);
int  VideoEncInit(fwm_venc_handle_t *encoder, fwm_venc_base_config_t *config);
int  VideoEncUnInit(fwm_venc_handle_t *encoder);
void VideoEncDestroy(fwm_venc_handle_t *encoder);
int  VideoEncSetParameter(fwm_venc_handle_t *encoder, fwm_venc_param_e index, void *param);
int  VideoEncGetParameter(fwm_venc_handle_t *encoder, fwm_venc_param_e index, void *param);
int  VideoEncodeOneFrame(fwm_venc_handle_t *encoder);

int  AddOneInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_picture_t *buffer);
int  AlreadyUsedInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_picture_t *buffer);

int  ValidBitstreamFrameNum(fwm_venc_handle_t *encoder);
int  GetOneBitstreamFrame(fwm_venc_handle_t *encoder, fwm_venc_output_frame_t *buffer);
int  FreeOneBitStreamFrame(fwm_venc_handle_t *encoder, fwm_venc_output_frame_t *buffer);

int  VideoEncoderReset(fwm_venc_handle_t *encoder);
unsigned int VideoEncoderGetUnencodedBufferNum(fwm_venc_handle_t *encoder);

int  AllocInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_pool_t *param);
int  GetOneAllocInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_picture_t *buffer);
int  FlushCacheAllocInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_picture_t *buffer);
int  ReturnOneAllocInputBuffer(fwm_venc_handle_t *encoder, fwm_venc_input_picture_t *buffer);
int  ReleaseAllocInputBuffer(fwm_venc_handle_t *encoder);

void VideoEncoderGetVeIommuAddr(fwm_venc_handle_t *encoder, struct user_iommu_param *param);
void VideoEncoderFreeVeIommuAddr(fwm_venc_handle_t *encoder, struct user_iommu_param *param);
int  VideoEncoderSetFreq(fwm_venc_handle_t *encoder, int freq);
void VideoEncoderSetDdrMode(fwm_venc_handle_t *encoder, int ddr_mode);

/* ========================================================= layout checks */

#if defined(__arm__)
_Static_assert(sizeof(fwm_venc_handle_t) == 4, "handle is 4 bytes");
_Static_assert(sizeof(struct user_iommu_param) == 8, "user_iommu_param is 8 bytes");
_Static_assert(offsetof(struct user_iommu_param, fd) == 0, "user_iommu_param.fd");
_Static_assert(offsetof(struct user_iommu_param, engine_addr) == 4, "user_iommu_param.engine_addr");
#endif

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_FENC_ABI_H */
