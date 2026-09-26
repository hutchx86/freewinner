/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Private state and helpers of the H.264 encoder device (spec/12-encoder-
 * device.md, r4). Not a public interface: only freecodec_enc.c and
 * freecodec_enc_regs.c (the volatile-access translation unit, spec section
 * 5.7) include this. */

#ifndef FREECODEC_ENC_PRIV_H
#define FREECODEC_ENC_PRIV_H

#include <stdint.h>

#include "freecodec/h264_bufs.h"
#include "freecodec/h264_headers.h"
#include "freecodec/h264_isp.h"
#include "freecodec/h264_rc.h"
#include "freecodec/h264_regs.h"
#include "freecodec/vencoder.h"
#include "freecodec/venc_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------- section 5.7 register access
 *
 * Every read or write of the encoder-block / ISP-block register window goes
 * through these two functions. Their own pointer parameter is `volatile`-
 * qualified; they live in their own translation unit (freecodec_enc_regs.c)
 * and are marked noinline there, so a caller can never have them folded away
 * or reordered relative to the surrounding writes (spec section 5.7). */
uint32_t fc_enc_reg_read(volatile uint32_t *base, unsigned int word_off);
void     fc_enc_reg_write(volatile uint32_t *base, unsigned int word_off, uint32_t val);

/* Write `n` consecutive words starting at word offset 0 (the per-picture
 * register script, spec 12 section 5.5 step 6, and the ISP block, spec 13
 * section 1 -- both blocks are "rebuilt from scratch every picture"). */
void fc_enc_reg_write_block(volatile uint32_t *base, const uint32_t *words,
                            unsigned int n);

/* -------------------------------------------------------------- geometry -- */

typedef struct fc_enc_geom {
    unsigned int in_w, in_h, in_stride;
    unsigned int dst_w, dst_h;
    unsigned int w16, h16;      /* coded size rounded up to 16 */
    unsigned int w_mb, h_mb;    /* W = w16/16, H = h16/16 (spec 12 section 3.1) */
} fc_enc_geom;

/* --------------------------------------------------------------- buffers -- */

typedef struct fc_enc_buf {
    void        *vir;
    void        *phy;           /* engine-visible physical address */
    unsigned int size;
} fc_enc_buf;

typedef struct fc_enc_buffers {
    vb_bitstream_manager *bs;
    fc_enc_buf   full[2];       /* luma+chroma, one allocation each (section 3.2) */
    unsigned int luma_size, chroma_size;
    fc_enc_buf   aux[2];        /* auxiliary (compressed) plane, IC > 0x1666 only */
    int          has_aux;
    fc_enc_buf   mb_info;
    fc_enc_buf   dblk;
    fc_enc_buf   mbrc;
    fc_enc_buf   mv;
} fc_enc_buffers;

/* -------------------------------------------------------------- parameters -- */

typedef struct fc_enc_params {
    unsigned int bitrate;            /* bit/s */
    unsigned int framerate;          /* fps, or >=1000 => milli-fps (section 8) */
    unsigned int max_key_interval;
    int          force_key;
    int          rotation;           /* multiple of 90 */
    int          hflip;
    unsigned int slice_height;       /* only 0 (single slice) is used */
    unsigned int stride;
    fwm_venc_pixel_format_e color_fmt;
    int          lbc_lossy_2x;       /* LBC input lossy-compression ratio flags, */
    int          lbc_lossy_2_5x;     /* from the base config, fed to the ISP */
    unsigned int vbv_size;
    unsigned int p_skip_factor;      /* accepted, unused */
    VencH264ProfileLevel profile_level;
    int          cabac_enable;
    int          fixed_qp_enable;
    int          fast_enc;           /* 0x10 fast-mode bits (spec 11) */
    int          vbv_no_cache;       /* bitstream ring allocated uncached */
    int          fixed_i_qp, fixed_p_qp;
    int          qp_min, qp_max;
    int          max_qp_step;
    int          chroma_gray;
    int          i_qp_offset;
    int          cbr_filling;
    int          roi_enable;
    VencROIConfig roi[8];
    FreecodecDisplaySize display_size;
} fc_enc_params;

/* --------------------------------------------------------- per-picture NAL -- */

typedef struct fc_enc_pic_state {
    unsigned int picture_count;   /* since init, never reset by IDR (section 5.1) */
    unsigned int frame_num;
    unsigned int poc_lsb;
    unsigned int idr_pic_id;
    int          first_picture;
} fc_enc_pic_state;

/* --------------------------------------------------------------- instance -- */

typedef struct fc_enc_instance {
    fc_ve_ops   *ve_ops;
    void        *ve_self;
    volatile uint32_t *enc_base;   /* engine window + 0xb00, word-addressed */
    volatile uint32_t *isp_base;   /* engine window + 0xa00, word-addressed */
    unsigned int ic_version;
    unsigned int dram_type;

    struct vb_mem_ops *memops;

    fc_enc_geom     geom;
    fc_enc_buffers  bufs;
    fc_enc_params   params;
    fc_enc_pic_state pic;

    freecodec_rc    rc;
    int             initialised;   /* init() has succeeded at least once */

    freecodec_h264_reg_info_out reg_info;   /* persistent per-instance values */
    freecodec_h264_shadow_v2    slice_seed; /* == reg_info.shadow, kept apart
                                              * for clarity at the slice step */

    unsigned char spspps[512];
    unsigned int  spspps_len;

    freecodec_h264_isp *isp;

    /* Burned-in overlay (h264-overlay.md): grow-only header/data buffers and
     * the packed ISP overlay fields, re-applied to every picture. */
    fc_enc_buf    ovl_hdr;
    fc_enc_buf    ovl_data;
    fc_enc_buf    ovl_inv;   /* overlay invert scratch buffer, reg 0x54 */
    freecodec_h264_isp_info ovl;   /* only the overlay fields are used */

    /* only for the host test's write-log / register-order assertions */
    void (*trace)(void *opaque, unsigned int block, unsigned int off, uint32_t val);
    void *trace_opaque;
} fc_enc_instance;

/* Shared helpers used by both the device and its tests. */
unsigned int fc_enc_align_up(unsigned int x, unsigned int n);

/* Test-only hook: install a register-write observer (spec section 14's
 * "register write order ... a write log"). block is 0 for the encoder
 * block, 1 for the ISP block. Not part of the device's public ABI. */
void fc_enc_set_trace(void *handle,
                      void (*trace)(void *opaque, unsigned int block,
                                   unsigned int off, uint32_t val),
                      void *opaque);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_ENC_PRIV_H */
