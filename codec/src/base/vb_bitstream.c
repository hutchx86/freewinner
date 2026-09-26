/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Bitstream ring of the encoder support library (cleanroom/venc-base SPEC §4): one
 * contiguous DMA buffer that the encoder writes frames into, plus a FIFO of frame
 * descriptors. Every frame occupies its length rounded up to 64 bytes. */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/venc_base_abi.h"

#define VB_BS_FIFO_SIZE   1024
#define VB_BS_ALIGN       64
#define VB_BS_RETRY_STEP  (512 * 1024)

struct vb_bitstream_manager {
    pthread_mutex_t    lock;
    struct vb_mem_ops *memops;
    void              *ve_ops;
    void              *ve_self;

    unsigned char     *buf;        /* user address */
    uintptr_t          phy_base;   /* engine address of the first byte */
    uintptr_t          phy_end;    /* engine address of the last byte */
    int                size;       /* bytes actually allocated */

    int                write_off;
    int                used;       /* occupied bytes (64-byte units) */

    vb_stream_info     fifo[VB_BS_FIFO_SIZE];
    int                ins;        /* insert position */
    int                rd;         /* read position */
    int                unread;     /* descriptors not yet read */
    int                unreturned; /* frames not yet returned */
};

static int bs_round(int len)
{
    return (int)(((unsigned int)len + (VB_BS_ALIGN - 1)) &
                 ~(unsigned int)(VB_BS_ALIGN - 1));
}

static void bs_log(const char *fn, const char *msg)
{
    fprintf(stderr, "venc_base: %s: %s\n", fn, msg);
}

vb_bitstream_manager *BitStreamCreate(unsigned char no_cache, int size,
                                      struct vb_mem_ops *memops,
                                      void *ve_ops, void *ve_self)
{
    vb_bitstream_manager *bs;
    void *(*alloc)(int, void *, void *);
    unsigned char *buf = NULL;
    int got;

    if (size <= 0) {
        bs_log("BitStreamCreate", "invalid size");
        return NULL;
    }
    if (!memops) {
        bs_log("BitStreamCreate", "NULL memory ops");
        return NULL;
    }
    alloc = no_cache ? memops->palloc_no_cache : memops->palloc;
    if (!alloc) {
        bs_log("BitStreamCreate", "memory ops lack an allocator");
        return NULL;
    }

    bs = calloc(1, sizeof(*bs));
    if (!bs) {
        bs_log("BitStreamCreate", "out of memory");
        return NULL;
    }

    for (got = size; got > 0; got -= VB_BS_RETRY_STEP) {
        buf = alloc(got, ve_ops, ve_self);
        if (buf)
            break;
    }
    if (!buf) {
        bs_log("BitStreamCreate", "buffer allocation failed");
        free(bs);
        return NULL;
    }
    if (got != size)
        fprintf(stderr, "venc_base: BitStreamCreate: got %d bytes of %d requested\n",
                got, size);

    if (pthread_mutex_init(&bs->lock, NULL) != 0) {
        bs_log("BitStreamCreate", "mutex init failed");
        memops->pfree(buf, ve_ops, ve_self);
        free(bs);
        return NULL;
    }

    memops->flush_cache(buf, got);

    bs->memops   = memops;
    bs->ve_ops   = ve_ops;
    bs->ve_self  = ve_self;
    bs->buf      = buf;
    bs->size     = got;
    bs->phy_base = (uintptr_t)memops->ve_get_phyaddr(buf);
    bs->phy_end  = bs->phy_base + (uintptr_t)got - 1u;
    return bs;
}

void BitStreamDestroy(vb_bitstream_manager *bs)
{
    if (!bs)
        return;
    if (bs->buf)
        bs->memops->pfree(bs->buf, bs->ve_ops, bs->ve_self);
    pthread_mutex_destroy(&bs->lock);
    free(bs);
}

void *BitStreamBaseAddress(vb_bitstream_manager *bs)
{
    if (!bs) {
        bs_log("BitStreamBaseAddress", "NULL handle");
        return NULL;
    }
    return bs->buf;
}

void *BitStreamBasePhyAddress(vb_bitstream_manager *bs)
{
    if (!bs) {
        bs_log("BitStreamBasePhyAddress", "NULL handle");
        return NULL;
    }
    return (void *)bs->phy_base;
}

void *BitStreamEndPhyAddress(vb_bitstream_manager *bs)
{
    if (!bs) {
        bs_log("BitStreamEndPhyAddress", "NULL handle");
        return NULL;
    }
    return (void *)bs->phy_end;
}

int BitStreamBufferSize(vb_bitstream_manager *bs)
{
    if (!bs) {
        bs_log("BitStreamBufferSize", "NULL handle");
        return 0;
    }
    return bs->size;
}

int BitStreamFreeBufferSize(vb_bitstream_manager *bs)
{
    int n;

    if (!bs) {
        bs_log("BitStreamFreeBufferSize", "NULL handle");
        return 0;
    }
    pthread_mutex_lock(&bs->lock);
    n = bs->size - bs->used;
    pthread_mutex_unlock(&bs->lock);
    return n;
}

int BitStreamFrameNum(vb_bitstream_manager *bs)
{
    int n;

    if (!bs) {
        bs_log("BitStreamFrameNum", "NULL handle");
        return -1;
    }
    pthread_mutex_lock(&bs->lock);
    n = bs->unreturned;
    pthread_mutex_unlock(&bs->lock);
    return n;
}

int BitStreamWriteOffset(vb_bitstream_manager *bs)
{
    int n;

    if (!bs) {
        bs_log("BitStreamWriteOffset", "NULL handle");
        return -1;
    }
    pthread_mutex_lock(&bs->lock);
    n = bs->write_off;
    pthread_mutex_unlock(&bs->lock);
    return n;
}

int BitStreamAddOneBitstream(vb_bitstream_manager *bs, vb_stream_info *info)
{
    vb_stream_info *slot;
    int len;

    if (!bs || !info) {
        bs_log("BitStreamAddOneBitstream", "NULL argument");
        return -1;
    }
    pthread_mutex_lock(&bs->lock);
    if (bs->unreturned >= VB_BS_FIFO_SIZE) {
        pthread_mutex_unlock(&bs->lock);
        bs_log("BitStreamAddOneBitstream", "descriptor FIFO full");
        return -1;
    }
    if (info->nStreamLength < 0 || info->nStreamLength > bs->size - bs->used) {
        pthread_mutex_unlock(&bs->lock);
        bs_log("BitStreamAddOneBitstream", "frame does not fit");
        return -1;
    }

    slot = &bs->fifo[bs->ins];
    *slot = *info;
    slot->nID = bs->ins;
    bs->ins = (bs->ins + 1) % VB_BS_FIFO_SIZE;

    bs->unread++;
    bs->unreturned++;
    len = bs_round(info->nStreamLength);
    bs->used += len;
    bs->write_off = (int)(((unsigned int)bs->write_off + (unsigned int)len) %
                          (unsigned int)bs->size);
    pthread_mutex_unlock(&bs->lock);
    return 0;
}

vb_stream_info *BitStreamGetOneBitstream(vb_bitstream_manager *bs)
{
    vb_stream_info *info;

    if (!bs) {
        bs_log("BitStreamGetOneBitstream", "NULL handle");
        return NULL;
    }
    pthread_mutex_lock(&bs->lock);
    if (bs->unread <= 0) {
        pthread_mutex_unlock(&bs->lock);
        return NULL;
    }
    info = &bs->fifo[bs->rd];
    bs->rd = (bs->rd + 1) % VB_BS_FIFO_SIZE;
    bs->unread--;
    pthread_mutex_unlock(&bs->lock);
    return info;
}

int BitStreamReturnOneBitstream(vb_bitstream_manager *bs, vb_stream_info *info)
{
    int id;

    if (!bs) {
        bs_log("BitStreamReturnOneBitstream", "NULL handle");
        return 0;
    }
    if (!info)
        return -1;
    pthread_mutex_lock(&bs->lock);
    if (bs->unreturned <= 0) {
        pthread_mutex_unlock(&bs->lock);
        return -1;
    }
    id = info->nID;
    if (id < 0 || id >= VB_BS_FIFO_SIZE) {
        pthread_mutex_unlock(&bs->lock);
        bs_log("BitStreamReturnOneBitstream", "descriptor id out of range");
        return -1;
    }
    bs->used -= bs_round(bs->fifo[id].nStreamLength);
    bs->unreturned--;
    pthread_mutex_unlock(&bs->lock);
    return 0;
}

int BitStreamReset(vb_bitstream_manager *bs, struct vb_mem_ops *memops)
{
    struct vb_mem_ops *ops;

    if (!bs) {
        bs_log("BitStreamReset", "NULL handle");
        return -1;
    }
    ops = memops ? memops : bs->memops;
    pthread_mutex_lock(&bs->lock);
    bs->write_off  = 0;
    bs->used       = 0;
    bs->ins        = 0;
    bs->rd         = 0;
    bs->unread     = 0;
    bs->unreturned = 0;
    memset(bs->fifo, 0, sizeof(bs->fifo));
    ops->flush_cache(bs->buf, bs->size);
    pthread_mutex_unlock(&bs->lock);
    return 0;
}
