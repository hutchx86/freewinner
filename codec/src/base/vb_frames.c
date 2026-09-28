/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Input picture queues of the encoder support library (support-library spec
 * s5): fixed slots moving through empty -> input -> used -> empty FIFOs, plus
 * an optional pool of library-allocated pictures. */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/venc_base_abi.h"

/* FIFO of slot indices; capacity equals the number of slots, so it never
 * overflows while every index is in exactly one queue. */
struct fm_queue {
    int *idx;
    int  cap;
    int  head;
    int  count;
};

struct vb_frame_manager {
    pthread_mutex_t    lock;
    struct vb_mem_ops *memops;
    void              *ve_ops;
    void              *ve_self;

    /* caller-supplied pictures */
    int                nslots;
    vb_input_buffer   *slots;
    struct fm_queue    q_empty;
    struct fm_queue    q_input;
    struct fm_queue    q_used;

    /* library-allocated pictures */
    unsigned int       pool_num;   /* 0: no pool */
    unsigned int       size_y;
    unsigned int       size_c;
    vb_input_buffer   *pool;
    unsigned char     *pool_avail; /* per picture: 1 when in q_avail */
    struct fm_queue    q_avail;

    unsigned int       pending;
};

static void fm_log(const char *fn, const char *msg)
{
    fprintf(stderr, "venc_base: %s: %s\n", fn, msg);
}

static int q_init(struct fm_queue *q, int cap)
{
    q->cap = cap;
    q->head = 0;
    q->count = 0;
    q->idx = calloc(cap > 0 ? (size_t)cap : 1u, sizeof(int));
    return q->idx ? 0 : -1;
}

static void q_push(struct fm_queue *q, int v)
{
    q->idx[(q->head + q->count) % q->cap] = v;
    q->count++;
}

static int q_front(const struct fm_queue *q)
{
    return q->idx[q->head];
}

static int q_pop(struct fm_queue *q)
{
    int v = q->idx[q->head];

    q->head = (q->head + 1) % q->cap;
    q->count--;
    return v;
}

static void q_fill(struct fm_queue *q)
{
    int i;

    q->head = 0;
    q->count = 0;
    for (i = 0; i < q->cap; i++)
        q_push(q, i);
}

static void q_clear(struct fm_queue *q)
{
    q->head = 0;
    q->count = 0;
}

static void pending_dec(vb_frame_manager *fm)
{
    if (fm->pending > 0)
        fm->pending--;
}

/* Free the pictures' buffers of the first n pool entries. */
static void pool_free_buffers(vb_frame_manager *fm, vb_input_buffer *pool,
                              unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        if (pool[i].pAddrVirY)
            fm->memops->pfree(pool[i].pAddrVirY, fm->ve_ops, fm->ve_self);
        if (pool[i].pAddrVirC)
            fm->memops->pfree(pool[i].pAddrVirC, fm->ve_ops, fm->ve_self);
    }
}

vb_frame_manager *FrameBufferManagerCreate(int slots, struct vb_mem_ops *memops,
                                           void *ve_ops, void *ve_self)
{
    vb_frame_manager *fm;

    if (slots < 0) {
        fm_log("FrameBufferManagerCreate", "negative slot count");
        return NULL;
    }
    fm = calloc(1, sizeof(*fm));
    if (!fm)
        goto oom;
    fm->memops  = memops;
    fm->ve_ops  = ve_ops;
    fm->ve_self = ve_self;
    fm->nslots  = slots;
    fm->slots   = calloc(slots > 0 ? (size_t)slots : 1u, sizeof(vb_input_buffer));
    if (!fm->slots)
        goto oom;
    if (q_init(&fm->q_empty, slots) || q_init(&fm->q_input, slots) ||
        q_init(&fm->q_used, slots))
        goto oom;
    if (pthread_mutex_init(&fm->lock, NULL) != 0)
        goto oom;
    q_fill(&fm->q_empty);
    return fm;

oom:
    fm_log("FrameBufferManagerCreate", "out of memory");
    if (fm) {
        free(fm->q_used.idx);
        free(fm->q_input.idx);
        free(fm->q_empty.idx);
        free(fm->slots);
        free(fm);
    }
    return NULL;
}

void FrameBufferManagerDestroy(vb_frame_manager *fm)
{
    if (!fm)
        return;
    if (fm->pool) {
        pool_free_buffers(fm, fm->pool, fm->pool_num);
        free(fm->pool);
        free(fm->pool_avail);
        free(fm->q_avail.idx);
    }
    free(fm->q_used.idx);
    free(fm->q_input.idx);
    free(fm->q_empty.idx);
    free(fm->slots);
    pthread_mutex_destroy(&fm->lock);
    free(fm);
}

int AddInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf)
{
    int s;

    if (!fm || !buf)
        return -1;
    pthread_mutex_lock(&fm->lock);
    if (fm->q_empty.count == 0) {
        pthread_mutex_unlock(&fm->lock);
        return -1;
    }
    s = q_pop(&fm->q_empty);
    fm->slots[s] = *buf;
    q_push(&fm->q_input, s);
    fm->pending++;
    pthread_mutex_unlock(&fm->lock);
    return 0;
}

int GetInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf)
{
    if (!fm || !buf)
        return -1;
    pthread_mutex_lock(&fm->lock);
    if (fm->q_input.count == 0) {
        pthread_mutex_unlock(&fm->lock);
        return -1;
    }
    *buf = fm->slots[q_front(&fm->q_input)];
    pthread_mutex_unlock(&fm->lock);
    return 0;
}

int AddUsedInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf)
{
    int s;

    if (!fm || !buf)
        return -1;
    pthread_mutex_lock(&fm->lock);
    if (fm->q_input.count == 0 ||
        fm->slots[q_front(&fm->q_input)].nID != buf->nID) {
        pthread_mutex_unlock(&fm->lock);
        return -1;
    }
    s = q_pop(&fm->q_input);
    q_push(&fm->q_used, s);
    pending_dec(fm);
    pthread_mutex_unlock(&fm->lock);
    return 0;
}

int GetUsedInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf)
{
    int s;

    if (!fm || !buf)
        return -1;
    pthread_mutex_lock(&fm->lock);
    if (fm->q_used.count == 0) {
        pthread_mutex_unlock(&fm->lock);
        return -1;
    }
    s = q_pop(&fm->q_used);
    *buf = fm->slots[s];
    q_push(&fm->q_empty, s);
    pthread_mutex_unlock(&fm->lock);
    return 0;
}

int AllocateInputBuffer(vb_frame_manager *fm, vb_alloc_param *param)
{
    struct vb_mem_ops *mo;
    vb_input_buffer *pool;
    unsigned char *avail;
    struct fm_queue q;
    unsigned int n, i;

    if (!fm || !param)
        return -1;
    mo = fm->memops;
    n = param->nBufferNum;
    if (!mo || n == 0 || n > (unsigned int)INT32_MAX) {
        fm_log("AllocateInputBuffer", "invalid request");
        return -1;
    }

    pthread_mutex_lock(&fm->lock);
    if (fm->pool) {
        pthread_mutex_unlock(&fm->lock);
        fm_log("AllocateInputBuffer", "pool already allocated");
        return -1;
    }

    pool  = calloc(n, sizeof(*pool));
    avail = calloc(n, 1);
    if (!pool || !avail || q_init(&q, (int)n)) {
        free(pool);
        free(avail);
        pthread_mutex_unlock(&fm->lock);
        fm_log("AllocateInputBuffer", "out of memory");
        return -1;
    }

    for (i = 0; i < n; i++) {
        vb_input_buffer *b = &pool[i];

        b->nID = i;
        b->bAllocMemSelf = 1;
        b->pAddrVirY = mo->palloc((int)param->nSizeY, fm->ve_ops, fm->ve_self);
        if (!b->pAddrVirY)
            goto fail;
        b->pAddrPhyY = mo->cpu_get_phyaddr(b->pAddrVirY);
        mo->flush_cache(b->pAddrVirY, (int)param->nSizeY);
        if (param->nSizeC > 0) {
            b->pAddrVirC = mo->palloc((int)param->nSizeC, fm->ve_ops, fm->ve_self);
            if (!b->pAddrVirC)
                goto fail;
            b->pAddrPhyC = mo->cpu_get_phyaddr(b->pAddrVirC);
            mo->flush_cache(b->pAddrVirC, (int)param->nSizeC);
        }
    }

    fm->pool       = pool;
    fm->pool_avail = avail;
    fm->pool_num   = n;
    fm->size_y     = param->nSizeY;
    fm->size_c     = param->nSizeC;
    fm->q_avail    = q;
    for (i = 0; i < n; i++) {
        q_push(&fm->q_avail, (int)i);
        avail[i] = 1;
    }
    pthread_mutex_unlock(&fm->lock);
    return 0;

fail:
    fm_log("AllocateInputBuffer", "picture allocation failed");
    pool_free_buffers(fm, pool, i + 1);
    free(q.idx);
    free(avail);
    free(pool);
    pthread_mutex_unlock(&fm->lock);
    return -1;
}

int GetOneAllocateInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf)
{
    int p;

    if (!fm || !buf)
        return VB_RESULT_NULL_PTR;
    pthread_mutex_lock(&fm->lock);
    if (!fm->pool) {
        pthread_mutex_unlock(&fm->lock);
        return VB_RESULT_NO_RESOURCE;
    }
    if (fm->q_avail.count == 0) {
        pthread_mutex_unlock(&fm->lock);
        return VB_RESULT_NO_FRAME_BUFFER;
    }
    p = q_pop(&fm->q_avail);
    fm->pool_avail[p] = 0;
    *buf = fm->pool[p];
    fm->pending++;
    pthread_mutex_unlock(&fm->lock);
    return VB_RESULT_OK;
}

int FlushCacheAllocateInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf)
{
    unsigned int sy, sc;

    if (!fm || !buf)
        return -1;
    pthread_mutex_lock(&fm->lock);
    sy = fm->size_y;
    sc = fm->size_c;
    pthread_mutex_unlock(&fm->lock);
    if (!fm->memops)
        return 0;
    if (sy > 0)
        fm->memops->flush_cache(buf->pAddrVirY, (int)sy);
    if (sc > 0)
        fm->memops->flush_cache(buf->pAddrVirC, (int)sc);
    return 0;
}

int ReturnOneAllocateInputBuffer(vb_frame_manager *fm, vb_input_buffer *buf)
{
    unsigned long id;

    if (!fm || !buf)
        return VB_RESULT_NULL_PTR;
    id = buf->nID;
    pthread_mutex_lock(&fm->lock);
    if (!fm->pool || id >= fm->pool_num || fm->pool_avail[id]) {
        pthread_mutex_unlock(&fm->lock);
        fm_log("ReturnOneAllocateInputBuffer", "not a checked-out pool picture");
        return VB_RESULT_ILLEGAL_PARAM;
    }
    q_push(&fm->q_avail, (int)id);
    fm->pool_avail[id] = 1;
    pending_dec(fm);
    pthread_mutex_unlock(&fm->lock);
    return VB_RESULT_OK;
}

int ResetFrameBuffer(vb_frame_manager *fm)
{
    unsigned int i;

    if (!fm)
        return -1;
    pthread_mutex_lock(&fm->lock);
    if (fm->nslots > 0)
        memset(fm->slots, 0, (size_t)fm->nslots * sizeof(vb_input_buffer));
    q_fill(&fm->q_empty);
    q_clear(&fm->q_input);
    q_clear(&fm->q_used);
    if (fm->pool) {
        q_clear(&fm->q_avail);
        for (i = 0; i < fm->pool_num; i++) {
            q_push(&fm->q_avail, (int)i);
            fm->pool_avail[i] = 1;
        }
    }
    fm->pending = 0;
    pthread_mutex_unlock(&fm->lock);
    return 0;
}

unsigned int GetUnencodedBufferNum(vb_frame_manager *fm)
{
    unsigned int n;

    if (!fm)
        return 0;
    pthread_mutex_lock(&fm->lock);
    n = (fm->nslots > 0 || fm->pool) ? fm->pending : 0;
    pthread_mutex_unlock(&fm->lock);
    return n;
}
