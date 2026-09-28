/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.265 command-block image builder (spec h265/11-register-programming.md).
 * Every field value and bit position comes from the fact tables in section 3;
 * where the spec only records observations the narrowest rule that reproduces
 * them is used and named here. */

#include <string.h>

#include "freecodec/h265_regs.h"

/* Place `value` (masked to `width` bits) at bit offset `shift`. */
static uint32_t field(uint32_t value, unsigned shift, unsigned width)
{
    uint32_t mask = (width >= 32u) ? 0xffffffffu : ((1u << width) - 1u);

    return (value & mask) << shift;
}

unsigned int freecodec_h265_length_stride(unsigned int width_px)
{
    return (width_px + 1023u) / 1024u;
}

void freecodec_h265_dyn_me_thresholds(unsigned int width_px, unsigned int th[4])
{
    if (th == NULL)
        return;
    /* Measured values at both geometries fit rounded 0.0030, 0.01875, 0.08125
     * and 0.1000 of the coded width (11 section 3.9). */
    th[0] = (width_px * 3u + 500u) / 1000u;
    th[1] = (width_px * 3u + 80u) / 160u;
    th[2] = (width_px * 13u + 80u) / 160u;
    th[3] = (width_px + 5u) / 10u;
}

void freecodec_h265_mad_thresholds(int is_i, uint32_t out[3])
{
    if (out == NULL)
        return;
    if (is_i) {
        /* I: 0, 0, 8, 12, 18, 26, 36, 48, then all-ones. */
        out[0] = 0x0c080000u;
        out[1] = 0x30241a12u;
    } else {
        /* P: 0, 4, 6, 10, 12, 16, 24, 36, then all-ones. */
        out[0] = 0x0a060400u;
        out[1] = 0x2418100cu;
    }
    out[2] = 0xffffffffu;
}

unsigned int freecodec_h265_intra_thresholds(int is_i)
{
    /* 3-D block threshold 32 in [31:23]; intra thresholds 5/4/1 (I) or 4/3/1
     * (P) in the low 23 bits (11 section 3.14). */
    if (is_i)
        return field(32u, 23, 9) | field(1u, 16, 7) | field(4u, 8, 8) | field(5u, 0, 8);
    return field(32u, 23, 9) | field(1u, 16, 7) | field(3u, 8, 8) | field(4u, 0, 8);
}

unsigned int freecodec_h265_tendency_word(unsigned int width_px, int is_i)
{
    /* Two observed constants; which layout the engine uses is an open item
     * (11 sections 3.13/7). The 2304-wide stream carries the intra value on
     * every picture, the 720p stream the P value on P pictures. */
    if (width_px > 1440u)
        return 0xffbbb75fu;
    return is_i ? 0xffbbb75fu : 0xdf7fb75fu;
}

void freecodec_h265_config_registers(const freecodec_h265_frame_cfg *cfg,
                                     uint32_t regs[0x200])
{
    unsigned int th[4];
    uint32_t mad[3];
    uint32_t rc_flags;

    if (cfg == NULL || regs == NULL)
        return;
    memset(regs, 0, 0x200u * sizeof(uint32_t));

    /* 0x04 picture control (11 section 3.1): transform_skip_en and
     * strong_intra_smooth_dis set; P adds slice_type; the deblock offsets and
     * frame_num come from the caller. eptb_disable is set only while the slice
     * header is fed. */
    regs[0x04u / 4u] = field(1u, 6, 1)
                     | field(1u, 7, 1)
                     | field(cfg->is_i ? 0u : 1u, 4, 1)
                     | field(cfg->deblock_tc, 16, 4)
                     | field(cfg->deblock_beta, 20, 4)
                     | field(cfg->frame_num, 24, 4)
                     | field(cfg->eptb_disable, 31, 1);

    /* 0x08 slice/QP control (11 section 3.2). */
    regs[0x08u / 4u] = field((uint32_t)cfg->qp, 0, 6)
                     | field(cfg->dynamic_me_en, 8, 1)
                     | field(cfg->length_stride, 10, 4)
                     | field(cfg->longterm_en, 14, 1)
                     | field(cfg->transform_8x8_en, 27, 1);

    /* 0x10 ME control (11 section 3.3): cache_bl8 always; TMVP write disabled
     * only on the first picture and read only while no reconstruction exists
     * (picture_index < 2); intra prediction disabled on P. */
    regs[0x10u / 4u] = field(1u, 27, 1)
                     | field((cfg->picture_index == 0u) ? 1u : 0u, 23, 1)
                     | field((cfg->picture_index < 2u) ? 1u : 0u, 22, 1)
                     | field(cfg->is_i ? 0u : 1u, 0, 1);

    /* 0x28 cyclic intra refresh: off on this path (11 section 3.4). */
    regs[0x28u / 4u] = 0u;

    /* 0x2c RC init word (11 section 3.5 / 10 section 1). */
    switch (cfg->rc_mode) {
    case FWM_VENC_H265_RC_VBR:
        rc_flags = 0x98000000u | (cfg->img_bin_enable ? (1u << 25) : 0u);
        break;
    case FWM_VENC_H265_RC_ABR:
    case FWM_VENC_H265_RC_AVBR:
        rc_flags = 0x58000000u | (cfg->img_bin_enable ? (1u << 25) : 0u);
        break;
    case FWM_VENC_H265_RC_FIXQP:
        rc_flags = 0x18000000u;
        break;
    case FWM_VENC_H265_RC_CBR:
    default:
        rc_flags = 0x98000000u;
        break;
    }
    regs[0x2cu / 4u] = rc_flags | (cfg->allocate_bits & 0x003fffffu);

    /* 0x30/0x34/0x38 MAD class thresholds: zero for the ABR/FixQP modes, which
     * clear classify (10 section 1). */
    if (cfg->rc_mode == FWM_VENC_H265_RC_ABR || cfg->rc_mode == FWM_VENC_H265_RC_AVBR ||
        cfg->rc_mode == FWM_VENC_H265_RC_FIXQP) {
        regs[0x30u / 4u] = regs[0x34u / 4u] = regs[0x38u / 4u] = 0u;
    } else {
        freecodec_h265_mad_thresholds(cfg->is_i, mad);
        regs[0x30u / 4u] = mad[0];
        regs[0x34u / 4u] = mad[1];
        regs[0x38u / 4u] = mad[2];
    }

    /* 0x3c CTU-info output address. */
    regs[0x3cu / 4u] = cfg->ctu_info_phy;

    /* 0x40 smart-function control: hpfilter_coef_shift 3, th 5 (11 section 3.7). */
    regs[0x40u / 4u] = field(5u, 26, 3) | field(3u, 29, 3);

    /* 0x44 temporal-filter thresholds: pic_var_chroma 30, pic_var_luma 24. */
    regs[0x44u / 4u] = field(30u, 16, 7) | field(24u, 23, 6);

    /* 0x48/0x4c dynamic-ME thresholds. */
    freecodec_h265_dyn_me_thresholds(cfg->width_px, th);
    regs[0x48u / 4u] = field(th[0], 0, 10) | field(th[1], 16, 10);
    regs[0x4cu / 4u] = field(th[2], 0, 10) | field(th[3], 16, 10);

    /* 0x60/0x64 TMVP write / read workspaces. */
    regs[0x60u / 4u] = cfg->tmvp_write_phy;
    regs[0x64u / 4u] = cfg->tmvp_read_phy;

    /* 0x68 lambda [17:0]; the high 14 bits are unassigned (11 section 7). */
    regs[0x68u / 4u] = cfg->lambda & 0x0003ffffu;

    /* 0x6c lambda sqrt, bright and dark thresholds. */
    regs[0x6cu / 4u] = field(cfg->lambda_sqrt, 0, 16)
                     | field(cfg->th_bright, 16, 8)
                     | field(cfg->th_dark, 24, 8);

    /* 0x70 lambda chroma and the eight ROI-disable bits. */
    regs[0x70u / 4u] = (cfg->lambda_c & 0x0003ffffu)
                     | field(cfg->roi_disable_mask, 24, 8);

    /* 0x74 tendency / intra coefficients. */
    regs[0x74u / 4u] = freecodec_h265_tendency_word(cfg->width_px, cfg->is_i);

    /* 0x80/0x84 bitstream bounds; 0x88/0x8c offsets in bits. */
    regs[0x80u / 4u] = cfg->bitstream_base_phy;
    regs[0x84u / 4u] = cfg->bitstream_end_phy;
    regs[0x88u / 4u] = cfg->bitstream_offset << 3;
    regs[0x8cu / 4u] = (cfg->bitstream_offset + cfg->bitstream_size) << 3;

    /* 0x94 intra / 3-D thresholds. */
    regs[0x94u / 4u] = freecodec_h265_intra_thresholds(cfg->is_i);

    /* 0x98/0xcc ROI QP offset records: zero when ROI is off. */
    regs[0x98u / 4u] = 0u;
    regs[0xccu / 4u] = 0u;

    /* 0x9c MB-RC buffer address. */
    regs[0x9cu / 4u] = cfg->mb_rc_phy;

    /* 0xa0/0xa4 reference, 0xb0/0xb4 reconstruction. */
    regs[0xa0u / 4u] = cfg->ref_y_phy;
    regs[0xa4u / 4u] = cfg->ref_c_phy;
    regs[0xb0u / 4u] = cfg->rec_y_phy;
    regs[0xb4u / 4u] = cfg->rec_c_phy;

    /* 0xb8/0xbc auxiliary reference / reconstruction (two separate registers). */
    regs[0xb8u / 4u] = cfg->aux_ref_phy;
    regs[0xbcu / 4u] = cfg->aux_rec_phy;

    /* 0xc0/0xc4 MB-info / deblock buffers; 0xc8 long-term auxiliary plane. */
    regs[0xc0u / 4u] = cfg->mb_info_phy;
    regs[0xc4u / 4u] = cfg->dblk_phy;
    regs[0xc8u / 4u] = cfg->lt_aux_phy;

    /* 0xd0..0xf4 ROI areas: zero when ROI is off. */
    regs[0xd0u / 4u] = 0u;
    regs[0xd4u / 4u] = 0u;
    regs[0xd8u / 4u] = 0u;
    regs[0xdcu / 4u] = 0u;
    regs[0xe8u / 4u] = 0u;
    regs[0xecu / 4u] = 0u;
    regs[0xf0u / 4u] = 0u;
    regs[0xf4u / 4u] = 0u;

    /* 0xf8 image-binary output; 0xfc CTU-mode control. */
    regs[0xf8u / 4u] = 0u;
    regs[0xfcu / 4u] = 0u;
}
