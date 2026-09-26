/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host-test seam for the encoder API framework (package C), same link-time
 * substitution style as tests/base/fake_memops.h. It supplies everything the
 * framework imports and scripts: the video-engine ops table, the adapter
 * calls, the memory-ops table, and the four codec device tables (fakes whose
 * slots record arguments and return scripted results). The real picture-queue
 * manager (src/base/vb_frames.c) is linked, not mocked. */

#ifndef FREECODEC_TEST_FENC_SEAM_H
#define FREECODEC_TEST_FENC_SEAM_H

#include "freecodec/ve_iface.h"
#include "freecodec/venc_base_abi.h"

#include "fenc_abi.h"

/* One scripted codec device. `open` returns `open_ret` (default: the device's
 * own address, which doubles as the handle every later slot receives). */
struct seam_dev {
    int open_n, init_n, uninit_n, close_n, encode_n;
    int getparam_n, setparam_n, valid_n, getone_n, freeone_n, reset_n;

    void *open_ret;
    int   init_ret, encode_ret, getparam_ret, setparam_ret;
    int   valid_ret, getone_ret, freeone_ret, reset_ret;

    unsigned int   open_ic;
    VencBaseConfig open_cfg;       /* copy seen by open */
    VencBaseConfig init_cfg;       /* copy seen by init */
    VencInputBuffer encode_buf;    /* copy seen by encode */

    int   setparam_index; void *setparam_ptr;
    int   getparam_index; void *getparam_ptr;

    VencOutputBuffer free_buf;     /* copy seen by free */
};

extern struct seam_dev seam_ver2, seam_ver1, seam_h265, seam_jpeg;

/* Ordered event log: every scripted slot appends a short tag. */
#define SEAM_EV_MAX 64
extern char seam_events[SEAM_EV_MAX][24];
extern int  seam_event_n;
void seam_events_reset(void);
int  seam_event_index(const char *tag);   /* first index of tag, -1 if absent */
void seam_log(const char *tag);           /* append one tag (also usable by tests) */

/* Video-engine ops table handed to the framework by GetVeOpsS. */
extern fc_ve_ops seam_ve_ops;
extern void *seam_ve_self;                 /* non-NULL instance token */

/* Control / observation for the imported functions. */
void seam_reset(void);                     /* clear everything to defaults */
void seam_set_ic_version(unsigned int v);  /* EncAdapterGetICVersion value */
void seam_set_ve_ops_null(int yes);        /* GetVeOpsS -> NULL */
void seam_set_ve_init_null(int yes);       /* ve->init -> NULL */
void seam_set_memops_null(int yes);        /* MemAdapterGetOpsS -> NULL */
void seam_set_init_mem_rc(int rc);         /* EncAdapterInitializeMem result */
void seam_set_ve_offset(unsigned int off); /* __EncAdapterMemGetVeAddrOffset */

extern int seam_ve_init_n, seam_ve_release_n, seam_ve_lock_n, seam_ve_unlock_n;
extern int seam_ve_getgroup_n, seam_ve_perf_init_n, seam_ve_perf_uninit_n;
extern int seam_mem_init_n, seam_mem_release_n;
extern int seam_last_perf_mode;
extern int seam_last_ve_dec, seam_last_ve_enc, seam_last_ve_afbc, seam_last_ve_reset;
extern unsigned int seam_last_ve_format;
extern int seam_iommu_n, seam_ddr_n;
extern int seam_speed_last;
extern void *seam_fake_memops;             /* table MemAdapterGetOpsS returns */

#endif /* FREECODEC_TEST_FENC_SEAM_H */
