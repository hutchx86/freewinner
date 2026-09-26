/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Encoder adapter functions (SPEC §2): thin forwarding onto a vb_mem_ops table
 * and the IC-version probe on the mapped video-engine top registers. */

#include <stdint.h>
#include <stdio.h>

#include "freecodec/venc_base_abi.h"

#define LOG(...) fprintf(stderr, "venc_base: " __VA_ARGS__)

/* Byte offsets within the video-engine top register block. */
#define VE_TOP_REG_IC_VERSION     0xf0u   /* version in bits [31:16] */
#define VE_TOP_REG_IC_VERSION_ALT 0xe4u   /* fallback when the above reads 0 */

int EncAdapterInitializeMem(struct vb_mem_ops *memops)
{
    if (!memops || !memops->open)
        return -1;
    return memops->open() < 0 ? -1 : 0;
}

void EncAdpaterRelease(struct vb_mem_ops *memops)
{
    if (memops && memops->close)
        memops->close();
}

unsigned int EncAdapterGetICVersion(void *ve_top_base)
{
    const volatile uint8_t *top = ve_top_base;
    uint32_t v;

    if (!top)
        return 0;
    v = *(const volatile uint32_t *)(top + VE_TOP_REG_IC_VERSION);
    if (v)
        return v >> 16;
    v = *(const volatile uint32_t *)(top + VE_TOP_REG_IC_VERSION_ALT);
    if (!v)
        LOG("IC version registers read as zero\n");
    return v;
}

void *__EncAdapterMemPalloc(struct vb_mem_ops *memops, int size, void *ve_ops,
                            void *ve_self)
{
    if (!memops || !memops->palloc)
        return NULL;
    return memops->palloc(size, ve_ops, ve_self);
}

void __EncAdapterMemPfree(struct vb_mem_ops *memops, void *mem, void *ve_ops,
                          void *ve_self)
{
    if (memops && memops->pfree)
        memops->pfree(mem, ve_ops, ve_self);
}

void __EncAdapterMemFlushCache(struct vb_mem_ops *memops, void *mem, int size)
{
    if (memops && memops->flush_cache)
        memops->flush_cache(mem, size);
}

void *__EncAdapterMemGetPhysicAddress(struct vb_mem_ops *memops, void *vir)
{
    if (!memops || !memops->ve_get_phyaddr)
        return NULL;
    return memops->ve_get_phyaddr(vir);
}

unsigned int __EncAdapterMemGetVeAddrOffset(struct vb_mem_ops *memops)
{
    if (!memops || !memops->get_ve_addr_offset)
        return 0;
    return memops->get_ve_addr_offset();
}
