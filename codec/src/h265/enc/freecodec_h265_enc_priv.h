/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Private state of the H.265 encoder device (spec h265/12-encoder-device.md).
 * Included only by the device and its register-access translation unit. The VE
 * driver (src/h264/ve/ve_driver.c) and the encoder-internal ISP
 * (src/h264/isp/h264_isp.c) are reused unchanged (00-index.md section 2). */

#ifndef FREECODEC_H265_ENC_PRIV_H
#define FREECODEC_H265_ENC_PRIV_H

#include <stdint.h>

#include "freecodec/h264_bufs.h"
#include "freecodec/h264_isp.h"
#include "freecodec/h265_headers.h"
#include "freecodec/h265_rc.h"
#include "freecodec/h265_regs.h"
#include "freecodec/venc_ext.h"
#include "freecodec/vencoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All register-window access is volatile and lives in its own translation unit. */
uint32_t fc_h265_reg_read(volatile uint32_t *base, unsigned int word_off);
void     fc_h265_reg_write(volatile uint32_t *base, unsigned int word_off, uint32_t val);

typedef struct fc_h265_geom {
    unsigned int in_w, in_h, in_stride;
    unsigned int dst_w, dst_h;
    unsigned int w16, h16;      /* coded size rounded up to 16                  */
    unsigned int w_mb, h_mb;    /* w16/16, h16/16                               */
} fc_h265_geom;

typedef struct fc_h265_buf {
    void        *vir;
    void        *phy;
    unsigned int size;
} fc_h265_buf;

typedef struct fc_h265_buffers {
    vb_bitstream_manager *bs;
    fc_h265_buf   full[2];      /* luma + chroma ping-pong (spec 11 section 6)   */
    unsigned int  luma_size, chroma_size;
    fc_h265_buf   aux[2];       /* auxiliary reference / reconstruction planes   */
    fc_h265_buf   tmvp[2];      /* TMVP workspaces, live from the second P       */
    fc_h265_buf   mb_info, dblk, ctu_info;
    fc_h265_buf   mbrc;
    fc_h265_buf   filt3d[2];    /* 3-D filter reference/reconstruction planes */
    unsigned char header[192];  /* VPS + SPS + PPS, 90 bytes at both geometries  */
    unsigned int  header_len;
    /* Burned-in OSD overlay (shared encoder-internal ISP): grow-only header and
     * data buffers plus the luma-invert scratch. */
    fc_h265_buf   ovl_hdr, ovl_data, ovl_inv;
} fc_h265_buffers;

typedef struct fc_h265_params {
    unsigned int  bitrate;
    unsigned int  framerate;
    unsigned int  max_key_interval;
    int           force_key;
    unsigned int  stride;
    fwm_venc_pixel_format_e color_fmt;
    /* LBC input: the shared encoder-internal ISP needs these to program the
     * compressed-input pattern/stride (12 section 8 / spec 14). */
    unsigned int  lbc_lossy_2x;
    unsigned int  lbc_lossy_2_5x;
    unsigned int  vbv_size;
    int           vbv_no_cache;

    fwm_venc_h265_profile_level_t profile_level;
    int           qp_min, qp_max;
    int           qp_init;
    int           min_i_qp;
    fwm_venc_h265_rc_mode_e rc_mode;

    /* Forced by the H265Param handler (10 section 5). */
    unsigned int  idr_period, intra_period, gop_size;

    fwm_venc_h265_gop_t      gop;
    fwm_venc_h265_transform_t transform;
    fwm_venc_h265_sao_t      sao;
    fwm_venc_h265_deblock_t  deblock;
    fwm_venc_h265_timing_t   timing;
    fwm_venc_h265_tendency_t tendency;

    unsigned int  th_bright, th_dark;
    int           fixed_qp_enable;
    int           fixed_i_qp, fixed_p_qp;
    int           fast_enc;

    /* Encoder 3-D (temporal noise) filter (11 sections 3.2/3.14; 3D-filter
     * spec): level 0 off / 1..6, an optional direct strength, and T carried
     * across pictures. */
    unsigned int  filter_3d_level;
    unsigned int  filter_3d_strength;
    unsigned int  rc_track;           /* target-tracking RC (extension)      */
    unsigned int  filt3d_t;
    int           dyn_me;       /* dynamic-ME enable, latched at init (3D spec 6) */

    /* Displayed window smaller than the coded picture, via the SPS conformance
     * window (venc_ext.h; same semantics as the H.264 device). */
    fwm_venc_display_size_t   display_size;
    fwm_venc_display_offset_t display_offset;
} fc_h265_params;

typedef struct fc_h265_instance {
    fc_ve_ops   *ve_ops;
    void        *ve_self;
    volatile uint32_t *enc_base;   /* engine window + 0xb00, word addressed */
    volatile uint32_t *isp_base;   /* engine window + 0xa00, word addressed */
    unsigned int ic_version;
    unsigned int dram_type;
    struct vb_mem_ops *memops;

    fc_h265_geom     geom;
    fc_h265_params   params;
    fc_h265_buffers  bufs;
    freecodec_h265_rc rc;

    freecodec_h264_isp *isp;
    freecodec_h264_isp_info ovl;   /* only the overlay fields are used         */
    unsigned int capability[9];    /* the matched capability record            */

    unsigned int pic_count;        /* pictures encoded since init             */
    unsigned int poc_lsb;
    unsigned int frame_num;
    int          first_picture;
    int          initialised;

    void (*trace)(void *opaque, unsigned int block, unsigned int off, uint32_t val);
    void *trace_opaque;
} fc_h265_instance;

unsigned int fc_h265_align_up(unsigned int x, unsigned int n);

/* Test-only register-write observer (block 0 = encoder, 1 = ISP). */
void fc_h265_set_trace(void *handle,
                       void (*trace)(void *opaque, unsigned int block,
                                     unsigned int off, uint32_t val),
                       void *opaque);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H265_ENC_PRIV_H */
