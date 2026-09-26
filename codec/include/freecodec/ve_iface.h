/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Video-engine driver interface (spec r2/05 part A).
 *
 * The engine is shared by every codec library of the process. Access goes
 * through one table of operations obtained with GetVeOpsS(); each operation
 * takes the instance returned by the table's `open` slot. The table layout,
 * the kernel control codes and the device-node names are binary facts; all
 * C identifiers here except GetVeOpsS are local to this project. */

#ifndef FREECODEC_VE_IFACE_H
#define FREECODEC_VE_IFACE_H

#include <stddef.h>
#include <stdint.h>

#include "freecodec/abi_check.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------ fixed constants */

#define FC_VE_NODE          "/dev/cedar_dev"       /* engine device node      */
#define FC_SOC_INFO_NODE    "/dev/sunxi_soc_info"  /* SoC information node    */
#define FC_VE_WINDOW_BYTES  0x800u                 /* mmap'd register window  */

/* Kind argument of GetVeOpsS: only the default table exists. */
#define FC_VE_OPS_DEFAULT   0

/* Register groups inside the window (argument of the `group_base` slot). */
typedef enum fc_ve_group {
    FC_VE_GROUP_TOP  = 0,   /* top-level engine control (encoder lives here) */
    FC_VE_GROUP_MPEG = 1,   /* MPEG decoder                                  */
    FC_VE_GROUP_H264 = 2,   /* H.264 decoder                                 */
    FC_VE_GROUP_AVS  = 2,   /* AVS shares the H.264 decoder group            */
    FC_VE_GROUP_VC1  = 3,   /* VC-1 decoder                                  */
    FC_VE_GROUP_RV   = 4,   /* RealVideo decoder                             */
    FC_VE_GROUP_H265 = 5,   /* H.265 decoder                                 */
    FC_VE_GROUP_JPEG = 6    /* JPEG                                          */
} fc_ve_group;

/* Engine configuration: work mode and reset mode values. */
typedef enum fc_ve_work_mode {
    FC_VE_WORK_NORMAL      = 0,
    FC_VE_WORK_DECODE      = 1,
    FC_VE_WORK_ENCODE      = 2,
    FC_VE_WORK_JPEG_DECODE = 3
} fc_ve_work_mode;

typedef enum fc_ve_reset_mode {
    FC_VE_RESET_MODE_NORMAL  = 0,
    FC_VE_RESET_MODE_SPECIAL = 1
} fc_ve_reset_mode;

/* Control codes of the engine device node (kernel interface). */
typedef enum fc_ve_ioc {
    FC_VE_IOC_PLACEHOLDER     = 0x100,  /* unused/unknown                    */
    FC_VE_IOC_GET_ENV         = 0x101,  /* environment: window base etc.     */
    FC_VE_IOC_WAIT_DEC        = 0x102,  /* wait for decoder completion       */
    FC_VE_IOC_WAIT_ENC        = 0x103,  /* wait for the encoder interrupt    */
    FC_VE_IOC_RESET           = 0x104,  /* reset the engine                  */
    FC_VE_IOC_CLK_ON          = 0x105,  /* enable the engine clock           */
    FC_VE_IOC_CLK_OFF         = 0x106,  /* disable the engine clock          */
    FC_VE_IOC_SET_CLK         = 0x107,  /* set the engine clock frequency    */
    FC_VE_IOC_REQUEST         = 0x206,  /* request the engine                */
    FC_VE_IOC_RELEASE         = 0x207,  /* release the engine                */
    FC_VE_IOC_CHECK_DELAY     = 0x208,  /* engine check delay                */
    FC_VE_IOC_IC_VERSION      = 0x209,  /* read the IC version               */
    FC_VE_IOC_FLUSH_RANGE     = 0x20b,  /* cache flush over a range          */
    FC_VE_IOC_SET_REFCOUNT    = 0x20c,  /* set the reference count           */
    FC_VE_IOC_FLUSH_ALL       = 0x20d,  /* flush the whole cache             */
    FC_VE_IOC_DRV_VERSION     = 0x20e,  /* test the driver version           */
    FC_VE_IOC_LOCK            = 0x310,  /* take the engine lock              */
    FC_VE_IOC_UNLOCK          = 0x311,  /* drop the engine lock              */
    FC_VE_IOC_SET_VOLTAGE     = 0x400,  /* set the voltage                   */
    FC_VE_IOC_WAIT_JPEG_DEC   = 0x500,  /* wait for a JPEG decode            */
    FC_VE_IOC_GET_REFCOUNT    = 0x501,  /* read the reference count          */
    FC_VE_IOC_IOMMU_MAP       = 0x502,  /* map a buffer into the engine IOMMU */
    FC_VE_IOC_IOMMU_UNMAP     = 0x503,  /* unmap it again                    */
    FC_VE_IOC_CHAN_INFO_SET   = 0x504,  /* set per-channel info text         */
    FC_VE_IOC_CHAN_INFO_STOP  = 0x505,  /* stop per-channel info             */
    FC_VE_IOC_CHAN_INFO_COPY  = 0x506,  /* copy per-channel info             */
    FC_VE_IOC_DRAM_HIGH_CHAN  = 0x600,  /* set the DRAM high channel         */
    FC_VE_IOC_DDR_READ        = 0x601,  /* read a DDR value                  */
    FC_VE_IOC_DDR_WRITE       = 0x602,  /* write a DDR value                 */
    FC_VE_IOC_DDR_CLEAR       = 0x603,  /* clear a DDR value                 */
    FC_VE_IOC_POWER_SETUP     = 0x700,  /* power set-up                      */
    FC_VE_IOC_POWER_DOWN      = 0x701   /* power shut-down                   */
} fc_ve_ioc;

/* --------------------------------------------------------------- records */

/* What the caller intends to do with the engine (28 bytes). */
typedef struct fc_ve_config {
    int          use_decoder;   /* non-zero: a decoder will run           */
    int          use_encoder;   /* non-zero: an encoder will run          */
    int          work_mode;     /* fc_ve_work_mode                        */
    int          width_hint;    /* picture width, px (0 = unknown)        */
    int          fbc_enable;    /* frame-buffer compression               */
    int          reset_mode;    /* fc_ve_reset_mode                       */
    unsigned int clock_mhz;     /* requested engine clock; 0 = default    */
} fc_ve_config;

/* IOMMU mapping request (8 bytes): a dma-buf in, its engine address out. */
typedef struct fc_ve_iommu_req {
    int          dmabuf_fd;
    unsigned int engine_addr;
} fc_ve_iommu_req;

/* Header of a per-channel information text (8 bytes). */
typedef struct fc_ve_chan_info {
    unsigned char channel;
    unsigned int  length;
} fc_ve_chan_info;

/* Operations table: exactly 24 function-pointer slots, slot n at 4 * n on the
 * 32-bit target (spec r2/05 A.3). All but `open` take the instance first. */
typedef struct fc_ve_ops {
    void        *(*open)(fc_ve_config *cfg);        /*  0: instance or NULL  */
    void         (*close)(void *ve);                /*  1                    */
    int          (*lock)(void *ve);                 /*  2: process-wide lock */
    int          (*unlock)(void *ve);               /*  3                    */
    void         (*reset)(void *ve);                /*  4: pulse the reset   */
    int          (*wait_irq)(void *ve);             /*  5: 0 = interrupt,
                                                           -1 = timeout/error */
    int          (*chip_id)(void *ve);              /*  6                    */
    uint64_t     (*ic_version)(void *ve);           /*  7                    */
    void        *(*group_base)(void *ve, int group);/*  8: fc_ve_group base  */
    int          (*dram_type)(void *ve);            /*  9                    */
    unsigned int (*phys_offset)(void *ve);          /* 10: CPU -> engine     */
    void         (*apply_dram_type)(void *ve);      /* 11                    */
    void         (*set_ddr_mode)(void *ve, int mode);          /* 12         */
    int          (*set_clock)(void *ve, unsigned int mhz);     /* 13         */
    void         (*set_fbc)(void *ve, int on);                 /* 14         */
    void         (*set_dram_adjust)(void *ve, int on);         /* 15         */
    void         (*enc_enable)(void *ve);           /* 16: encoder on in the
                                                           top control word  */
    void         (*enc_disable)(void *ve);          /* 17                    */
    void         (*perf_setup)(void *ve, int mode);             /* 18        */
    void         (*perf_teardown)(void *ve, int mode);          /* 19        */
    int          (*iommu_map)(void *ve, fc_ve_iommu_req *req);   /* 20       */
    int          (*iommu_unmap)(void *ve, fc_ve_iommu_req *req); /* 21       */
    int          (*chan_info_set)(void *ve, char *text, unsigned int len,
                                  unsigned char channel);        /* 22       */
    int          (*chan_info_stop)(void *ve, unsigned char channel); /* 23   */
} fc_ve_ops;

/* Exported (fixed name): kind FC_VE_OPS_DEFAULT returns the table, any other
 * kind NULL. */
fc_ve_ops *GetVeOpsS(int kind);

/* Convenience: switch the encoder on/off when the table provides it. */
static inline void fc_ve_enc_on(fc_ve_ops *ops, void *ve)
{
    if (ops != NULL && ops->enc_enable != NULL)
        ops->enc_enable(ve);
}

static inline void fc_ve_enc_off(fc_ve_ops *ops, void *ve)
{
    if (ops != NULL && ops->enc_disable != NULL)
        ops->enc_disable(ve);
}

/* ------------------------------------------------------- layout checks */

#if FC_ABI_CHECK_ALL
FC_ABI_SIZE(fc_ve_group, 4);
FC_ABI_SIZE(fc_ve_config, 28);
FC_ABI_OFFSET(fc_ve_config, use_decoder, 0);
FC_ABI_OFFSET(fc_ve_config, use_encoder, 4);
FC_ABI_OFFSET(fc_ve_config, work_mode, 8);
FC_ABI_OFFSET(fc_ve_config, width_hint, 12);
FC_ABI_OFFSET(fc_ve_config, fbc_enable, 16);
FC_ABI_OFFSET(fc_ve_config, reset_mode, 20);
FC_ABI_OFFSET(fc_ve_config, clock_mhz, 24);
FC_ABI_SIZE(fc_ve_iommu_req, 8);
FC_ABI_OFFSET(fc_ve_iommu_req, dmabuf_fd, 0);
FC_ABI_OFFSET(fc_ve_iommu_req, engine_addr, 4);
FC_ABI_SIZE(fc_ve_chan_info, 8);
FC_ABI_OFFSET(fc_ve_chan_info, channel, 0);
FC_ABI_OFFSET(fc_ve_chan_info, length, 4);
/* 24 slots whatever the pointer size. */
FC_ABI_SIZE(fc_ve_ops, 24 * sizeof(void (*)(void)));
#endif

#if FC_ABI_CHECK_PTR32
FC_ABI_SIZE(fc_ve_ops, 96);
FC_ABI_OFFSET(fc_ve_ops, open, 0);
FC_ABI_OFFSET(fc_ve_ops, close, 4);
FC_ABI_OFFSET(fc_ve_ops, lock, 8);
FC_ABI_OFFSET(fc_ve_ops, unlock, 12);
FC_ABI_OFFSET(fc_ve_ops, reset, 16);
FC_ABI_OFFSET(fc_ve_ops, wait_irq, 20);
FC_ABI_OFFSET(fc_ve_ops, chip_id, 24);
FC_ABI_OFFSET(fc_ve_ops, ic_version, 28);
FC_ABI_OFFSET(fc_ve_ops, group_base, 32);
FC_ABI_OFFSET(fc_ve_ops, dram_type, 36);
FC_ABI_OFFSET(fc_ve_ops, phys_offset, 40);
FC_ABI_OFFSET(fc_ve_ops, apply_dram_type, 44);
FC_ABI_OFFSET(fc_ve_ops, set_ddr_mode, 48);
FC_ABI_OFFSET(fc_ve_ops, set_clock, 52);
FC_ABI_OFFSET(fc_ve_ops, set_fbc, 56);
FC_ABI_OFFSET(fc_ve_ops, set_dram_adjust, 60);
FC_ABI_OFFSET(fc_ve_ops, enc_enable, 64);
FC_ABI_OFFSET(fc_ve_ops, enc_disable, 68);
FC_ABI_OFFSET(fc_ve_ops, perf_setup, 72);
FC_ABI_OFFSET(fc_ve_ops, perf_teardown, 76);
FC_ABI_OFFSET(fc_ve_ops, iommu_map, 80);
FC_ABI_OFFSET(fc_ve_ops, iommu_unmap, 84);
FC_ABI_OFFSET(fc_ve_ops, chan_info_set, 88);
FC_ABI_OFFSET(fc_ve_ops, chan_info_stop, 92);
#endif

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_VE_IFACE_H */
