/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Encoder API framework (package C): the `VideoEnc*` entry points that sit
 * between the media daemon and the encoder support library / codec device. It
 * owns one encoder context, selects a device table at create, drives the input
 * picture queues and the device per frame, and forwards parameters. Behaviour
 * is cleanroom/middleware/fenc/SPEC.md; this is original clean-room code.
 *
 * Deliberately not implemented (see NOT-IMPLEMENTED.md): H.265/JPEG/VP8 codecs,
 * the still-JPEG `AWJpecEnc`/ION path, the create-time SDK banner, and the
 * compile-time-disabled speed log. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/ve_iface.h"
#include "freecodec/venc_base_abi.h"

#include "fenc_abi.h"

#define FENC_LOG(...) fprintf(stderr, "fenc: " __VA_ARGS__)

/* Framework constants (SPEC §5.7). */
#define FENC_INPUT_SLOTS      4
#define FENC_IC_H264_VER2_MIN 0x1708u  /* at or above: H.264 -> the ver2 device */
#define FENC_IC_PERF_MODE     0x1639u  /* only this IC has the encoder-perf knob */
#define FENC_PERF_WIDE_W      3840u
#define FENC_PERF_WIDE_H      2160u

struct fenc_ctx {
    VENC_CODEC_TYPE    codec;
    VENC_DEVICE       *dev;        /* selected device table (process-global) */
    void              *dev_handle;
    struct vb_mem_ops *memops;
    fc_ve_ops  *ve_ops;
    void              *ve_self;
    unsigned int       ic_version;
    int                initialised;
    int                perf_mode;  /* only meaningful on the 0x1639 IC */
    vb_frame_manager  *fbm;        /* input picture queues, alive when inited */
    VencBaseConfig     base;       /* the caller's config, re-asserted at init */
    VencInputBuffer    peek;       /* framework-owned peek storage */
};

/* --------------------------------------------------------------- helpers */

static VENC_DEVICE *fenc_pick_device(VENC_CODEC_TYPE codec, unsigned int ic_version)
{
    if (codec == VENC_CODEC_H264)
        return ic_version >= FENC_IC_H264_VER2_MIN ? &video_encoder_h264_ver2
                                                   : &video_encoder_h264_ver1;
    /* VENC_CODEC_H264_VER2 names the ver2 device explicitly, bypassing the
     * IC-version test. */
    return &video_encoder_h264_ver2;
}

/* Unwind a partially built context in reverse acquisition order: device close,
 * manager release, VE release, free. Only what was acquired is released. */
static void fenc_create_unwind(struct fenc_ctx *ctx, int mem_inited, int dev_opened)
{
    if (dev_opened && ctx->dev && ctx->dev->close)
        ctx->dev->close(ctx->dev_handle);
    if (mem_inited)
        EncAdpaterRelease(ctx->memops);
    if (ctx->ve_ops && ctx->ve_ops->close)
        ctx->ve_ops->close(ctx->ve_self);
    free(ctx);
}

/* ------------------------------------------------------------ lifecycle */

VideoEncoder *VideoEncCreate(VENC_CODEC_TYPE codecType)
{
    struct fenc_ctx *ctx;
    fc_ve_config ve_cfg;
    fc_ve_ops *ve_ops;
    struct vb_mem_ops *memops;
    void *ve_self;
    void *top = NULL;
    unsigned int ic_version = 0;
    int mem_inited = 0;

    if (codecType != VENC_CODEC_H264 && codecType != VENC_CODEC_H264_VER2) {
        FENC_LOG("codec type %d is not supported\n", (int)codecType);
        return NULL;
    }

    ve_ops = GetVeOpsS(FC_VE_OPS_DEFAULT);
    if (!ve_ops)
        return NULL;

    memset(&ve_cfg, 0, sizeof(ve_cfg));
    ve_cfg.use_decoder    = 0;
    ve_cfg.use_encoder    = 1;
    ve_cfg.work_mode         = (int)codecType;
    ve_cfg.width_hint          = 0;
    ve_cfg.fbc_enable = 0;
    ve_cfg.reset_mode    = 0;

    ve_self = ve_ops->open(&ve_cfg);
    if (!ve_self)
        return NULL;

    if (ve_ops->group_base) {
        top = ve_ops->group_base(ve_self, FC_VE_GROUP_TOP);
        if (ve_ops->lock)
            ve_ops->lock(ve_self);
        ic_version = EncAdapterGetICVersion(top);
        if (ve_ops->unlock)
            ve_ops->unlock(ve_self);
    }

    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        if (ve_ops->close)
            ve_ops->close(ve_self);
        return NULL;
    }
    ctx->codec      = codecType;
    ctx->ve_ops     = ve_ops;
    ctx->ve_self    = ve_self;
    ctx->ic_version = ic_version;
    ctx->dev        = fenc_pick_device(codecType, ic_version);

    memops = MemAdapterGetOpsS();
    if (!memops)
        goto fail;
    if (EncAdapterInitializeMem(memops) != 0)
        goto fail;
    ctx->memops = memops;
    mem_inited  = 1;

    ctx->base.memops     = memops;
    ctx->base.veOpsS     = ve_ops;
    ctx->base.pVeOpsSelf = ve_self;

    ctx->dev_handle = ctx->dev->open(&ctx->base, ic_version);
    if (!ctx->dev_handle)
        goto fail;

    return (VideoEncoder *)ctx;

fail:
    fenc_create_unwind(ctx, mem_inited, 0);
    return NULL;
}

int VideoEncInit(VideoEncoder *encoder, VencBaseConfig *config)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;
    int rc;

    if (!ctx || !config || ctx->initialised)
        return VENC_RESULT_NULL_PTR;

    /* The framework owns these three; re-assert them into the caller's record
     * before it is copied into the context. */
    config->memops     = ctx->memops;
    config->veOpsS     = ctx->ve_ops;
    config->pVeOpsSelf = ctx->ve_self;

    ctx->fbm = FrameBufferManagerCreate(FENC_INPUT_SLOTS, ctx->memops,
                                        ctx->ve_ops, ctx->ve_self);
    if (!ctx->fbm) {
        FENC_LOG("input picture manager create failed\n");
        return VENC_RESULT_NO_MEMORY;
    }

    /* Encoder-performance mode exists only on the 0x1639 IC; inert elsewhere. */
    if (ctx->ic_version == FENC_IC_PERF_MODE) {
        ctx->perf_mode = (config->nDstWidth >= FENC_PERF_WIDE_W ||
                          config->nDstHeight >= FENC_PERF_WIDE_H) ? 1 : 0;
        if (ctx->ve_ops->perf_setup)
            ctx->ve_ops->perf_setup(ctx->ve_self, ctx->perf_mode);
    }

    ctx->base             = *config;
    ctx->base.veOpsS      = ctx->ve_ops;
    ctx->base.pVeOpsSelf  = ctx->ve_self;

    rc = ctx->dev->init(ctx->dev_handle, &ctx->base);
    if (rc != 0) {
        /* The device refused; leave the context usable for a retry but do not
         * leak the picture manager. */
        FrameBufferManagerDestroy(ctx->fbm);
        ctx->fbm = NULL;
        return rc;
    }

    ctx->initialised = 1;
    return 0;
}

int VideoEncUnInit(VideoEncoder *encoder)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !ctx->initialised) {
        FENC_LOG("uninit on a context that is not initialised\n");
        return -1;
    }

    if (ctx->dev && ctx->dev->uninit)
        ctx->dev->uninit(ctx->dev_handle);

    if (ctx->ic_version == FENC_IC_PERF_MODE && ctx->ve_ops->perf_teardown)
        ctx->ve_ops->perf_teardown(ctx->ve_self, ctx->perf_mode);

    if (ctx->fbm) {
        FrameBufferManagerDestroy(ctx->fbm);
        ctx->fbm = NULL;
    }

    ctx->initialised = 0;
    return 0;
}

void VideoEncDestroy(VideoEncoder *encoder)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx)
        return;

    (void)VideoEncUnInit(encoder);

    if (ctx->dev) {
        if (ctx->dev->close)
            ctx->dev->close(ctx->dev_handle);
        ctx->dev = NULL;
    }
    if (ctx->memops)
        EncAdpaterRelease(ctx->memops);
    if (ctx->ve_ops && ctx->ve_ops->close)
        ctx->ve_ops->close(ctx->ve_self);

    free(ctx);
}

/* --------------------------------------------------------------- encode */

int VideoEncodeOneFrame(VideoEncoder *encoder)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;
    unsigned int offset;
    int rc;

    if (!ctx)
        return -1;

    offset = __EncAdapterMemGetVeAddrOffset(ctx->memops);

    /* Peek the oldest queued picture without removing it. */
    if (GetInputBuffer(ctx->fbm, (vb_input_buffer *)&ctx->peek) != 0)
        return VENC_RESULT_NO_FRAME_BUFFER;

    /* The device sees engine-relative addresses. */
    ctx->peek.pAddrPhyY -= offset;
    ctx->peek.pAddrPhyC -= offset;

    if (ctx->ve_ops->lock)
        ctx->ve_ops->lock(ctx->ve_self);
    rc = ctx->dev->encode(ctx->dev_handle, &ctx->peek);
    if (ctx->ve_ops->unlock)
        ctx->ve_ops->unlock(ctx->ve_self);

    /* Whether or not the encode succeeded, the picture moves to the used queue
     * so the caller can reclaim it. Matching is on nID; the manager keeps its
     * own (unadjusted) copy. */
    (void)AddUsedInputBuffer(ctx->fbm, (vb_input_buffer *)&ctx->peek);

    return rc;
}

/* ----------------------------------------------------------- parameters */

int VideoEncSetParameter(VideoEncoder *encoder, VENC_INDEXTYPE indexType, void *param)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !ctx->dev)
        return VENC_RESULT_NULL_PTR;
    return ctx->dev->SetParameter(ctx->dev_handle, (int)indexType, param);
}

int VideoEncGetParameter(VideoEncoder *encoder, VENC_INDEXTYPE indexType, void *param)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !ctx->dev)
        return VENC_RESULT_NULL_PTR;
    return ctx->dev->GetParameter(ctx->dev_handle, (int)indexType, param);
}

/* --------------------------------------------------- input picture calls */

int AddOneInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !buffer)
        return -1;
    return AddInputBuffer(ctx->fbm, (vb_input_buffer *)buffer);
}

int AlreadyUsedInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !buffer)
        return -1;
    return GetUsedInputBuffer(ctx->fbm, (vb_input_buffer *)buffer);
}

/* ------------------------------------------------------- bitstream calls */

int ValidBitstreamFrameNum(VideoEncoder *encoder)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !ctx->dev) {
        FENC_LOG("bitstream query on an invalid context\n");
        return -1;
    }
    return ctx->dev->ValidBitStreamFrameNum(ctx->dev_handle);
}

int GetOneBitstreamFrame(VideoEncoder *encoder, VencOutputBuffer *buffer)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;
    int rc;

    if (!ctx)
        return -1;
    rc = ctx->dev->GetOneBitStreamFrame(ctx->dev_handle, buffer);
    return rc != 0 ? VENC_RESULT_BITSTREAM_IS_EMPTY : 0;
}

int FreeOneBitStreamFrame(VideoEncoder *encoder, VencOutputBuffer *buffer)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;
    int rc;

    if (!ctx)
        return -1;
    rc = ctx->dev->FreeOneBitStreamFrame(ctx->dev_handle, buffer);
    return rc != 0 ? -1 : 0;
}

/* ---------------------------------------------------------------- reset */

int VideoEncoderReset(VideoEncoder *encoder)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;
    int rc;

    if (!ctx)
        return -1;
    if (ResetFrameBuffer(ctx->fbm) != 0)
        return -1;
    rc = ctx->dev->ResetBitStreamFrame(ctx->dev_handle);
    if (rc != 0)
        return -1;
    return rc;
}

/* ------------------------------------------------ pool / companion calls */

unsigned int VideoEncoderGetUnencodedBufferNum(VideoEncoder *encoder)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx)
        return (unsigned int)-1;
    return GetUnencodedBufferNum(ctx->fbm);
}

int AllocInputBuffer(VideoEncoder *encoder, VencAllocateBufferParam *param)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !param)
        return VENC_RESULT_NULL_PTR;
    if (!ctx->fbm)
        return VENC_RESULT_NO_RESOURCE;
    if (AllocateInputBuffer(ctx->fbm, (vb_alloc_param *)param) != 0)
        return VENC_RESULT_NO_MEMORY;
    return 0;
}

int GetOneAllocInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx)
        return VENC_RESULT_NULL_PTR;
    return GetOneAllocateInputBuffer(ctx->fbm, (vb_input_buffer *)buffer);
}

int FlushCacheAllocInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx)
        return VENC_RESULT_NULL_PTR;
    (void)FlushCacheAllocateInputBuffer(ctx->fbm, (vb_input_buffer *)buffer);
    return 0;
}

int ReturnOneAllocInputBuffer(VideoEncoder *encoder, VencInputBuffer *buffer)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx)
        return VENC_RESULT_NULL_PTR;
    return ReturnOneAllocateInputBuffer(ctx->fbm, (vb_input_buffer *)buffer);
}

int ReleaseAllocInputBuffer(VideoEncoder *encoder)
{
    /* The pool lives inside the picture manager and is freed with it. */
    return encoder ? 0 : -1;
}

void VideoEncoderGetVeIommuAddr(VideoEncoder *encoder, struct user_iommu_param *param)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !param || !ctx->ve_ops->iommu_map)
        return;
    ctx->ve_ops->iommu_map(ctx->ve_self,
                              (fc_ve_iommu_req *)param);
}

void VideoEncoderFreeVeIommuAddr(VideoEncoder *encoder, struct user_iommu_param *param)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !param || !ctx->ve_ops->iommu_unmap)
        return;
    ctx->ve_ops->iommu_unmap(ctx->ve_self,
                               (fc_ve_iommu_req *)param);
}

int VideoEncoderSetFreq(VideoEncoder *encoder, int nFreq)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !ctx->ve_ops->set_clock)
        return -1;
    return ctx->ve_ops->set_clock(ctx->ve_self, (unsigned int)nFreq);
}

void VideoEncoderSetDdrMode(VideoEncoder *encoder, int nDdrMode)
{
    struct fenc_ctx *ctx = (struct fenc_ctx *)encoder;

    if (!ctx || !ctx->ve_ops->set_ddr_mode)
        return;
    ctx->ve_ops->set_ddr_mode(ctx->ve_self, nDdrMode);
}
