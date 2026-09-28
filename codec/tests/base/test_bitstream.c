/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Behavioural test for the encoder support library's bitstream ring against a
 * fake memory-ops table (support-library spec s4, s7). */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_memops.h"

#define KIB 1024
#define FIFO 1024

static int VE_OPS_TOKEN, VE_SELF_TOKEN;

static vb_stream_info mk(int len, long long pts)
{
    vb_stream_info s;

    memset(&s, 0, sizeof(s));
    s.nStreamLength = len;
    s.nPts = pts;
    s.nID = 777;
    s.nFlags = 5;
    return s;
}

static void test_create_basic(void)
{
    vb_bitstream_manager *bs;
    uintptr_t base;

    fake_reset();
    bs = BitStreamCreate(0, 64 * KIB, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    check(bs != NULL, "create 64 KiB");
    if (!bs)
        return;
    check(g_palloc_calls == 1 && g_palloc_nc_calls == 0, "cached create uses palloc");
    check(g_live == 1, "one buffer allocated");
    check(g_flush_calls == 1 && g_flush_last_mem == BitStreamBaseAddress(bs) &&
          g_flush_last_size == 64 * KIB, "create flushes the whole buffer");
    base = (uintptr_t)fake_ve_phy(BitStreamBaseAddress(bs));
    check(base == FAKE_IOMMU_BASE - FAKE_VE_OFFSET, "base engine address via ve_get_phyaddr");
    check((uintptr_t)BitStreamBasePhyAddress(bs) == base, "BasePhyAddress");
    check((uintptr_t)BitStreamEndPhyAddress(bs) == base + 64 * KIB - 1,
          "EndPhyAddress is the last byte (inclusive)");
    check(BitStreamBufferSize(bs) == 64 * KIB, "BufferSize");
    check(BitStreamFreeBufferSize(bs) == 64 * KIB, "initially all free");
    check(BitStreamFrameNum(bs) == 0, "initially no frames");
    check(BitStreamWriteOffset(bs) == 0, "initial write offset 0");
    check(BitStreamGetOneBitstream(bs) == NULL, "nothing to read initially");
    BitStreamDestroy(bs);
    check(g_live == 0 && g_pfree_calls == 1, "destroy frees the buffer");
    check(g_pfree_bad == 0, "ve ops/instance passed through to palloc/pfree");

    fake_reset();
    bs = BitStreamCreate(1, 4 * KIB, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    check(bs != NULL && g_palloc_nc_calls == 1 && g_palloc_calls == 0,
          "no_cache create uses palloc_no_cache");
    BitStreamDestroy(bs);
    check(g_live == 0, "no_cache destroy frees");
}

static void test_create_errors(void)
{
    fake_reset();
    check(BitStreamCreate(0, 0, &g_fake_ops, NULL, NULL) == NULL, "size 0 -> NULL");
    check(BitStreamCreate(0, -5, &g_fake_ops, NULL, NULL) == NULL, "negative size -> NULL");
    check(g_palloc_calls == 0, "invalid size does not allocate");
    check(BitStreamCreate(0, 4096, NULL, NULL, NULL) == NULL, "NULL memops -> NULL");

    /* everything fails: sizes size, size-512K, ... while positive */
    fake_reset();
    g_fail_countdown = -1;
    check(BitStreamCreate(0, 1600 * KIB, &g_fake_ops, g_expect_ve_ops,
                          g_expect_ve_self) == NULL, "all allocations fail -> NULL");
    check(g_palloc_calls == 4, "retries 1600K,1088K,576K,64K (4 attempts)");
    check(g_last_req_size == 64 * KIB, "last attempt is the smallest positive size");
    check(g_live == 0, "nothing leaked");

    fake_reset();
    g_fail_countdown = -1;
    check(BitStreamCreate(0, 1024 * KIB, &g_fake_ops, g_expect_ve_ops,
                          g_expect_ve_self) == NULL, "exact multiple fails -> NULL");
    check(g_palloc_calls == 2, "1024K then 512K, never 0");
}

static void test_create_retry(void)
{
    vb_bitstream_manager *bs;

    fake_reset();
    g_fail_above = 1200 * KIB;
    bs = BitStreamCreate(0, 2000 * KIB, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    check(bs != NULL, "retry succeeds with a smaller buffer");
    check(g_palloc_calls == 3, "2000K, 1488K fail, 976K succeeds");
    check(BitStreamBufferSize(bs) == 976 * KIB, "BufferSize is the size actually obtained");
    check(BitStreamFreeBufferSize(bs) == 976 * KIB, "free size uses obtained size");
    check(g_flush_last_size == 976 * KIB, "flush covers the obtained size");
    check((uintptr_t)BitStreamEndPhyAddress(bs) ==
          (uintptr_t)BitStreamBasePhyAddress(bs) + 976 * KIB - 1,
          "end address uses obtained size");
    BitStreamDestroy(bs);
    check(g_live == 0, "retry buffer freed");
}

static void test_null_accessors(void)
{
    vb_stream_info s = mk(10, 0);

    check(BitStreamBaseAddress(NULL) == NULL, "NULL BaseAddress");
    check(BitStreamBasePhyAddress(NULL) == NULL, "NULL BasePhyAddress");
    check(BitStreamEndPhyAddress(NULL) == NULL, "NULL EndPhyAddress");
    check(BitStreamBufferSize(NULL) == 0, "NULL BufferSize");
    check(BitStreamFreeBufferSize(NULL) == 0, "NULL FreeBufferSize");
    check(BitStreamFrameNum(NULL) == -1, "NULL FrameNum");
    check(BitStreamWriteOffset(NULL) == -1, "NULL WriteOffset");
    check(BitStreamAddOneBitstream(NULL, &s) == -1, "NULL Add");
    check(BitStreamGetOneBitstream(NULL) == NULL, "NULL Get");
    check(BitStreamReturnOneBitstream(NULL, &s) == 0, "NULL handle Return -> 0");
    check(BitStreamReset(NULL, &g_fake_ops) == -1, "NULL Reset");
    BitStreamDestroy(NULL);
}

static void test_add_get_return(void)
{
    vb_bitstream_manager *bs;
    vb_stream_info s, *g1, *g2, *g3;

    fake_reset();
    bs = BitStreamCreate(0, 4096, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    if (!bs) {
        check(0, "create 4096");
        return;
    }
    check(BitStreamAddOneBitstream(bs, NULL) == -1, "Add NULL info");

    s = mk(1, 100);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add 1 byte");
    check(s.nID == 777, "caller's info not modified");
    check(BitStreamFreeBufferSize(bs) == 4096 - 64, "1 byte occupies 64");
    check(BitStreamWriteOffset(bs) == 64, "write offset +64");
    check(BitStreamFrameNum(bs) == 1, "1 frame");

    s = mk(64, 200);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add 64");
    check(BitStreamFreeBufferSize(bs) == 4096 - 128, "64 bytes occupy 64");
    check(BitStreamWriteOffset(bs) == 128, "write offset 128");

    s = mk(65, 300);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add 65");
    check(BitStreamFreeBufferSize(bs) == 4096 - 256, "65 bytes occupy 128");
    check(BitStreamWriteOffset(bs) == 256, "write offset 256");

    s = mk(0, 400);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add 0-length frame");
    check(BitStreamFreeBufferSize(bs) == 4096 - 256, "0 bytes occupy 0");
    check(BitStreamFrameNum(bs) == 4, "4 frames");

    g1 = BitStreamGetOneBitstream(bs);
    check(g1 && g1->nID == 0 && g1->nPts == 100 && g1->nStreamLength == 1 &&
          g1->nFlags == 5, "get #1: copy with nID = slot 0");
    g2 = BitStreamGetOneBitstream(bs);
    check(g2 && g2->nID == 1 && g2->nPts == 200, "get #2: nID 1");
    check(BitStreamFrameNum(bs) == 4, "get does not change frame count");
    check(BitStreamFreeBufferSize(bs) == 4096 - 256, "get does not free space");

    check(BitStreamReturnOneBitstream(bs, NULL) == -1, "Return NULL info");
    s = *g1;
    s.nID = FIFO;
    check(BitStreamReturnOneBitstream(bs, &s) == -1, "Return nID 1024 rejected");
    s.nID = -1;
    check(BitStreamReturnOneBitstream(bs, &s) == -1, "Return nID -1 rejected");
    check(BitStreamFrameNum(bs) == 4, "bad returns change nothing");

    /* the stored descriptor's length is used, not the caller's copy */
    s = *g1;
    s.nStreamLength = 4000;
    check(BitStreamReturnOneBitstream(bs, &s) == 0, "return #1");
    check(BitStreamFreeBufferSize(bs) == 4096 - 192, "return #1 frees 64 (stored length)");
    check(BitStreamFrameNum(bs) == 3, "3 frames after return");
    check(BitStreamReturnOneBitstream(bs, g2) == 0, "return #2");
    check(BitStreamFreeBufferSize(bs) == 4096 - 128, "return #2 frees 64");
    check(BitStreamWriteOffset(bs) == 256, "return does not move write offset");

    g3 = BitStreamGetOneBitstream(bs);
    check(g3 && g3->nID == 2 && g3->nStreamLength == 65, "get #3");
    check(BitStreamReturnOneBitstream(bs, g3) == 0, "return #3");
    check(BitStreamFreeBufferSize(bs) == 4096, "return #3 frees 128");
    g3 = BitStreamGetOneBitstream(bs);
    check(g3 && g3->nID == 3, "get #4");
    check(BitStreamGetOneBitstream(bs) == NULL, "nothing more to read");
    check(BitStreamReturnOneBitstream(bs, g3) == 0, "return #4");
    check(BitStreamFrameNum(bs) == 0, "all returned");
    check(BitStreamReturnOneBitstream(bs, g3) == -1, "return with nothing outstanding");
    check(BitStreamFreeBufferSize(bs) == 4096, "all space free");

    BitStreamDestroy(bs);
    check(g_live == 0 && g_pfree_bad == 0, "destroy clean");
}

static void test_capacity_and_wrap(void)
{
    vb_bitstream_manager *bs;
    vb_stream_info s, *g;
    int i, ok;

    /* free-size check */
    fake_reset();
    bs = BitStreamCreate(0, 1024, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    if (!bs) {
        check(0, "create 1024");
        return;
    }
    s = mk(1025, 0);
    check(BitStreamAddOneBitstream(bs, &s) == -1, "frame larger than buffer rejected");
    s = mk(-1, 0);
    check(BitStreamAddOneBitstream(bs, &s) == -1, "negative length rejected");
    s = mk(1000, 0);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "1000 bytes fit");
    check(BitStreamFreeBufferSize(bs) == 0, "1000 rounds to 1024, buffer full");
    check(BitStreamWriteOffset(bs) == 0, "write offset wraps to 0 at exactly size");
    s = mk(1, 0);
    check(BitStreamAddOneBitstream(bs, &s) == -1, "no room for 1 byte");
    s = mk(0, 0);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "0 bytes fit in 0 free");
    BitStreamDestroy(bs);

    /* write offset wraps modulo the buffer size (size not a multiple of 64) */
    fake_reset();
    bs = BitStreamCreate(0, 1000, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    if (!bs) {
        check(0, "create 1000");
        return;
    }
    s = mk(900, 0);    /* rounds to 960 */
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add 900");
    check(BitStreamWriteOffset(bs) == 960, "offset 960");
    g = BitStreamGetOneBitstream(bs);
    check(BitStreamReturnOneBitstream(bs, g) == 0, "return 900");
    s = mk(100, 0);    /* rounds to 128: 960 + 128 = 1088 -> 88 */
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add 100");
    check(BitStreamWriteOffset(bs) == 88, "offset wraps: (960+128) % 1000 = 88");
    check(BitStreamFreeBufferSize(bs) == 1000 - 128, "occupied 128");
    BitStreamDestroy(bs);

    /* FIFO capacity 1024 frames not yet returned */
    fake_reset();
    bs = BitStreamCreate(0, 128 * KIB, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    if (!bs) {
        check(0, "create 128K");
        return;
    }
    ok = 1;
    for (i = 0; i < FIFO; i++) {
        s = mk(10, i);
        if (BitStreamAddOneBitstream(bs, &s) != 0)
            ok = 0;
    }
    check(ok, "1024 frames accepted");
    check(BitStreamFrameNum(bs) == FIFO, "1024 frames outstanding");
    check(BitStreamFreeBufferSize(bs) == 128 * KIB - FIFO * 64, "1024 x 64 occupied");
    s = mk(10, 9999);
    check(BitStreamAddOneBitstream(bs, &s) == -1, "1025th frame rejected (FIFO full)");
    check(BitStreamWriteOffset(bs) == 64 * KIB, "write offset unchanged by rejection");

    /* read everything; still full until returned */
    for (i = 0; i < FIFO; i++) {
        g = BitStreamGetOneBitstream(bs);
        if (!g || g->nID != i || g->nPts != i)
            ok = 0;
    }
    check(ok, "1024 frames read in order with nID = index");
    check(BitStreamGetOneBitstream(bs) == NULL, "all read");
    check(BitStreamAddOneBitstream(bs, &s) == -1, "read but unreturned still counts");

    /* return one, then insert position wraps to slot 0 */
    s = mk(0, 0);
    s.nID = 0;
    check(BitStreamReturnOneBitstream(bs, &s) == 0, "return slot 0");
    s = mk(20, 5000);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add after one return");
    g = BitStreamGetOneBitstream(bs);
    check(g && g->nID == 0 && g->nPts == 5000, "insert and read positions wrap to 0");
    BitStreamDestroy(bs);
    check(g_live == 0, "no leaks");
}

static void test_reset(void)
{
    vb_bitstream_manager *bs;
    vb_stream_info s, *g;
    struct vb_mem_ops other = g_fake_ops;

    fake_reset();
    bs = BitStreamCreate(0, 8192, &g_fake_ops, g_expect_ve_ops, g_expect_ve_self);
    if (!bs) {
        check(0, "create 8192");
        return;
    }
    s = mk(100, 1);
    BitStreamAddOneBitstream(bs, &s);
    BitStreamAddOneBitstream(bs, &s);
    BitStreamGetOneBitstream(bs);
    g_flush_calls = 0;
    check(BitStreamReset(bs, &other) == 0, "reset returns 0");
    check(g_flush_calls == 1 && g_flush_last_mem == BitStreamBaseAddress(bs) &&
          g_flush_last_size == 8192, "reset flushes the whole buffer");
    check(BitStreamFrameNum(bs) == 0, "reset clears frame count");
    check(BitStreamWriteOffset(bs) == 0, "reset clears write offset");
    check(BitStreamFreeBufferSize(bs) == 8192, "reset clears occupancy");
    check(BitStreamGetOneBitstream(bs) == NULL, "reset clears unread");
    s.nID = 0;
    check(BitStreamReturnOneBitstream(bs, &s) == -1, "nothing outstanding after reset");
    s = mk(1, 42);
    check(BitStreamAddOneBitstream(bs, &s) == 0, "add after reset");
    g = BitStreamGetOneBitstream(bs);
    check(g && g->nID == 0 && g->nPts == 42, "positions restart at 0");
    check(BitStreamBufferSize(bs) == 8192, "buffer kept across reset");
    BitStreamDestroy(bs);
    check(g_live == 0, "no leaks");
}

int main(void)
{
    g_expect_ve_ops = &VE_OPS_TOKEN;
    g_expect_ve_self = &VE_SELF_TOKEN;

    test_create_basic();
    test_create_errors();
    test_create_retry();
    test_null_accessors();
    test_add_get_return();
    test_capacity_and_wrap();
    test_reset();

    printf("venc_base: bitstream ring create/retry, 64-byte occupancy, wrap, FIFO and reset %s\n",
           g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
