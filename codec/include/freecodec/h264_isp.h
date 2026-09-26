/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 encoder internal ISP (spec/h264-isp.md), golden-verified: a register
 * file at register-group base + 0xa00 the encoder programs every frame. */

#ifndef FREECODEC_H264_ISP_H
#define FREECODEC_H264_ISP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct freecodec_h264_isp freecodec_h264_isp;

/* Input buffer/format description; packed to 476 B, never crosses an ABI
 * boundary (spec h264-isp.md). */
typedef struct freecodec_h264_isp_info {
    int          rotate_angle;              /* 0 */
    int          input_width_mb;            /* 4 */
    int          input_height_mb;           /* 8 */
    int          output_width_mb;           /* 12 */
    int          output_height_mb;          /* 16 */
    int          input_stride_mb;           /* 20 */
    int          thumb_enable;              /* 24 */
    int          b_save_back_write;         /* 28 */
    int          thumb_scale_factor;        /* 32 */
    int          color_format;              /* 36 */
    unsigned int phy_address_y;             /* 40 */
    unsigned int phy_address_c0;            /* 44 */
    unsigned int phy_address_c1;            /* 48 */
    unsigned int thumb_wb_phy_address_y;    /* 52 */
    unsigned int thumb_wb_phy_address_c;    /* 56 */
    int          x_offset;                  /* 60 */
    int          y_offset;                  /* 64 */
    int          color_space_rgb2yuv;       /* 68 */
    int          color_space_yuv2yuv;       /* 72 */
    int          pic_type;                  /* 76 */
    int          ic_version;                /* 80 */
    unsigned char thumb_wb_phy_y_high8;     /* 84 */
    unsigned char thumb_wb_phy_c_high8;     /* 85 */
    unsigned char phy_address_y_high8;      /* 86 */
    unsigned char phy_address_c0_high8;     /* 87 */
    unsigned char phy_address_c1_high8;     /* 88 */
    unsigned char pad0[3];
    int          enc_version2_flag;         /* 92 */
    unsigned char b_overlay;                /* 96 */
    unsigned char pad1[3];
    int          horizonflip_enable;        /* 100 */
    int          e_overlay_argb_type;       /* 104 */
    int          n_blk_len;                 /* 108 */
    unsigned int phy_address_overlay_header;/* 112 */
    unsigned int phy_address_overlay_data;  /* 116 */
    unsigned char n_overlay_num;            /* 120 */
    unsigned char init_coef_flag;           /* 121 */
    unsigned char pad2[2];
    int          coef[9];                   /* 124..159 */
    unsigned char b_height_is8_align;       /* 160 */
    unsigned char b_only_wb_flag;           /* 161 */
    unsigned char pad3[2];
    unsigned int phy_address_overlay_invert;/* 164 */
    int          invert_mode;               /* 168 */
    int          invert_threshold;          /* 172 */
    unsigned char b_lbc_lossy_com_en_2x;    /* 176 */
    unsigned char b_lbc_lossy_com_en_2_5x;  /* 177 */
    unsigned char pad4[2];
    int          n_lbc_y_stride;            /* 180 */
    int          n_lbc_yc_stride;           /* 184 */
    int          n_encode_format;           /* 188 */
    unsigned char roi_config_copy[280];     /* 192 */
    int          memory_type;               /* 472 */
} freecodec_h264_isp_info;

freecodec_h264_isp *freecodec_h264_isp_create(void);
void freecodec_h264_isp_destroy(freecodec_h264_isp *isp);

/* Set the hardware register base (register group base + 0xa00). */
void freecodec_h264_isp_set_base(freecodec_h264_isp *isp, unsigned long base);

/* Program the ISP registers for one frame. */
void freecodec_h264_isp_set_register(freecodec_h264_isp *isp,
                                     const freecodec_h264_isp_info *info);

/* ------------------------------------------------------------- overlay -- */

/* Overlay (OSD) support (spec/h264-overlay.md): the SetParameter-side packing step
 * filling the isp_info overlay fields. Block descriptor is our own (no SDK dependency). */
#define FREECODEC_H264_OVERLAY_MAX_BLOCKS   64u
/* Fixed header-buffer size, sized for the 64-block ceiling (spec sec. 2). */
#define FREECODEC_H264_OVERLAY_HEADER_SIZE  1024u
/* Per-block header-table stride: 1024 / 64, confirmed by a live capture of
 * the reference's overlay buffers (spec sec. 5). */
#define FREECODEC_H264_OVERLAY_HDR_STRIDE   16u
/* Data-buffer per-block padding and fixed growth slack (spec sec. 2). */
#define FREECODEC_H264_OVERLAY_BLOCK_ALIGN  256u
#define FREECODEC_H264_OVERLAY_DATA_SLACK   4096u

typedef struct freecodec_h264_overlay_block {
    unsigned short       start_mb_x;
    unsigned short       end_mb_x;
    unsigned short       start_mb_y;
    unsigned short       end_mb_y;
    const unsigned char *bitmap_vir;  /* caller's block bitmap bytes */
    unsigned int         bitmap_size; /* bitmap byte size */
    unsigned char        reverse_unit_w_minus1; /* luma-invert unit, MB - 1 (0..3) */
    unsigned char        reverse_unit_h_minus1;
    unsigned char        reverse_luma;          /* LUMA_REVERSE block: header +8 bit 29 */
} freecodec_h264_overlay_block;

/* Required data-buffer size: sum of block bitmap_size padded to BLOCK_ALIGN plus
 * DATA_SLACK. NULL/0 blocks returns the slack; over the ceiling returns 0. */
unsigned int freecodec_h264_overlay_data_size(const freecodec_h264_overlay_block *blocks,
                                              unsigned int blk_num);

/* Pack `blocks` into the buffers and fill `info`'s overlay fields; blk_num==0 disables.
 * No partial writes. Returns 0, or -1 on over-ceiling/NULL/does-not-fit. Caller flushes. */
int freecodec_h264_isp_update_overlay(freecodec_h264_isp_info *info,
                                      const freecodec_h264_overlay_block *blocks,
                                      unsigned int blk_num,
                                      int argb_type,
                                      unsigned char *header_vir,
                                      unsigned char *data_vir,
                                      unsigned int data_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H264_ISP_H */
