/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Behavioural test for the encoder support library's input picture queues against
 * a fake memory-ops table (support-library spec s5, s7). */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_memops.h"

static int VE_OPS_TOKEN, VE_SELF_TOKEN;

static vb_input_buffer mk(unsigned long id)
{
    vb_input_buffer b;

    memset(&b, 0, sizeof(b));
    b.nID = id;
    b.pAddrVirY = (unsigned char *)(uintptr_t)(0x1000u + id);
    memset(b._opaque1, (int)(id & 0xff), sizeof(b._opaque1));
    return b;
}

static vb_frame_manager *create(int slots)
{
    return FrameBufferManagerCreate(slots, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
}

static void test_null(void)
{
    vb_input_buffer b = mk(0);
    vb_alloc_param p = { 1, 16, 8 };

    check(AddInputBuffer(NULL, &b) == -1, "Add NULL fm");
    check(GetInputBuffer(NULL, &b) == -1, "Get NULL fm");
    check(AddUsedInputBuffer(NULL, &b) == -1, "AddUsed NULL fm");
    check(GetUsedInputBuffer(NULL, &b) == -1, "GetUsed NULL fm");
    check(AllocateInputBuffer(NULL, &p) == -1, "Allocate NULL fm");
    check(GetOneAllocateInputBuffer(NULL, &b) == VB_RESULT_NULL_PTR, "GetOne NULL fm");
    check(FlushCacheAllocateInputBuffer(NULL, &b) == -1, "Flush NULL fm");
    check(ResetFrameBuffer(NULL) == -1, "Reset NULL");
    check(GetUnencodedBufferNum(NULL) == 0, "pending NULL");
    FrameBufferManagerDestroy(NULL);
    check(FrameBufferManagerCreate(-1, &g_fake_ops, NULL, NULL) == NULL, "negative slots");
}

static void test_slots(void)
{
    vb_frame_manager *fm;
    vb_input_buffer b, o;
    int i, ok;

    fake_reset();
    fm = create(3);
    if (!fm) {
        check(0, "create 3 slots");
        return;
    }
    check(GetUnencodedBufferNum(fm) == 0, "pending 0 initially");
    check(GetInputBuffer(fm, &o) == -1, "input queue empty");
    check(GetUsedInputBuffer(fm, &o) == -1, "used queue empty");
    b = mk(10);
    check(AddUsedInputBuffer(fm, &b) == -1, "AddUsed with empty input queue");
    check(AddInputBuffer(fm, NULL) == -1, "Add NULL buf");

    for (i = 0; i < 3; i++) {
        b = mk(10u + (unsigned)i);
        check(AddInputBuffer(fm, &b) == 0, "add input");
    }
    b = mk(99);
    check(AddInputBuffer(fm, &b) == -1, "4th add: no empty slot");
    check(GetUnencodedBufferNum(fm) == 3, "pending 3");

    memset(&o, 0, sizeof(o));
    check(GetInputBuffer(fm, &o) == 0 && o.nID == 10, "peek oldest input");
    b = mk(10);
    check(memcmp(&o, &b, sizeof(b)) == 0, "whole record copied");
    check(GetInputBuffer(fm, &o) == 0 && o.nID == 10, "peek does not remove");

    /* nID mismatch leaves queues unchanged */
    b = mk(11);
    check(AddUsedInputBuffer(fm, &b) == -1, "AddUsed nID mismatch rejected");
    check(GetUnencodedBufferNum(fm) == 3, "mismatch: pending unchanged");
    check(GetInputBuffer(fm, &o) == 0 && o.nID == 10, "mismatch: input head unchanged");
    check(GetUsedInputBuffer(fm, &o) == -1, "mismatch: used queue still empty");

    /* matching nID moves the stored entry (not the caller's copy) to used */
    b = mk(10);
    b.pAddrVirY = NULL;
    check(AddUsedInputBuffer(fm, &b) == 0, "AddUsed 10");
    check(GetUnencodedBufferNum(fm) == 2, "pending 2");
    check(GetInputBuffer(fm, &o) == 0 && o.nID == 11, "input head now 11");
    b = mk(11);
    check(AddUsedInputBuffer(fm, &b) == 0, "AddUsed 11");

    /* no empty slot yet: used slots are not free until collected */
    b = mk(20);
    check(AddInputBuffer(fm, &b) == -1, "still no empty slot before GetUsed");

    memset(&o, 0, sizeof(o));
    check(GetUsedInputBuffer(fm, &o) == 0 && o.nID == 10, "collect used 10 (FIFO)");
    check(o.pAddrVirY == (unsigned char *)(uintptr_t)0x100au, "stored copy returned");
    b = mk(20);
    check(AddInputBuffer(fm, &b) == 0, "slot recycled after collection");
    check(GetUsedInputBuffer(fm, &o) == 0 && o.nID == 11, "collect used 11");
    check(GetUsedInputBuffer(fm, &o) == -1, "used queue drained");
    check(GetUnencodedBufferNum(fm) == 2, "pending 2 (12 and 20)");

    /* order through wraps */
    ok = 1;
    b = mk(21);
    if (AddInputBuffer(fm, &b) != 0)
        ok = 0;
    for (i = 0; i < 20; i++) {
        unsigned long want;

        if (GetInputBuffer(fm, &o) != 0) {
            ok = 0;
            break;
        }
        want = o.nID;
        if (AddUsedInputBuffer(fm, &o) != 0 || GetUsedInputBuffer(fm, &o) != 0 ||
            o.nID != want)
            ok = 0;
        b = mk(100u + (unsigned)i);
        if (AddInputBuffer(fm, &b) != 0)
            ok = 0;
    }
    check(ok, "FIFO order kept across ring wrap");
    check(GetInputBuffer(fm, &o) == 0 && o.nID == 117, "input order after wrap");

    /* reset */
    check(ResetFrameBuffer(fm) == 0, "reset");
    check(GetUnencodedBufferNum(fm) == 0, "reset: pending 0");
    check(GetInputBuffer(fm, &o) == -1, "reset: input queue empty");
    check(GetUsedInputBuffer(fm, &o) == -1, "reset: used queue empty");
    ok = 1;
    for (i = 0; i < 3; i++) {
        b = mk(200u + (unsigned)i);
        if (AddInputBuffer(fm, &b) != 0)
            ok = 0;
    }
    check(ok, "reset: all 3 slots empty again");
    b = mk(300);
    check(AddInputBuffer(fm, &b) == -1, "reset: exactly 3 slots");

    FrameBufferManagerDestroy(fm);
    check(g_live == 0 && g_palloc_calls == 0, "slots use no DMA memory");

    /* zero slots */
    fm = create(0);
    check(fm != NULL, "0 slots allowed");
    b = mk(1);
    check(AddInputBuffer(fm, &b) == -1, "0 slots: add fails");
    check(GetUnencodedBufferNum(fm) == 0, "0 slots, no pool: pending 0");
    check(ResetFrameBuffer(fm) == 0, "0 slots: reset");
    FrameBufferManagerDestroy(fm);
}

static void test_pool(void)
{
    vb_frame_manager *fm;
    vb_alloc_param p;
    vb_input_buffer b[4], o;
    int i, ok;

    fake_reset();
    fm = create(0);
    if (!fm) {
        check(0, "create for pool");
        return;
    }
    check(GetOneAllocateInputBuffer(fm, &o) == VB_RESULT_NO_RESOURCE, "no pool -> NO_RESOURCE");
    o = mk(0);
    check(ReturnOneAllocateInputBuffer(fm, &o) == VB_RESULT_ILLEGAL_PARAM,
          "return with no pool -> ILLEGAL_PARAM");
    check(AllocateInputBuffer(fm, NULL) == -1, "Allocate NULL param");

    p.nBufferNum = 3;
    p.nSizeY = 4096;
    p.nSizeC = 2048;
    g_flush_calls = 0;
    check(AllocateInputBuffer(fm, &p) == 0, "allocate 3 pictures");
    check(g_palloc_calls == 6 && g_palloc_nc_calls == 0, "6 cached pallocs (Y+C)");
    check(g_live == 6, "6 live buffers");
    check(g_flush_calls == 6, "each buffer flushed");
    check(GetUnencodedBufferNum(fm) == 0, "pending 0 after allocate");

    ok = 1;
    for (i = 0; i < 3; i++) {
        if (GetOneAllocateInputBuffer(fm, &b[i]) != VB_RESULT_OK)
            ok = 0;
        if (b[i].nID != (unsigned long)i || b[i].bAllocMemSelf != 1)
            ok = 0;
        if (!b[i].pAddrVirY || !b[i].pAddrVirC)
            ok = 0;
        if (b[i].pAddrPhyY != fake_cpu_phy(b[i].pAddrVirY) ||
            b[i].pAddrPhyC != fake_cpu_phy(b[i].pAddrVirC))
            ok = 0;
    }
    check(ok, "pictures in index order with nID, bAllocMemSelf and cpu phys addresses");
    check((uintptr_t)b[0].pAddrPhyY == FAKE_IOMMU_BASE, "phys Y uses cpu_get_phyaddr (no ve offset)");
    check(GetUnencodedBufferNum(fm) == 3, "pending 3");
    check(GetOneAllocateInputBuffer(fm, &o) == VB_RESULT_NO_FRAME_BUFFER, "exhausted");
    check(GetOneAllocateInputBuffer(fm, NULL) == VB_RESULT_NULL_PTR, "GetOne NULL buf");

    /* flush uses the pool sizes */
    g_flush_calls = 0;
    check(FlushCacheAllocateInputBuffer(fm, &b[1]) == 0, "flush picture");
    check(g_flush_calls == 2 && g_flush_last_mem == b[1].pAddrVirC &&
          g_flush_last_size == 2048, "flush Y and C (C last, nSizeC bytes)");
    check(FlushCacheAllocateInputBuffer(fm, NULL) == -1, "flush NULL buf");

    /* return: invalid ids */
    o = b[0];
    o.nID = 3;
    check(ReturnOneAllocateInputBuffer(fm, &o) == VB_RESULT_ILLEGAL_PARAM, "return id 3 illegal");
    o.nID = (unsigned long)-1;
    check(ReturnOneAllocateInputBuffer(fm, &o) == VB_RESULT_ILLEGAL_PARAM, "return id -1 illegal");
    check(GetUnencodedBufferNum(fm) == 3, "illegal returns leave pending");

    check(ReturnOneAllocateInputBuffer(fm, &b[1]) == VB_RESULT_OK, "return 1");
    check(ReturnOneAllocateInputBuffer(fm, &b[1]) == VB_RESULT_ILLEGAL_PARAM,
          "double return rejected");
    check(ReturnOneAllocateInputBuffer(fm, &b[0]) == VB_RESULT_OK, "return 0");
    check(GetUnencodedBufferNum(fm) == 1, "pending 1");
    check(GetOneAllocateInputBuffer(fm, &o) == 0 && o.nID == 1, "returned pictures queue FIFO: 1");
    check(o.pAddrVirY == b[1].pAddrVirY, "same buffer comes back");
    check(GetOneAllocateInputBuffer(fm, &o) == 0 && o.nID == 0, "then 0");
    check(AllocateInputBuffer(fm, &p) == -1, "second allocate rejected");
    check(g_live == 6, "second allocate allocates nothing");

    /* reset: all available in index order, pending 0 */
    check(ResetFrameBuffer(fm) == 0, "reset");
    check(GetUnencodedBufferNum(fm) == 0, "reset: pending 0");
    ok = 1;
    for (i = 0; i < 3; i++)
        if (GetOneAllocateInputBuffer(fm, &o) != 0 || o.nID != (unsigned long)i)
            ok = 0;
    check(ok, "reset: pool available in index order");
    check(GetOneAllocateInputBuffer(fm, &o) == VB_RESULT_NO_FRAME_BUFFER, "reset: exactly 3");

    FrameBufferManagerDestroy(fm);
    check(g_live == 0, "destroy frees all 6 pool buffers");
    check(g_pfree_bad == 0, "pool uses stored ve ops/instance");
}

static void test_pool_luma_only_and_counting(void)
{
    vb_frame_manager *fm;
    vb_alloc_param p = { 2, 1000, 0 };
    vb_input_buffer a, b;

    fake_reset();
    fm = create(2);
    check(AllocateInputBuffer(fm, &p) == 0, "luma-only allocate");
    check(g_palloc_calls == 2 && g_live == 2, "no chroma buffers when nSizeC = 0");
    GetOneAllocateInputBuffer(fm, &a);
    check(a.pAddrVirC == NULL && a.pAddrPhyC == NULL, "chroma pointers NULL");
    g_flush_calls = 0;
    FlushCacheAllocateInputBuffer(fm, &a);
    check(g_flush_calls == 1 && g_flush_last_mem == a.pAddrVirY &&
          g_flush_last_size == 1000, "flush only luma");

    /* both facilities share one pending count */
    b = mk(7);
    AddInputBuffer(fm, &b);
    check(GetUnencodedBufferNum(fm) == 2, "pool + slot pending = 2");
    ReturnOneAllocateInputBuffer(fm, &a);
    check(GetUnencodedBufferNum(fm) == 1, "pending 1 after pool return");
    AddUsedInputBuffer(fm, &b);
    check(GetUnencodedBufferNum(fm) == 0, "pending 0 after encode");
    /* reset clears pending from both facilities */
    GetOneAllocateInputBuffer(fm, &a);
    ResetFrameBuffer(fm);
    GetOneAllocateInputBuffer(fm, &a);
    GetOneAllocateInputBuffer(fm, &b);
    AddInputBuffer(fm, &b);                   /* pending 3 */
    ResetFrameBuffer(fm);                     /* pending 0 */
    check(GetUnencodedBufferNum(fm) == 0, "reset clears pending");
    GetOneAllocateInputBuffer(fm, &a);        /* pending 1 */
    ReturnOneAllocateInputBuffer(fm, &a);     /* pending 0 */
    check(GetUnencodedBufferNum(fm) == 0, "pending back to 0");
    FrameBufferManagerDestroy(fm);
    check(g_live == 0, "no leaks");
}

/* Pending counts what is outstanding; reset voids outstanding pictures, so stale
 * returns after a reset are rejected and pending never goes below 0. */
static void test_pending_reset(void)
{
    vb_frame_manager *fm;
    vb_alloc_param p = { 2, 64, 0 };
    vb_input_buffer a, c, x;

    fake_reset();
    fm = create(1);
    AllocateInputBuffer(fm, &p);
    check(GetOneAllocateInputBuffer(fm, &a) == 0 && a.nID == 0, "take 0");
    x = mk(9);
    check(AddInputBuffer(fm, &x) == 0, "add slot");
    check(GetUnencodedBufferNum(fm) == 2, "pending 2");
    check(ResetFrameBuffer(fm) == 0, "reset");
    check(GetUnencodedBufferNum(fm) == 0, "reset: pending 0");
    check(ReturnOneAllocateInputBuffer(fm, &a) == VB_RESULT_ILLEGAL_PARAM,
          "stale return after reset rejected (picture already available)");
    check(AddUsedInputBuffer(fm, &x) == -1, "stale AddUsed after reset rejected");
    check(GetUnencodedBufferNum(fm) == 0, "pending stays 0, no wrap");

    check(GetOneAllocateInputBuffer(fm, &a) == 0 && a.nID == 0, "take 0 again");
    check(GetOneAllocateInputBuffer(fm, &c) == 0 && c.nID == 1, "take 1");
    check(ReturnOneAllocateInputBuffer(fm, &a) == 0, "return 0");
    check(ReturnOneAllocateInputBuffer(fm, &c) == 0, "return 1");
    check(GetOneAllocateInputBuffer(fm, &a) == 0 && a.nID == 0, "FIFO: 0 first");
    check(ReturnOneAllocateInputBuffer(fm, &a) == 0, "return 0");
    check(GetOneAllocateInputBuffer(fm, &a) == 0 && a.nID == 1, "then 1");
    check(ReturnOneAllocateInputBuffer(fm, &a) == 0, "return 1");
    check(GetUnencodedBufferNum(fm) == 0, "balanced: pending 0");
    FrameBufferManagerDestroy(fm);
    check(g_live == 0, "no leaks");
}

static void test_pool_failure(void)
{
    vb_frame_manager *fm;
    vb_alloc_param p = { 4, 512, 256 };
    vb_input_buffer o;
    int k;

    /* fail at each of the 8 allocations in turn: nothing leaks, no pool */
    for (k = 1; k <= 8; k++) {
        fake_reset();
        fm = create(0);
        g_fail_countdown = k;
        check(AllocateInputBuffer(fm, &p) == -1, "allocation failure -> -1");
        check(g_live == 0, "all-or-nothing: every buffer of the call freed");
        check(g_pfree_calls == k - 1, "exactly the allocated buffers are freed");
        check(GetOneAllocateInputBuffer(fm, &o) == VB_RESULT_NO_RESOURCE,
              "failed allocate leaves no pool");
        g_fail_countdown = 0;
        check(AllocateInputBuffer(fm, &p) == 0, "allocate works after a failure");
        check(g_live == 8, "8 buffers after retry");
        FrameBufferManagerDestroy(fm);
        check(g_live == 0 && g_pfree_bad == 0, "destroy after retry clean");
    }

    fake_reset();
    fm = create(0);
    p.nBufferNum = 0;
    check(AllocateInputBuffer(fm, &p) == -1, "0 pictures rejected");
    p.nBufferNum = 1;
    p.nSizeY = 0;
    check(AllocateInputBuffer(fm, &p) == -1, "0-byte luma fails in palloc");
    check(g_live == 0, "no leak");
    FrameBufferManagerDestroy(fm);

    fm = FrameBufferManagerCreate(0, NULL, NULL, NULL);
    p.nSizeY = 64;
    check(fm && AllocateInputBuffer(fm, &p) == -1, "no memops: allocate fails");
    FrameBufferManagerDestroy(fm);
}

int main(void)
{
    g_expect_ve_ops = &VE_OPS_TOKEN;
    g_expect_ve_self = &VE_SELF_TOKEN;

    test_null();
    test_slots();
    test_pool();
    test_pool_luma_only_and_counting();
    test_pending_reset();
    test_pool_failure();

    printf("venc_base: input queues, nID match, pool all-or-nothing, pending and reset %s\n",
           g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
