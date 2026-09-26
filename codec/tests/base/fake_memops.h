/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Malloc-backed fake of the encoder support library's memory-ops table for host
 * tests: every allocation gets a range in a fake engine address space, failures
 * can be injected, and calls are counted. Header-only; include once per test. */

#ifndef FREECODEC_TEST_FAKE_MEMOPS_H
#define FREECODEC_TEST_FAKE_MEMOPS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/venc_base_abi.h"

#define FAKE_MAX_ALLOCS  64
#define FAKE_IOMMU_BASE  0x40000000u
#define FAKE_IOMMU_STEP  0x01000000u  /* each allocation gets its own 16 MiB window */
#define FAKE_VE_OFFSET   0x10000000u  /* engine address = iommu - offset */

struct fake_alloc {
    unsigned char *vir;
    int            size;
    uintptr_t      iommu;
    int            cached;
};

static struct fake_alloc g_fa[FAKE_MAX_ALLOCS];
static int       g_fa_next;          /* next iommu window index */
static int       g_live;             /* live allocations */
static int       g_palloc_calls;
static int       g_palloc_nc_calls;
static int       g_pfree_calls;
static int       g_pfree_bad;        /* pfree of an unknown pointer or wrong ve args */
static int       g_flush_calls;
static void     *g_flush_last_mem;
static int       g_flush_last_size;
static int       g_fail_above;       /* >0: palloc fails when size exceeds this */
static int       g_fail_countdown;   /* >0: this many successes, then every call fails */
static int       g_last_req_size;
static void     *g_expect_ve_ops;
static void     *g_expect_ve_self;

static void fake_reset(void)
{
    memset(g_fa, 0, sizeof(g_fa));
    g_fa_next = 0;
    g_live = 0;
    g_palloc_calls = g_palloc_nc_calls = g_pfree_calls = g_pfree_bad = 0;
    g_flush_calls = 0;
    g_flush_last_mem = NULL;
    g_flush_last_size = 0;
    g_fail_above = 0;
    g_fail_countdown = 0;
    g_last_req_size = 0;
}

static void *fake_alloc_common(int size, void *ve_ops, void *ve_self, int cached)
{
    int i;

    g_last_req_size = size;
    if (ve_ops != g_expect_ve_ops || ve_self != g_expect_ve_self)
        g_pfree_bad++;
    if (size <= 0)
        return NULL;
    if (g_fail_above > 0 && size > g_fail_above)
        return NULL;
    if (g_fail_countdown > 0 && --g_fail_countdown == 0) {
        g_fail_countdown = -1;
        return NULL;
    }
    if (g_fail_countdown < 0)
        return NULL;
    for (i = 0; i < FAKE_MAX_ALLOCS; i++) {
        if (!g_fa[i].vir) {
            g_fa[i].vir = malloc((size_t)size);
            if (!g_fa[i].vir)
                return NULL;
            g_fa[i].size = size;
            g_fa[i].iommu = FAKE_IOMMU_BASE + (uintptr_t)g_fa_next++ * FAKE_IOMMU_STEP;
            g_fa[i].cached = cached;
            g_live++;
            return g_fa[i].vir;
        }
    }
    return NULL;
}

static void *fake_palloc(int size, void *ve_ops, void *ve_self)
{
    g_palloc_calls++;
    return fake_alloc_common(size, ve_ops, ve_self, 1);
}

static void *fake_palloc_nc(int size, void *ve_ops, void *ve_self)
{
    g_palloc_nc_calls++;
    return fake_alloc_common(size, ve_ops, ve_self, 0);
}

static void fake_pfree(void *mem, void *ve_ops, void *ve_self)
{
    int i;

    g_pfree_calls++;
    if (ve_ops != g_expect_ve_ops || ve_self != g_expect_ve_self)
        g_pfree_bad++;
    for (i = 0; i < FAKE_MAX_ALLOCS; i++) {
        if (g_fa[i].vir && g_fa[i].vir == mem) {
            free(g_fa[i].vir);
            g_fa[i].vir = NULL;
            g_live--;
            return;
        }
    }
    g_pfree_bad++;
}

static void fake_flush(void *mem, int size)
{
    g_flush_calls++;
    g_flush_last_mem = mem;
    g_flush_last_size = size;
}

static void *fake_cpu_phy(void *vir)
{
    int i;
    uintptr_t v = (uintptr_t)vir;

    for (i = 0; i < FAKE_MAX_ALLOCS; i++) {
        uintptr_t s = (uintptr_t)g_fa[i].vir;

        if (g_fa[i].vir && v >= s && v < s + (uintptr_t)g_fa[i].size)
            return (void *)(g_fa[i].iommu + (v - s));
    }
    return NULL;
}

static void *fake_ve_phy(void *vir)
{
    uintptr_t p = (uintptr_t)fake_cpu_phy(vir);

    return p ? (void *)(p - FAKE_VE_OFFSET) : NULL;
}

/* Only the slots the managers may use are filled; the rest stay NULL so a stray
 * call crashes the test. */
static struct vb_mem_ops g_fake_ops = {
    .palloc          = fake_palloc,
    .palloc_no_cache = fake_palloc_nc,
    .pfree           = fake_pfree,
    .flush_cache     = fake_flush,
    .ve_get_phyaddr  = fake_ve_phy,
    .cpu_get_phyaddr = fake_cpu_phy,
};

static int g_fail;

static void check(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_fail = 1;
    }
}

#endif /* FREECODEC_TEST_FAKE_MEMOPS_H */
