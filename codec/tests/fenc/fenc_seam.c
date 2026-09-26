/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Seam definitions for the package C host tests (see fenc_seam.h). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/ve_iface.h"
#include "freecodec/venc_base_abi.h"

#include "fenc_seam.h"

/* ------------------------------------------------------------ event log */

char seam_events[SEAM_EV_MAX][24];
int  seam_event_n;

void seam_log(const char *tag)
{
    if (seam_event_n < SEAM_EV_MAX) {
        snprintf(seam_events[seam_event_n], sizeof(seam_events[0]), "%s", tag);
        seam_event_n++;
    }
}

void seam_events_reset(void)
{
    seam_event_n = 0;
}

int seam_event_index(const char *tag)
{
    int i;

    for (i = 0; i < seam_event_n; i++)
        if (strcmp(seam_events[i], tag) == 0)
            return i;
    return -1;
}

/* --------------------------------------------------------- device fakes */

struct seam_dev seam_ver2, seam_ver1, seam_h265, seam_jpeg;

static struct seam_dev *dev_of(void *handle)
{
    return (struct seam_dev *)handle;
}

static void *dev_open(struct seam_dev *d, VencBaseConfig *cfg, unsigned int ic)
{
    d->open_n++;
    d->open_ic = ic;
    if (cfg)
        d->open_cfg = *cfg;
    seam_log("open");
    return d->open_ret;
}

static void *ver2_open(VencBaseConfig *cfg, unsigned int ic)
{
    return dev_open(&seam_ver2, cfg, ic);
}
static void *ver1_open(VencBaseConfig *cfg, unsigned int ic)
{
    return dev_open(&seam_ver1, cfg, ic);
}
static void *h265_open(VencBaseConfig *cfg, unsigned int ic)
{
    return dev_open(&seam_h265, cfg, ic);
}
static void *jpeg_open(VencBaseConfig *cfg, unsigned int ic)
{
    return dev_open(&seam_jpeg, cfg, ic);
}

static int dev_init(void *h, VencBaseConfig *cfg)
{
    struct seam_dev *d = dev_of(h);

    d->init_n++;
    if (cfg)
        d->init_cfg = *cfg;
    seam_log("init");
    return d->init_ret;
}

static int dev_uninit(void *h)
{
    struct seam_dev *d = dev_of(h);

    d->uninit_n++;
    seam_log("uninit");
    return 0;
}

static void dev_close(void *h)
{
    struct seam_dev *d = dev_of(h);

    d->close_n++;
    seam_log("close");
}

static int dev_encode(void *h, VencInputBuffer *in)
{
    struct seam_dev *d = dev_of(h);

    d->encode_n++;
    if (in)
        d->encode_buf = *in;
    seam_log("encode");
    return d->encode_ret;
}

static int dev_getparam(void *h, int index, void *param)
{
    struct seam_dev *d = dev_of(h);

    d->getparam_n++;
    d->getparam_index = index;
    d->getparam_ptr = param;
    seam_log("getparam");
    return d->getparam_ret;
}

static int dev_setparam(void *h, int index, void *param)
{
    struct seam_dev *d = dev_of(h);

    d->setparam_n++;
    d->setparam_index = index;
    d->setparam_ptr = param;
    seam_log("setparam");
    return d->setparam_ret;
}

static int dev_valid(void *h)
{
    struct seam_dev *d = dev_of(h);

    d->valid_n++;
    seam_log("valid");
    return d->valid_ret;
}

static int dev_getone(void *h, VencOutputBuffer *out)
{
    struct seam_dev *d = dev_of(h);

    d->getone_n++;
    seam_log("getone");
    if (d->getone_ret == 0 && out) {
        memset(out, 0, sizeof(*out));
        out->nID    = 0x1234;
        out->nSize0 = 111;
        out->nSize1 = 222;
        out->pData0 = (unsigned char *)(uintptr_t)0x5000u;
    }
    return d->getone_ret;
}

static int dev_freeone(void *h, VencOutputBuffer *out)
{
    struct seam_dev *d = dev_of(h);

    d->freeone_n++;
    if (out)
        d->free_buf = *out;
    seam_log("freeone");
    return d->freeone_ret;
}

static int dev_reset(void *h)
{
    struct seam_dev *d = dev_of(h);

    d->reset_n++;
    seam_log("reset");
    return d->reset_ret;
}

/* Four device tables with distinct identities, so selection is observable. */
VENC_DEVICE video_encoder_h264_ver2 = {
    "seam ver2", ver2_open, dev_init, dev_uninit, dev_close, dev_encode,
    dev_getparam, dev_setparam, dev_valid, dev_getone, dev_freeone, dev_reset
};
VENC_DEVICE video_encoder_h264_ver1 = {
    "seam ver1", ver1_open, dev_init, dev_uninit, dev_close, dev_encode,
    dev_getparam, dev_setparam, dev_valid, dev_getone, dev_freeone, dev_reset
};
VENC_DEVICE video_encoder_h265 = {
    "seam h265", h265_open, dev_init, dev_uninit, dev_close, dev_encode,
    dev_getparam, dev_setparam, dev_valid, dev_getone, dev_freeone, dev_reset
};
VENC_DEVICE video_encoder_jpeg = {
    "seam jpeg", jpeg_open, dev_init, dev_uninit, dev_close, dev_encode,
    dev_getparam, dev_setparam, dev_valid, dev_getone, dev_freeone, dev_reset
};

/* --------------------------------------------------------- video engine */

int seam_ve_init_n, seam_ve_release_n, seam_ve_lock_n, seam_ve_unlock_n;
int seam_ve_getgroup_n, seam_ve_perf_init_n, seam_ve_perf_uninit_n;
int seam_last_perf_mode;
int seam_last_ve_dec, seam_last_ve_enc, seam_last_ve_afbc, seam_last_ve_reset;
unsigned int seam_last_ve_format;
int seam_iommu_n, seam_ddr_n;
int seam_speed_last;

static int g_ve_ops_null;
static int g_ve_init_null;
static int g_ve_self_token;
static int g_top_token;
void *seam_ve_self = &g_ve_self_token;

static void *ve_init(fc_ve_config *cfg)
{
    seam_ve_init_n++;
    seam_last_ve_dec    = cfg->use_decoder;
    seam_last_ve_enc    = cfg->use_encoder;
    seam_last_ve_format = (unsigned int)cfg->work_mode;
    seam_last_ve_afbc   = cfg->fbc_enable;
    seam_last_ve_reset  = cfg->reset_mode;
    return g_ve_init_null ? NULL : &g_ve_self_token;
}

static void ve_release(void *h)
{
    (void)h;
    seam_ve_release_n++;
    seam_log("ve_release");
}

static int ve_lock(void *h)
{
    (void)h;
    seam_ve_lock_n++;
    seam_log("ve_lock");
    return 0;
}

static int ve_unlock(void *h)
{
    (void)h;
    seam_ve_unlock_n++;
    seam_log("ve_unlock");
    return 0;
}

static void *ve_getgroup(void *h, int group)
{
    (void)h;
    (void)group;
    seam_ve_getgroup_n++;
    return &g_top_token;
}

static void ve_perf_init(void *h, int mode)
{
    (void)h;
    seam_ve_perf_init_n++;
    seam_last_perf_mode = mode;
}

static void ve_perf_uninit(void *h, int mode)
{
    (void)h;
    seam_ve_perf_uninit_n++;
    seam_last_perf_mode = mode;
}

static int ve_get_iommu(void *h, fc_ve_iommu_req *p)
{
    (void)h;
    (void)p;
    seam_iommu_n++;
    return 0;
}

static int ve_free_iommu(void *h, fc_ve_iommu_req *p)
{
    (void)h;
    (void)p;
    seam_iommu_n++;
    return 0;
}

static void ve_set_ddr(void *h, int mode)
{
    (void)h;
    seam_ddr_n++;
    (void)mode;
}

static int ve_set_speed(void *h, unsigned int freq)
{
    (void)h;
    seam_speed_last = (int)freq;
    return 0;
}

fc_ve_ops seam_ve_ops = {
    .open          = ve_init,
    .close         = ve_release,
    .lock          = ve_lock,
    .unlock        = ve_unlock,
    .group_base    = ve_getgroup,
    .set_ddr_mode  = ve_set_ddr,
    .set_clock     = ve_set_speed,
    .perf_setup    = ve_perf_init,
    .perf_teardown = ve_perf_uninit,
    .iommu_map     = ve_get_iommu,
    .iommu_unmap   = ve_free_iommu,
};

fc_ve_ops *GetVeOpsS(int type)
{
    (void)type;
    return g_ve_ops_null ? NULL : &seam_ve_ops;
}

/* --------------------------------------------------- adapter functions */

static unsigned int g_ic_version = 0x1708u;
static unsigned int g_ve_offset;
static int g_init_mem_rc;
static int g_memops_null;

int seam_mem_init_n, seam_mem_release_n;

unsigned int EncAdapterGetICVersion(void *ve_top_base)
{
    (void)ve_top_base;
    return g_ic_version;
}

unsigned int __EncAdapterMemGetVeAddrOffset(struct vb_mem_ops *memops)
{
    (void)memops;
    return g_ve_offset;
}

int EncAdapterInitializeMem(struct vb_mem_ops *memops)
{
    (void)memops;
    seam_mem_init_n++;
    seam_log("mem_init");
    return g_init_mem_rc;
}

void EncAdpaterRelease(struct vb_mem_ops *memops)
{
    (void)memops;
    seam_mem_release_n++;
    seam_log("mem_release");
}

static struct vb_mem_ops g_seam_memops;
void *seam_fake_memops = &g_seam_memops;

/* A small malloc-backed table so the library-allocated picture pool works in
 * host tests. Only the slots the manager touches are filled. */
static void *seam_palloc(int size, void *ve_ops, void *ve_self)
{
    (void)ve_ops;
    (void)ve_self;
    return size > 0 ? malloc((size_t)size) : NULL;
}

static void *seam_palloc_nc(int size, void *ve_ops, void *ve_self)
{
    return seam_palloc(size, ve_ops, ve_self);
}

static void seam_pfree(void *mem, void *ve_ops, void *ve_self)
{
    (void)ve_ops;
    (void)ve_self;
    free(mem);
}

static void seam_flush(void *mem, int size)
{
    (void)mem;
    (void)size;
}

static void *seam_cpu_phy(void *vir)
{
    return vir;
}

static unsigned int seam_get_offset(void)
{
    return g_ve_offset;
}

static struct vb_mem_ops g_seam_memops = {
    .palloc             = seam_palloc,
    .palloc_no_cache    = seam_palloc_nc,
    .pfree              = seam_pfree,
    .flush_cache        = seam_flush,
    .cpu_get_phyaddr    = seam_cpu_phy,
    .get_ve_addr_offset = seam_get_offset,
};

struct vb_mem_ops *MemAdapterGetOpsS(void)
{
    return g_memops_null ? NULL : &g_seam_memops;
}

/* ------------------------------------------------------------- controls */

static void dev_reset_defaults(struct seam_dev *d, const char *name)
{
    (void)name;
    memset(d, 0, sizeof(*d));
    d->open_ret = d;
}

void seam_reset(void)
{
    dev_reset_defaults(&seam_ver2, "ver2");
    dev_reset_defaults(&seam_ver1, "ver1");
    dev_reset_defaults(&seam_h265, "h265");
    dev_reset_defaults(&seam_jpeg, "jpeg");

    seam_events_reset();

    seam_ve_init_n = seam_ve_release_n = seam_ve_lock_n = seam_ve_unlock_n = 0;
    seam_ve_getgroup_n = seam_ve_perf_init_n = seam_ve_perf_uninit_n = 0;
    seam_mem_init_n = seam_mem_release_n = 0;
    seam_last_perf_mode = 0;
    seam_last_ve_dec = seam_last_ve_enc = seam_last_ve_afbc = seam_last_ve_reset = 0;
    seam_last_ve_format = 0;
    seam_iommu_n = seam_ddr_n = 0;
    seam_speed_last = 0;

    g_ve_ops_null = 0;
    g_ve_init_null = 0;
    g_ic_version = 0x1708u;
    g_ve_offset = 0;
    g_init_mem_rc = 0;
    g_memops_null = 0;
}

void seam_set_ic_version(unsigned int v)
{
    g_ic_version = v;
}

void seam_set_ve_ops_null(int yes)
{
    g_ve_ops_null = yes;
}

void seam_set_ve_init_null(int yes)
{
    g_ve_init_null = yes;
}

void seam_set_memops_null(int yes)
{
    g_memops_null = yes;
}

void seam_set_init_mem_rc(int rc)
{
    g_init_mem_rc = rc;
}

void seam_set_ve_offset(unsigned int off)
{
    g_ve_offset = off;
}
