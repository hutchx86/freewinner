/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 encoder internal ISP programmer (spec/h264-isp.md §2) plus the overlay-info
 * packer, golden-verified where a golden exists. Shadow re-zeroed per call. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/h264_isp.h"

/* Register base, 13-word low-offset shadow, 64-entry scaler bank, bicubic
 * scratch (spec h264-isp.md). */
struct freecodec_h264_isp {
    unsigned long base;               /* register group base + 0xa00 */
    uint32_t      shadow[13];         /* the ISP register shadow, word index = offset / 4 */
    uint32_t      scaler_coeff[64];
    int           bicubic_ratio;
};

/* Input colour format (fwm_venc_pixel_format_e order) -> the ISP control word's
 * input-layout code [31:27] (spec 13 §1.1). Index 1 = YVU420SP/NV21 (VI output). */
static const unsigned char isp_pattern_in_mode[20] = {
    [0]  = 0x00,  /* YUV420SP / NV12 */
    [1]  = 0x04,  /* YVU420SP / NV21 */
    [2]  = 0x18,  /* YUV420P */
    [3]  = 0x1a,  /* YVU420P */
    [4]  = 0x02,  /* YUV422SP */
    [5]  = 0x06,  /* YVU422SP */
    [6]  = 0x1c,  /* YUV422P */
    [7]  = 0x1e,  /* YVU422P */
    [8]  = 0x01,  /* YUYV422 */
    [9]  = 0x03,  /* UYVY422 */
    [10] = 0x05,  /* YVYU422 */
    [11] = 0x07,  /* VYUY422 */
    [12] = 0x10,  /* ARGB */
    [13] = 0x12,  /* RGBA */
    [14] = 0x14,  /* ABGR */
    [15] = 0x16,  /* BGRA */
    [16] = 0x0a,  /* TILE_32X32 */
    [17] = 0x0c,  /* TILE_128X32 */
    [18] = 0x08,  /* AFBC_AW */
    [19] = 0x15,  /* LBC_AW */
};

static void isp_write(freecodec_h264_isp *isp, unsigned int off, uint32_t val)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)isp->base;

    reg[off >> 2] = val;
}

/* Bilinear tap pair for a sub-pixel phase (low byte of the crop offset); taps sum
 * to 0x100 and pack as (256 - phase) << 16 | phase. */
static uint32_t isp_scaler_pair(int offset)
{
    uint32_t frac = (uint32_t)offset & 0xffu;

    return (((0x100u - frac) & 0xffu) << 16) | frac;
}

freecodec_h264_isp *freecodec_h264_isp_create(void)
{
    return calloc(1, sizeof(freecodec_h264_isp));
}

void freecodec_h264_isp_destroy(freecodec_h264_isp *isp)
{
    free(isp);
}

void freecodec_h264_isp_set_base(freecodec_h264_isp *isp, unsigned long base)
{
    if (isp)
        isp->base = base;
}

void freecodec_h264_isp_set_register(freecodec_h264_isp *isp,
                                     const freecodec_h264_isp_info *info)
{
    unsigned int i;
    int          scaler;
    uint32_t     ctrl;

    if (!isp || !info)
        return;

    memset(isp->shadow, 0, sizeof(isp->shadow));

    /* Scaler is engaged whenever the output size differs from the input. */
    scaler = (info->output_width_mb != info->input_width_mb) ||
             (info->output_height_mb != info->input_height_mb);

    /* 0x00 input picture size: input size in macroblocks. */
    isp->shadow[0x00 >> 2] = ((uint32_t)info->input_width_mb << 16) |
                             ((uint32_t)info->input_height_mb & 0xffffu);

    {
        uint32_t fmt = (uint32_t)info->color_format;
        uint32_t pin = (fmt < 20u) ? (uint32_t)isp_pattern_in_mode[fmt] : 0u;
        uint32_t w = (((uint32_t)info->input_width_mb * 8u) + 0x1fu) & ~0x1fu;
        uint32_t y_stride, yc_stride;
        int      lbc_lossy;

        /* LBC input shrinks the CSI stride by the lossy fraction, derived from the
         * MB-aligned width then rounded to 64 (2X: 4.8x/7.2x; 2.5X: 3.52x/6.08x). */
        lbc_lossy = (pin == 0x15u) &&
                    (info->b_lbc_lossy_com_en_2x ||
                     info->b_lbc_lossy_com_en_2_5x);
        if (lbc_lossy) {
            uint32_t luma, chroma;

            if (info->b_lbc_lossy_com_en_2x) {
                luma = (w * 4800u) / 1000u;
                chroma = (450u * w * 8u) / 500u;
            } else {
                luma = (w * 3520u) / 1000u;
                chroma = (380u * w * 8u) / 500u;
            }
            y_stride = ((luma + 511u) >> 3) & ~0x3fu;
            yc_stride = ((chroma + 511u) >> 3) & ~0x3fu;
        } else {
            y_stride = (uint32_t)info->input_stride_mb;
            yc_stride = (uint32_t)info->input_stride_mb;
        }

        /* 0x04 luma line stride: LBC uses nLbc_y_stride>>4, else the input
         * stride unshifted (the >>4 is part of the derivation, not a field shift). */
        isp->shadow[0x04 >> 2] = (isp->shadow[0x04 >> 2] & 0xfc00u) |
                                 (((lbc_lossy ? (y_stride >> 4) : y_stride) & 0x3ffu) << 16);

        /* 0x08 ISP control word. */
        ctrl  = ((uint32_t)info->color_space_rgb2yuv & 0x3u) << 1;
        ctrl |= ((uint32_t)info->color_space_yuv2yuv & 0x3u) << 8;
        ctrl |= ((uint32_t)info->e_overlay_argb_type & 0x3u) << 11;
        ctrl |= ((uint32_t)(scaler ? 1u : 0u)) << 16;
        ctrl |= ((uint32_t)(info->b_overlay ? 1u : 0u)) << 18;
        ctrl |= ((uint32_t)(info->thumb_enable ? 1u : 0u)) << 19;
        ctrl |= ((uint32_t)info->rotate_angle & 0x3u) << 20;
        ctrl |= ((uint32_t)(info->horizonflip_enable ? 1u : 0u)) << 22;
        ctrl |= ((uint32_t)info->thumb_scale_factor & 0x3u) << 25;
        ctrl |= (pin & 0x1fu) << 27;
        /* lbc_lossy_com_en: pattern 0x15 and either lossy flag set. */
        if (lbc_lossy)
            ctrl |= 1u << 4;
        isp->shadow[0x08 >> 2] = ctrl;

        /* 0x14 chroma line stride: LBC uses nLbc_yc_stride>>3, else the input
         * stride unshifted (same reasoning as 0x04 above). */
        isp->shadow[0x14 >> 2] = (isp->shadow[0x14 >> 2] & 0xf000u) |
                                 ((lbc_lossy ? (yc_stride >> 3) : yc_stride) & 0xfffu);

        if (getenv("FREECODEC_DBG") != NULL)
            fprintf(stderr,
                    "freecodec_isp: fmt=%u pin=0x%02x lbc_lossy=%d "
                    "in_stride_mb=%d y_stride=%u yc_stride=%u reg04=0x%08x reg14=0x%08x\n",
                    fmt, pin, lbc_lossy, info->input_stride_mb, y_stride, yc_stride,
                    isp->shadow[0x04 >> 2], isp->shadow[0x14 >> 2]);
    }
    if (info->b_overlay) {
        isp->shadow[0x14 >> 2] |=
            ((uint32_t)(info->n_overlay_num - 1u) & 0x3fu) << 16;
        isp->shadow[0x14 >> 2] |= ((uint32_t)info->invert_mode & 0x3u) << 22;
        isp->shadow[0x14 >> 2] |= ((uint32_t)info->invert_threshold & 0xffu) << 24;
    }

    /* 0x2c output size and RoI count (RoI run is empty here). */
    isp->shadow[0x2c >> 2] = ((uint32_t)info->output_width_mb & 0x7ffu) |
                             (((uint32_t)info->output_height_mb & 0x7ffu) << 16);

    isp_write(isp, 0x00, isp->shadow[0x00 >> 2]);
    isp_write(isp, 0x04, isp->shadow[0x04 >> 2]);
    isp_write(isp, 0x08, isp->shadow[0x08 >> 2]);
    isp_write(isp, 0x14, isp->shadow[0x14 >> 2]);
    isp_write(isp, 0x2c, isp->shadow[0x2c >> 2]);

    /* 0x78/0x7c/0x80 input plane physical addresses. */
    isp_write(isp, 0x78, info->phy_address_y);
    isp_write(isp, 0x7c, info->phy_address_c0);
    isp_write(isp, 0x80, info->phy_address_c1);

    /* 0x88 CSI address high bytes (ver2 path). */
    if (info->enc_version2_flag) {
        uint32_t hi = ((uint32_t)info->phy_address_y_high8 << 16) |
                      ((uint32_t)info->phy_address_c0_high8 << 8) |
                       (uint32_t)info->phy_address_c1_high8;

        isp_write(isp, 0x88, hi);
    }

    /* 0x44/0x48/0x54 overlay plane addresses. */
    if (info->b_overlay) {
        isp_write(isp, 0x44, info->phy_address_overlay_header);
        isp_write(isp, 0x48, info->phy_address_overlay_data);
        if (info->ic_version > 0x2110f)
            isp_write(isp, 0x54, info->phy_address_overlay_invert);
    }

    /* 0x70/0x74/0x84 thumbnail write-back planes. */
    if (info->thumb_enable) {
        isp_write(isp, 0x70, info->thumb_wb_phy_address_y);
        isp_write(isp, 0x74, info->thumb_wb_phy_address_c);
        /* Thumbnail write-back high bytes: thumb_wb_y_high8 at [15:8], thumb_wb_c_high8 at [7:0]
         * (cf. the 0x88 CSI word packing three bytes at [23:16]/[15:8]/[7:0]). */
        isp_write(isp, 0x84, ((uint32_t)info->thumb_wb_phy_y_high8 << 8) |
                             (uint32_t)info->thumb_wb_phy_c_high8);
    }

    if (scaler) {
        unsigned int shift = (info->ic_version > 0x2100f) ? 10u : 8u;
        uint32_t     hor, ver;
        uint32_t     ow = (uint32_t)info->output_width_mb;
        uint32_t     oh = (uint32_t)info->output_height_mb;

        /* 0x30/0x34 scaler luma/chroma phase. */
        isp->shadow[0x30 >> 2] =
            ((uint32_t)((uint32_t)info->x_offset << shift) & 0xffffu) |
            ((uint32_t)info->y_offset << (shift + 16));
        isp_write(isp, 0x30, isp->shadow[0x30 >> 2]);
        isp_write(isp, 0x34,
                  ((uint32_t)((uint32_t)info->x_offset << (shift - 1)) & 0xffffu) |
                  ((uint32_t)info->y_offset << (shift + 15)));

        /* 0x38 scaler ratio: fixed-point ratio. ver1 packs hor[14:0]/ver[30:16];
         * ver0 packs hor[10:0]/ver[22:12] (spec h264-isp.md, scaler ratio 0x38). */
        if (info->ic_version > 0x2100f) {
            hor = ow ? (((uint32_t)info->input_width_mb << 12) / ow) : 0u;
            ver = oh ? (((uint32_t)info->input_height_mb << 12) / oh) : 0u;
            hor &= 0x7fffu;
            ver &= 0x7fffu;
            if (info->ic_version == 0x21110 &&
                (info->n_encode_format == 1 || info->n_encode_format == 2) &&
                ver >= 0xf01u && ver <= 0xfffu)
                ver = 0xf00u;
            isp_write(isp, 0x38, hor | (ver << 16));
        } else {
            hor = ow ? (((uint32_t)info->input_width_mb << 11) / ow) : 0u;
            ver = oh ? (((uint32_t)info->input_height_mb << 11) / oh) : 0u;
            hor &= 0x7ffu;
            ver &= 0x7ffu;
            isp_write(isp, 0x38, hor | (ver << 12));
        }

        /* All 64 coefficients latch through the single 0xe4 port (no 4-byte
         * stride); 0xe0 is the port's zeroed base word. */
        for (i = 0; i < 64u; i++)
            isp->scaler_coeff[i] = isp_scaler_pair(info->y_offset);
        isp_write(isp, 0xe0, 0);
        for (i = 0; i < 64u; i++)
            isp_write(isp, 0xe4, isp->scaler_coeff[i]);
    }
}

/* ------------------------------------------------------------- overlay -- */

unsigned int freecodec_h264_overlay_data_size(const freecodec_h264_overlay_block *blocks,
                                              unsigned int blk_num)
{
    unsigned int total = 0u;
    unsigned int i;

    if (blk_num > FREECODEC_H264_OVERLAY_MAX_BLOCKS)
        return 0u;
    if (blocks == NULL || blk_num == 0u)
        return FREECODEC_H264_OVERLAY_DATA_SLACK;

    for (i = 0; i < blk_num; i++) {
        unsigned int sz = blocks[i].bitmap_size;

        total += (sz + (FREECODEC_H264_OVERLAY_BLOCK_ALIGN - 1u)) &
                 ~(FREECODEC_H264_OVERLAY_BLOCK_ALIGN - 1u);
    }
    return total + FREECODEC_H264_OVERLAY_DATA_SLACK;
}

static void overlay_put_u32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

int freecodec_h264_isp_update_overlay(freecodec_h264_isp_info *info,
                                      const freecodec_h264_overlay_block *blocks,
                                      unsigned int blk_num,
                                      int argb_type,
                                      unsigned char *header_vir,
                                      unsigned char *data_vir,
                                      unsigned int data_buf_size)
{
    unsigned int offset;
    unsigned int i;
    unsigned int ctrl;

    if (info == NULL)
        return -1;

    /* blk_num == 0 (or no block list) disables the overlay rather than erroring;
     * this is how a caller removes an overlay (spec/h264-overlay.md §3). */
    if (blk_num == 0u || blocks == NULL) {
        info->b_overlay           = 0;
        info->n_overlay_num       = 0;
        info->e_overlay_argb_type = argb_type;
        return 0;
    }

    if (blk_num > FREECODEC_H264_OVERLAY_MAX_BLOCKS)
        return -1;
    if (header_vir == NULL || data_vir == NULL)
        return -1;

    /* No partial writes: verify every block fits before copying any bytes
     * (spec §3 step 1). */
    offset = 0u;
    for (i = 0; i < blk_num; i++) {
        unsigned int sz = blocks[i].bitmap_size;
        unsigned int padded = (sz + (FREECODEC_H264_OVERLAY_BLOCK_ALIGN - 1u)) &
                              ~(FREECODEC_H264_OVERLAY_BLOCK_ALIGN - 1u);

        if (sz != 0u && blocks[i].bitmap_vir == NULL)
            return -1;
        if (padded > data_buf_size - offset)   /* offset <= data_buf_size always holds here */
            return -1;
        offset += padded;
    }

    offset = 0u;
    for (i = 0; i < blk_num; i++) {
        const freecodec_h264_overlay_block *b = &blocks[i];
        unsigned int sz = b->bitmap_size;
        unsigned int padded = (sz + (FREECODEC_H264_OVERLAY_BLOCK_ALIGN - 1u)) &
                              ~(FREECODEC_H264_OVERLAY_BLOCK_ALIGN - 1u);
        unsigned char *hdr = header_vir + (uintptr_t)i * FREECODEC_H264_OVERLAY_HDR_STRIDE;

        if (sz != 0u)
            memcpy(data_vir + offset, b->bitmap_vir, sz);

        /* 16-byte header entry (ic_version > 0x2110f), laid out as in
         * docs/integration-h264.md "OSD header layout". */
        hdr[0] = (unsigned char)(b->start_mb_x & 0xffu);
        hdr[1] = (unsigned char)(b->end_mb_x & 0xffu);
        hdr[2] = (unsigned char)(b->start_mb_y & 0xffu);
        hdr[3] = (unsigned char)(b->end_mb_y & 0xffu);
        hdr[4] = hdr[5] = hdr[6] = hdr[7] = 0;
        hdr[12] = hdr[13] = hdr[14] = 0;
        /* +15: luma-invert unit, low nibble width-1, high nibble height-1,
         * in macroblocks (0..3 each; overlay-invert spec section 5). */
        hdr[15] = (unsigned char)(((b->reverse_unit_h_minus1 & 3u) << 4) |
                                  (b->reverse_unit_w_minus1 & 3u));
        ctrl = (offset >> 8) & 0x3fffffu;
        if (i + 1u == blk_num)
            ctrl |= 1u << 31;                      /* last_blk_flag */
        overlay_put_u32(hdr + 8, ctrl);

        offset += padded;
    }

    info->b_overlay           = 1;
    info->n_overlay_num       = (unsigned char)blk_num;
    info->e_overlay_argb_type = argb_type;
    info->n_blk_len           = (int)offset;       /* total data bytes used */

    return 0;
}
