/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.265 command-block register programming (spec h265/11-register-programming.md):
 * the per-picture image of the encoder command block at engine window +0xb00.
 * Word index = byte offset / 4; the device writes the words it owns, in the
 * documented order. Fields no H.265 role assigns are left zero; the shared
 * hardware skeleton (bit-writer 0x18/0x1c/0x20, stream counter 0x90) is driven
 * by the device, not built here. */

#ifndef FREECODEC_H265_REGS_H
#define FREECODEC_H265_REGS_H

#include <stdint.h>

#include "freecodec/venc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One picture's inputs to the command-block builder. All physical addresses are
 * already shifted right by 8 (engine address form). */
typedef struct freecodec_h265_frame_cfg {
    unsigned int width_px;           /* coded width                          */
    unsigned int height_px;          /* coded height                         */
    unsigned int length_stride;      /* +0x08 [13:10]                        */
    int          is_i;               /* I (IDR) picture                      */
    unsigned int picture_index;      /* pictures since init, 0-based         */
    int          qp;                 /* slice QP, written to +0x08 [5:0]     */
    fwm_venc_h265_rc_mode_e rc_mode; /* +0x2c flag selection                 */
    int          dynamic_me_en;      /* +0x08 bit 8, set on P pictures       */
    int          img_bin_enable;     /* +0x2c bit 25 (VBR/ABR from f1)       */
    unsigned int allocate_bits;      /* +0x2c [21:0]                         */
    unsigned int deblock_tc;         /* +0x04 [19:16]                        */
    unsigned int deblock_beta;       /* +0x04 [23:20]                        */
    unsigned int lambda;             /* +0x68 [17:0] I/P constant            */
    unsigned int lambda_sqrt;        /* +0x6c [15:0]                         */
    unsigned int lambda_c;           /* +0x70 [17:0]                         */
    unsigned int th_bright;          /* +0x6c [23:16]                        */
    unsigned int th_dark;            /* +0x6c [31:24]                        */
    unsigned int roi_disable_mask;   /* +0x70 [31:24]; 0xff = all disabled   */
    unsigned int transform_8x8_en;   /* +0x08 bit 27                         */
    unsigned int longterm_en;        /* +0x08 bit 14                         */
    unsigned int eptb_disable;       /* +0x04 bit 31 (header feed)           */
    unsigned int frame_num;          /* +0x04 [27:24]                        */

    /* Address words (engine form). */
    unsigned int bitstream_base_phy; /* +0x80                                */
    unsigned int bitstream_end_phy;  /* +0x84                                */
    unsigned int bitstream_offset;   /* +0x88 / 8                            */
    unsigned int bitstream_size;     /* +0x8c free bytes                     */
    unsigned int ctu_info_phy;       /* +0x3c                                */
    unsigned int tmvp_read_phy;      /* +0x64                                */
    unsigned int tmvp_write_phy;     /* +0x60                                */
    unsigned int mb_rc_phy;          /* +0x9c                                */
    unsigned int ref_y_phy;          /* +0xa0                                */
    unsigned int ref_c_phy;          /* +0xa4                                */
    unsigned int rec_y_phy;          /* +0xb0                                */
    unsigned int rec_c_phy;          /* +0xb4                                */
    unsigned int aux_ref_phy;        /* +0xb8                                */
    unsigned int aux_rec_phy;        /* +0xbc                                */
    unsigned int mb_info_phy;        /* +0xc0                                */
    unsigned int dblk_phy;           /* +0xc4                                */
    unsigned int lt_aux_phy;         /* +0xc8                                */
    unsigned int img_bin_phy;        /* +0xf8                                */
    unsigned int ctu_mode_phy;       /* +0xfc                                */
} freecodec_h265_frame_cfg;

/* Fill the command-block image (0x200 words) for one picture. */
void freecodec_h265_config_registers(const freecodec_h265_frame_cfg *cfg,
                                     uint32_t regs[0x200]);

/* +0x08 [13:10]: observed 2 at 1280 wide and 3 at 2304 wide (11 section 3.2).
 * The spec records the values, not a formula; the narrowest rule that yields
 * both is one 1024-pixel unit. */
unsigned int freecodec_h265_length_stride(unsigned int width_px);

/* +0x48/0x4c dynamic-ME thresholds: proportional to the coded width
 * (11 section 3.9). */
void freecodec_h265_dyn_me_thresholds(unsigned int width_px,
                                      unsigned int th[4]);

/* +0x30/0x34/0x38 MAD class thresholds: the I and P tables of 11 section 3.6. */
void freecodec_h265_mad_thresholds(int is_i, uint32_t out[3]);

/* +0x94 intra / 3-D thresholds (11 section 3.14). */
unsigned int freecodec_h265_intra_thresholds(int is_i);

/* +0x74 tendency / intra coefficients: the two observed constants
 * (11 section 3.13). The layout-selection rule is an open item; the width and
 * picture type are the only inputs the capture distinguishes. */
unsigned int freecodec_h265_tendency_word(unsigned int width_px, int is_i);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H265_REGS_H */
