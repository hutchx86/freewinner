/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Process-wide DMA allocator behind MemAdapterGetOpsS(): ION buffers
 * mapped into the process and into the video engine's IOMMU. IOMMU mode only.
 * All kernel/engine calls go through the vb_sys seam. */

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "freecodec/venc_base_abi.h"
#include "freecodec/ve_iface.h"
#include "vb_sys.h"

#define LOG(...) fprintf(stderr, "venc_base: " __VA_ARGS__)

/* One live allocation. */
struct vb_alloc {
    struct vb_alloc                  *next;
    void                             *start;   /* user address */
    size_t                            size;
    uintptr_t                         iommu;   /* engine (IOMMU) address */
    vb_ion_handle                     handle;
    int                               share_fd;
    fc_ve_iommu_req iommu_param;
};

static pthread_mutex_t  g_lock = PTHREAD_MUTEX_INITIALIZER;
static int              g_refs;
static int              g_ion_fd = -1;
static unsigned int     g_phy_offset;
static struct vb_alloc *g_list;

/* ------------------------------------------------------------ lifetime */

static int mem_open(void)
{
    const struct vb_sys *sys = vb_sys_get();
    fc_ve_ops *ve;
    fc_ve_config cfg;
    void *inst;
    int fd;

    pthread_mutex_lock(&g_lock);
    if (g_refs > 0) {
        g_refs++;
        pthread_mutex_unlock(&g_lock);
        return 0;
    }

    ve = sys->get_ve_ops(FC_VE_OPS_DEFAULT);
    if (!ve || !ve->open || !ve->close || !ve->phys_offset) {
        LOG("mem open: no video-engine ops\n");
        goto fail;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.use_decoder = 1;
    inst = ve->open(&cfg);
    if (!inst) {
        LOG("mem open: video-engine init failed\n");
        goto fail;
    }
    g_phy_offset = ve->phys_offset(inst);
    ve->close(inst);

    fd = sys->open(VB_ION_DEV_NODE, O_RDONLY);
    if (fd < 0) {
        LOG("mem open: cannot open " VB_ION_DEV_NODE "\n");
        g_phy_offset = 0;
        goto fail;
    }

    g_ion_fd = fd;
    g_list = NULL;
    g_refs = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;

fail:
    pthread_mutex_unlock(&g_lock);
    return -1;
}

static void mem_close(void)
{
    struct vb_alloc *a, *next;

    pthread_mutex_lock(&g_lock);
    if (g_refs <= 0) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    if (--g_refs == 0) {
        /* Forget remaining records; their memory is the callers'. */
        for (a = g_list; a; a = next) {
            next = a->next;
            free(a);
        }
        g_list = NULL;
        vb_sys_get()->close(g_ion_fd);
        g_ion_fd = -1;
        g_phy_offset = 0;
    }
    pthread_mutex_unlock(&g_lock);
}

/* ---------------------------------------------------------- allocation */

static void *palloc_flags(int size, void *ve_ops, void *ve_self,
                          unsigned int flags)
{
    const struct vb_sys *sys = vb_sys_get();
    fc_ve_ops *ve = ve_ops;
    struct vb_ion_alloc_data alloc;
    struct vb_ion_fd_data share;
    struct vb_ion_handle_data hd;
    struct vb_alloc *a;
    void *addr;
    int rc;

    if (size <= 0)
        return NULL;
    if (!ve || !ve_self || !ve->iommu_map) {
        LOG("palloc: no video-engine ops/instance for IOMMU mapping\n");
        return NULL;
    }

    a = calloc(1, sizeof(*a));
    if (!a) {
        LOG("palloc: out of memory\n");
        return NULL;
    }

    pthread_mutex_lock(&g_lock);
    if (g_refs <= 0) {
        LOG("palloc: allocator not open\n");
        goto fail_record;
    }

    memset(&alloc, 0, sizeof(alloc));
    alloc.len          = (size_t)size;
    alloc.align        = VB_ION_ALLOC_ALIGN;
    alloc.heap_id_mask = VB_ION_HEAP_SYSTEM_MASK | VB_ION_HEAP_CARVEOUT_MASK;
    alloc.flags        = flags;
    if (sys->ioctl(g_ion_fd, VB_ION_IOC_ALLOC, &alloc) < 0) {
        LOG("palloc: ION alloc of %d bytes failed\n", size);
        goto fail_record;
    }

    memset(&share, 0, sizeof(share));
    share.handle = alloc.handle;
    share.fd = -1;
    if (sys->ioctl(g_ion_fd, VB_ION_IOC_MAP, &share) < 0 || share.fd < 0) {
        LOG("palloc: ION map (share) failed\n");
        goto fail_handle;
    }

    addr = sys->mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED,
                     share.fd, 0);
    if (addr == MAP_FAILED || !addr) {
        LOG("palloc: mmap of %d bytes failed\n", size);
        goto fail_fd;
    }

    a->iommu_param.dmabuf_fd = share.fd;
    a->iommu_param.engine_addr = 0;
    rc = ve->iommu_map(ve_self, &a->iommu_param);
    if (rc < 0) {
        LOG("palloc: IOMMU mapping failed (%d)\n", rc);
        goto fail_map;
    }
    if (a->iommu_param.engine_addr & 0xffu) {
        LOG("palloc: IOMMU address 0x%08x not 256-byte aligned\n",
            a->iommu_param.engine_addr);
        goto fail_iommu;
    }

    a->start    = addr;
    a->size     = (size_t)size;
    a->iommu    = a->iommu_param.engine_addr;
    a->handle   = alloc.handle;
    a->share_fd = share.fd;
    a->next     = g_list;
    g_list      = a;
    pthread_mutex_unlock(&g_lock);
    return addr;

fail_iommu:
    if (ve->iommu_unmap)
        ve->iommu_unmap(ve_self, &a->iommu_param);
fail_map:
    sys->munmap(addr, (size_t)size);
fail_fd:
    sys->close(share.fd);
fail_handle:
    hd.handle = alloc.handle;
    sys->ioctl(g_ion_fd, VB_ION_IOC_FREE, &hd);
fail_record:
    pthread_mutex_unlock(&g_lock);
    free(a);
    return NULL;
}

static void *mem_palloc(int size, void *ve_ops, void *ve_self)
{
    return palloc_flags(size, ve_ops, ve_self,
                        VB_ION_FLAG_CACHED | VB_ION_FLAG_CACHED_SYNC);
}

static void *mem_palloc_no_cache(int size, void *ve_ops, void *ve_self)
{
    return palloc_flags(size, ve_ops, ve_self, 0);
}

static void mem_pfree(void *mem, void *ve_ops, void *ve_self)
{
    const struct vb_sys *sys = vb_sys_get();
    fc_ve_ops *ve = ve_ops;
    struct vb_ion_handle_data hd;
    struct vb_alloc **pp, *a;

    if (!mem) {
        LOG("pfree: NULL address\n");
        return;
    }

    pthread_mutex_lock(&g_lock);
    if (g_refs <= 0) {
        pthread_mutex_unlock(&g_lock);
        LOG("pfree: allocator not open\n");
        return;
    }
    for (pp = &g_list; *pp; pp = &(*pp)->next)
        if ((*pp)->start == mem)
            break;
    a = *pp;
    if (!a) {
        pthread_mutex_unlock(&g_lock);
        LOG("pfree: %p is not an allocation\n", mem);
        return;
    }
    *pp = a->next;

    if (ve && ve_self && ve->iommu_unmap)
        ve->iommu_unmap(ve_self, &a->iommu_param);
    sys->munmap(a->start, a->size);
    sys->close(a->share_fd);
    hd.handle = a->handle;
    if (sys->ioctl(g_ion_fd, VB_ION_IOC_FREE, &hd) < 0)
        LOG("pfree: ION free failed\n");
    pthread_mutex_unlock(&g_lock);
    free(a);
}

/* --------------------------------------------------------- translation */

/* Callers hold g_lock. */
static void *cpu_phy_locked(void *vir)
{
    uintptr_t v = (uintptr_t)vir;
    struct vb_alloc *a;

    if (!vir)
        return NULL;
    for (a = g_list; a; a = a->next) {
        uintptr_t s = (uintptr_t)a->start;
        if (v >= s && v - s < a->size)
            return (void *)(a->iommu + (v - s));
    }
    return NULL;
}

static void *cpu_vir_locked(void *phy)
{
    uintptr_t p = (uintptr_t)phy;
    struct vb_alloc *a;

    for (a = g_list; a; a = a->next)
        if (p >= a->iommu && p - a->iommu < a->size)
            return (char *)a->start + (p - a->iommu);
    return NULL;
}

static void *mem_cpu_get_phyaddr(void *vir)
{
    void *r;

    pthread_mutex_lock(&g_lock);
    r = cpu_phy_locked(vir);
    pthread_mutex_unlock(&g_lock);
    if (!r)
        LOG("cpu_get_phyaddr: %p not found\n", vir);
    return r;
}

static void *mem_cpu_get_viraddr(void *phy)
{
    void *r;

    pthread_mutex_lock(&g_lock);
    r = cpu_vir_locked(phy);
    pthread_mutex_unlock(&g_lock);
    if (!r)
        LOG("cpu_get_viraddr: %p not found\n", phy);
    return r;
}

static void *mem_ve_get_phyaddr(void *vir)
{
    void *r;
    unsigned int off;

    pthread_mutex_lock(&g_lock);
    r = cpu_phy_locked(vir);
    off = g_phy_offset;
    pthread_mutex_unlock(&g_lock);
    if (!r) {
        LOG("ve_get_phyaddr: %p not found\n", vir);
        return NULL;
    }
    return (void *)((uintptr_t)r - off);
}

static void *mem_ve_get_viraddr(void *phy)
{
    void *r;

    pthread_mutex_lock(&g_lock);
    r = cpu_vir_locked((void *)((uintptr_t)phy + g_phy_offset));
    pthread_mutex_unlock(&g_lock);
    if (!r)
        LOG("ve_get_viraddr: %p not found\n", phy);
    return r;
}

static unsigned int mem_get_ve_addr_offset(void)
{
    unsigned int off;

    pthread_mutex_lock(&g_lock);
    off = g_refs > 0 ? g_phy_offset : 0;
    pthread_mutex_unlock(&g_lock);
    return off;
}

/* --------------------------------------------------------------- cache */

static void mem_flush_cache(void *mem, int size)
{
    struct vb_ion_cache_range range;

    if (!mem || size <= 0)
        return;

    pthread_mutex_lock(&g_lock);
    if (g_refs > 0) {
        range.start = (long)(uintptr_t)mem;
        range.end   = (long)((uintptr_t)mem + (size_t)size);
        if (vb_sys_get()->ioctl(g_ion_fd, VB_ION_IOC_FLUSH_RANGE,
                                &range) < 0)
            LOG("flush_cache: %p+%d failed\n", mem, size);
    }
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------ remaining slots */

static int mem_set(void *s, int c, size_t n)
{
    memset(s, c, n);
    return 0;
}

static int mem_cpy(void *dst, void *src, size_t n)
{
    memcpy(dst, src, n);
    return 0;
}

static int mem_unsupported(void)
{
    return -1;
}

static void *mem_palloc_secure(int size, void *ve_ops, void *ve_self)
{
    (void)size;
    (void)ve_ops;
    (void)ve_self;
    return NULL;
}

static struct vb_mem_ops g_ops = {
    .open               = mem_open,
    .close              = mem_close,
    .total_size         = mem_unsupported,
    .palloc             = mem_palloc,
    .palloc_no_cache    = mem_palloc_no_cache,
    .pfree              = mem_pfree,
    .flush_cache        = mem_flush_cache,
    .ve_get_phyaddr     = mem_ve_get_phyaddr,
    .ve_get_viraddr     = mem_ve_get_viraddr,
    .cpu_get_phyaddr    = mem_cpu_get_phyaddr,
    .cpu_get_viraddr    = mem_cpu_get_viraddr,
    .mem_set            = mem_set,
    .mem_cpy            = mem_cpy,
    .mem_read           = mem_cpy,
    .mem_write          = mem_cpy,
    .setup              = mem_unsupported,
    .shutdown           = mem_unsupported,
    .palloc_secure      = mem_palloc_secure,
    .get_ve_addr_offset = mem_get_ve_addr_offset,
};

struct vb_mem_ops *MemAdapterGetOpsS(void)
{
    return &g_ops;
}
