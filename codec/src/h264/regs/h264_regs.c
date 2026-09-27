/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 ver2 register-shadow builder(s) from spec/h264-reg-shadow.md,
 * golden-matched. Where spec and golden disagree, the golden is authoritative. */

#include <string.h>

#include "freecodec/h264_regs.h"

/* Place `value` (masked to `width` bits) at bit offset `shift`. */
static uint32_t field(uint32_t value, unsigned shift, unsigned width)
{
    uint32_t mask = (width >= 32) ? 0xffffffffu : ((1u << width) - 1u);

    return (value & mask) << shift;
}

void freecodec_h264_init_reg_info(const freecodec_h264_reg_info_cfg *cfg,
                                  freecodec_h264_reg_info_out *out)
{
    freecodec_h264_shadow_v2 *s = &out->shadow;
    unsigned int transform8x8 = cfg->transform8x8_mode_flag;
    unsigned int use_intra_4x4 = cfg->use_intra_4x4;
    unsigned int classify_engine_enabled;

    memset(out, 0, sizeof(*out));

    /* A non-High-profile stream cannot carry transform8x8; the flag is
     * cleared before the shadow is built. */
    if (cfg->profile_idc != 100)
        transform8x8 = 0;

    /* Fast encoding forces 4x4-intra off and turns on the refinement
     * disables below. */
    if (cfg->fast_enc)
        use_intra_4x4 = 0;

    classify_engine_enabled = (cfg->fix_qp_enable == 0);

    /* 0x04 picture control (spec 11 2.1). */
    s->picture_control = field(cfg->entropy_coding_mode_flag, 8, 1)  /* b1[0] */
              | field(cfg->cabac_context_index, 10, 2)             /* b1[3:2] */
              | field(cfg->deblock_idc, 12, 2)              /* b1[5:4] */
              | field((uint32_t)cfg->alpha_offset_div2, 16, 4) /* b2[3:0] */
              | field((uint32_t)cfg->beta_offset_div2, 20, 4)  /* b2[7:4] */
              | field(1u, 31, 1);                    /* emulation-prevention control */

    /* 0x08 slice/stride control (spec 11 2.2). */
    s->slice_stride_control = field((uint32_t)cfg->slice_qp, 0, 6)
              | field((cfg->dst_width_mb + 47u) / 48u, 10, 4) /* stride-group count */
              | field(cfg->chroma_qp_index_offset, 16, 5)
              | field(cfg->alter_frame_en, 21, 1)
              | field(transform8x8 == 0, 27, 1);    /* 8x8 transform disable */

    /* 0x10 motion-estimation control (spec 11 2.3). */
    s->me_control = field(cfg->use_intra_in_p == 0, 0, 1) /* intra macroblocks disabled in P */
               | field(1u, 9, 1)                       /* skip-mode optimisation disable */
               | field(1u, 12, 1)                      /* quantisation-threshold disable */
               | field(use_intra_4x4 == 0, 14, 1)      /* intra 4x4 disable */
               | field(cfg->mv_addr_vir_null, 16, 1);  /* MV-info write-back disable */
    if (cfg->fast_enc) {
        /* Set by the fast-encode path (me_control bits 4 and 20). */
        s->me_control |= field(1u, 4, 1)   /* quarter-pel smart search off */
                    | field(1u, 20, 1); /* sub-pel refinement disable */
    }

    /* 0x2c MB-level rate-control control (spec 11 2.4). */
    s->mb_rc_control =
          field(cfg->binned_image_output_enable, 25, 1)      /* b3[1] */
        | field(cfg->use_hp_filter, 26, 1)          /* b3[2] */
        | field(cfg->qp_mad_sse_output_en, 27, 1)   /* b3[3] */
        | field(classify_engine_enabled, 31, 1);                /* b3[7] */

    /* 0x40 adaptive-function control (spec 11 2.5). The spec places th_hvs_coef_shift in b2[7:4]; the golden
     * shows it in b1[7:4] with th_hvs_dir occupying the whole of b2. */
    s->adaptive_control = field(cfg->th_smart_img_bin, 0, 8)
                  | field(cfg->smart_en, 8, 1)
                  | field(cfg->smart_shift_bits, 9, 2)
                  | field(cfg->hvs_en, 11, 1)
                  | field(cfg->th_hvs_coef_shift, 12, 4)
                  | field(cfg->th_hvs_dir, 16, 8)
                  | field(cfg->skip_tend_pic_en, 24, 1)
                  | field(cfg->mode_ctrl_en, 25, 1)
                  | field(cfg->hpfilter_coef_th, 26, 3)
                  | field(cfg->hpfilter_coef_shift, 29, 3);

    /* temporal parameter word. */
    s->temporal_param = field(0x20u, 8, 8);            /* maxcoef b1 */

    /* 0x44 high-pass / picture-variance thresholds (spec 11 2.5) — only for the
     * v5-capable media engine. The spec's
     * pic_var_luma constant (0x18) is wrong; the golden stores 0x0c. */
    if (cfg->ic_version > 0x2100fu) {
        s->hp_variance_control = field(cfg->hpfilter_contrast_th, 0, 8)
                           | field(cfg->hpfilter_mad_th, 8, 8)
                           | field(0x1eu, 16, 7)      /* pic_var_chroma */
                           | field(0x0cu, 24, 6);     /* pic_var_luma */
    }

    /* bright/dark classification thresholds. */
    s->bright_threshold = field(cfg->th_bright, 16, 8)         /* th_bright b2 */
               | field(cfg->th_dark, 24, 8);          /* th_dark b3 */

    /* Context flags outside the register shadow: filter_3d_level holds the raw
     * 3D-filter level, dynamic_me_enable the derived boolean. */
    out->first_frame = 1;
    /* The MB rate control is disabled on frames taller than 0x1000
     * (16-aligned), in addition to the fixed-QP condition. */
    out->mb_rc_enable = (classify_engine_enabled
                         && cfg->display_height_16align <= 0x1000u) ? 1u : 0u;
    out->ctx_flag_a = 1;
    out->filter_3d_level = cfg->filter_3d_level;
    out->dynamic_me_enable = (cfg->filter_3d_level == 0) ? 1u : 0u;
    out->over_time_mb_count = 0;
    out->ctx_flag_b = 0;
    out->transform8x8_mode_flag = transform8x8;
}

/* Slice-header / register-writer path (spec/h264-slice-regs.md; hwslice and
 * setreg goldens). Byte offsets are used because the spec describes them that way. */

static void put_byte(uint32_t *w, unsigned off, uint32_t val)
{
    unsigned shift = (off % 4u) * 8u;

    w[off / 4u] = (w[off / 4u] & ~(0xffu << shift))
                | ((val & 0xffu) << shift);
}

/* Word pointer for a ver2 shadow byte offset (idx = offset / 4). */
static uint32_t *v2_word(freecodec_h264_shadow_v2 *s, unsigned idx)
{
    switch (idx) {
    case 0:  return &s->picture_control;
    case 1:  return &s->slice_stride_control;
    case 2:  return &s->hw_slice_header;
    case 3:  return &s->me_control;
    case 4:  return &s->start_control;
    case 5:  return &s->mb_rc_control;
    case 6:  return &s->rate_ctrl_param_0;
    case 7:  return &s->rate_ctrl_param_1;
    case 8:  return &s->rate_ctrl_param_2;
    case 9:  return &s->adaptive_control;
    case 10: return &s->temporal_param;
    case 11: return &s->hp_variance_control;
    case 12: return &s->dynamic_me_param_0;
    case 13: return &s->dynamic_me_param_1;
    case 14: return &s->bright_threshold;
    case 15: return &s->r3DFilterThreshold;
    case 16: return &s->rIntraRefresh;
    case 17: return &s->roi_qp_offset;
    default: return &s->roi_qp_offset_v5;
    }
}

static uint32_t v2_get_byte(const freecodec_h264_shadow_v2 *s, unsigned off)
{
    freecodec_h264_shadow_v2 tmp = *s;
    uint32_t                 *w  = v2_word(&tmp, off / 4u);

    return (*w >> ((off % 4u) * 8u)) & 0xffu;
}

static void v2_put_byte(freecodec_h264_shadow_v2 *s, unsigned off, uint32_t val)
{
    uint32_t *w     = v2_word(s, off / 4u);
    unsigned  shift = (off % 4u) * 8u;

    *w = (*w & ~(0xffu << shift)) | ((val & 0xffu) << shift);
}

void freecodec_h264_hw_slice_header(const freecodec_h264_slice_cfg *cfg,
                                    int arg, int shadow_version,
                                    freecodec_h264_slice_state *st)
{
    unsigned int nal;

    if (cfg->coding_type == 18u)
        nal = 3u;
    else if (cfg->coding_type == 0u)
        nal = 2u;
    else
        nal = 0u;

    st->nal_reference_idc = nal;
    st->cfg = *cfg;

    if (cfg->pic_order_cnt_type == 0u) {
        uint32_t mask = (1u << cfg->log2_max_pic_order_cnt_lsb) - 1u;
        int      poc  = (arg != 2) ? cfg->top_poc : cfg->bottom_poc;

        st->pic_order_cnt_lsb = (uint32_t)poc & mask;
    }

    if (shadow_version == 0) {
        memset(st->v1.w, 0, sizeof(st->v1.w));

        /* +7: b0-3 = frame_num low bits; b5 (the hardware-slice mode bit) selects the hardware slice
         * header and is set only when a slice height is programmed. */
        put_byte(st->v1.w, 7u, (cfg->frame_num & 0xfu)
                               | (cfg->n_slice_height != 0u ? 0x20u : 0u));
        put_byte(st->v1.w, 17u, cfg->n_slice_height >> 4);
        put_byte(st->v1.w, 18u, st->pic_order_cnt_lsb);
        put_byte(st->v1.w, 19u,
                 ((cfg->log2_max_pic_order_cnt_lsb - 4u) & 7u)
                 | ((nal & 3u) << 3));
    } else if (shadow_version == 1) {
        /* Seed from the persisted shadow when supplied, else zero: a zeroed shadow
         * drops the 8x8 transform disable (slice_stride_control bit 27) and breaks the MB coder. */
        if (cfg->seed_shadow != NULL && cfg->n_slice_height == 0u)
            st->v2 = *cfg->seed_shadow;
        else
            memset(&st->v2, 0, sizeof(st->v2));

        /* +3: b0-3 = frame_num low bits; b5 (the hardware-slice mode bit) follows the slice path. */
        v2_put_byte(&st->v2, 3u, (cfg->frame_num & 0xfu)
                                  | (cfg->n_slice_height != 0u ? 0x20u : 0u));
        v2_put_byte(&st->v2, 9u, cfg->n_slice_height >> 4);
        v2_put_byte(&st->v2, 10u, st->pic_order_cnt_lsb);
        v2_put_byte(&st->v2, 11u,
                    ((cfg->log2_max_pic_order_cnt_lsb - 4u) & 7u)
                    | ((nal & 3u) << 3));
    }
}

void freecodec_h264_set_reg_ver1(freecodec_h264_slice_state *st,
                                 unsigned int ic_version,
                                 uint32_t over_time_mb_count,
                                 uint32_t mv_addr_phy,
                                 uint32_t regs[6])
{
    const freecodec_h264_slice_cfg *c = &st->cfg;
    unsigned int slice_non_i = (c->slice_type != 2u);
    uint32_t     b4, b5;

    /* +4: b0-1 = picture type, b4 = (slice type != 2). The golden also sets b2
     * for a field picture (picture type != 0); the spec omits that flag. */
    b4 = field(c->pic_type, 0, 2)
       | field((c->pic_type != 0u) ? 1u : 0u, 2, 1)
       | field(slice_non_i ? 1u : 0u, 4, 1);
    b5 = field(c->entropy_coding_mode_flag, 0, 1)
       | field(slice_non_i ? (c->model_number & 3u) : 0u, 2, 2);

    put_byte(st->v1.w, 4u, b4);
    put_byte(st->v1.w, 5u, b5);

    /* slice_qp: +0xc on newer engines, else +8 and +9. */
    if (ic_version > 5734u) {
        put_byte(st->v1.w, 0x0cu, (uint32_t)c->slice_qp & 63u);
    } else {
        put_byte(st->v1.w, 0x08u, (uint32_t)c->slice_qp & 63u);
        put_byte(st->v1.w, 0x09u, (uint32_t)c->slice_qp & 63u);
    }

    /* +0x16 b0: cleared for coding type 1 or a non-zero motion parameter. */
    put_byte(st->v1.w, 0x16u,
             (c->coding_type == 1u || c->motion_parameter != 0u) ? 0u : 1u);

    regs[0] = st->v1.w[1];
    regs[1] = (ic_version > 5734u) ? st->v1.w[3] : st->v1.w[2];
    regs[2] = st->v1.w[4];
    regs[3] = st->v1.w[5];
    regs[4] = over_time_mb_count;
    regs[5] = mv_addr_phy;
}

void freecodec_h264_set_reg_ver2(freecodec_h264_slice_state *st,
                                 unsigned int dram_type,
                                 uint32_t over_time_mb_count,
                                 uint32_t mv_addr_phy,
                                 uint32_t regs[6])
{
    const freecodec_h264_slice_cfg *c = &st->cfg;
    unsigned int slice_non_i = (c->slice_type != 2u);
    unsigned int lt;
    uint32_t     b0, b1;

    b0 = field(slice_non_i ? 1u : 0u, 4, 1);
    b1 = field(c->entropy_coding_mode_flag, 0, 1)
       | field(slice_non_i ? (c->model_number & 3u) : 0u, 2, 2);

    v2_put_byte(&st->v2, 0u, b0);
    v2_put_byte(&st->v2, 1u, b1);

    /* +4 b0-5 = slice_qp & 63. */
    v2_put_byte(&st->v2, 4u, (uint32_t)c->slice_qp & 0x3fu);

    /* +5 b6: long-term reference in use, unless the virtual period divides
     * the frame count. */
    lt = (c->long_term_ref_enable != 0u
          && c->frame_count > c->n_long_ref_poc
          && !(c->virtual_period != 0u
               && (c->frame_count % c->virtual_period) == 0u)) ? 1u : 0u;
    v2_put_byte(&st->v2, 5u,
                (v2_get_byte(&st->v2, 5u) & ~0x40u) | (lt ? 0x40u : 0u));

    /* +15 b3: cleared for DRAM types 6/9/10, set otherwise. */
    if (dram_type == 6u || dram_type == 9u || dram_type == 10u)
        v2_put_byte(&st->v2, 15u, v2_get_byte(&st->v2, 15u) & ~0x08u);
    else
        v2_put_byte(&st->v2, 15u, v2_get_byte(&st->v2, 15u) | 0x08u);

    regs[0] = st->v2.picture_control;
    regs[1] = st->v2.slice_stride_control;
    regs[2] = st->v2.hw_slice_header;
    regs[3] = st->v2.me_control;
    regs[4] = over_time_mb_count;
    regs[5] = mv_addr_phy;
}

/* 3D (temporal noise) filter helpers, from the 3D-filter spec. */
unsigned int freecodec_h264_3d_enabled(unsigned int level, unsigned int picture_index)
{
    return (level != 0u && picture_index != 0u) ? 1u : 0u;     /* §3 */
}

unsigned int freecodec_h264_3d_threshold(unsigned int level, int slice_qp,
                                         unsigned int prev_t)
{
    /* §2.2: only the QP 20..30 band doubles the level; QP >= 52 keeps T. */
    if (slice_qp >= 52)
        return prev_t;
    if (slice_qp >= 20 && slice_qp <= 30)
        return 2u * level;
    return level;
}

unsigned int freecodec_h264_3d_fill(unsigned int level)
{
    /* §5: F = max(0xCC, ((16 - level) * 0x11) & 0xFF): 0xFF, 0xEE, 0xDD, then 0xCC. */
    unsigned int f = ((16u - level) * 0x11u) & 0xffu;

    return f > 0xccu ? f : 0xccu;
}

/* Per-frame VETOP register script (register-config step) from
 * spec/h264-config-registers.md and the full capture; golden-authoritative. */
static unsigned int pic_idx(unsigned int idx)
{
    return (idx < 5u) ? idx : 0u;
}

void freecodec_h264_config_registers(const freecodec_h264_config_reg_cfg *cfg,
                                     uint32_t regs[0x200])
{
    unsigned int i;

    /* 0x04: the ver1 picture-control word | emulation-prevention control (bit 31). Stored from context every frame;
     * the slice step re-stores it before the kick. */
    regs[0x04u / 4u] = cfg->rpara0_v1 | 0x80000000u;

    /* 0x08: slice_stride_control. The 3D step sets the 3-D filter enable (bit 22) when the level is
     * non-zero and this is not the instance's first picture, and clears it otherwise (3D-filter spec
     * §3: picture index, not the ring slot); dynamic ME ORs bit 8 + misc bits (see spec r2/02
     * section 2.3). Always stored from context so a cleared block cannot leave the dynamic-ME enable
     * zero (spec sec. 5.6). */
    {
        uint32_t rpara1 = cfg->rpara1 & ~(1u << 22);

        if (freecodec_h264_3d_enabled(cfg->filter_3d_level, cfg->picture_index))
            rpara1 |= 1u << 22;                 /* 3-D filter enable */
        if (cfg->dynamic_me_enable) {
            rpara1 |= 0x100u;                   /* dynamic-ME enable */
            rpara1 |= (cfg->dyn_me_extra & 0x3fu) << 10;
        }
        regs[0x08u / 4u] = rpara1;
    }

    /* 0x14: constant. */
    regs[0x14u / 4u] = 0x0000000fu;

    /* 0x18/0x20: bit-writer state from the prefix-NAL init step, else zero.
     * Deterministic from context; 0x18/0x20 are outside the write list. */
    regs[0x18u / 4u] = (cfg->temporal_svc != 0u) ? ((7u << 8) | 1u) : 0u;
    regs[0x20u / 4u] = 0u;

    /* 0x1c: the one read-modify-write (spec ve-register-lifecycle.md section
     * 5.1) - OR in the three IRQ-enable bits. The caller supplies 0x1c's status
     * base (zero after the VE reset pulse, so this yields 7). */
    regs[0x1cu / 4u] |= 7u;

    /* 0x28: intra refresh; bit 31 = enable, [15:0] = MBs per refresh block
     * (coded width in MBs / nBlockNumber). 0 when disabled. */
    if (cfg->intra_refresh_enable && cfg->intra_refresh_block_number != 0u)
        regs[0x28u / 4u] = 0x80000000u
                         | (cfg->dst_width_mb
                            / cfg->intra_refresh_block_number);
    else
        regs[0x28u / 4u] = 0u;

    /* 0x2c: rate-control init. Clamp a too-small mb_rc_control to 0x1900; when
     * classify runs, store enable bits 25/26/27/30/31 | per-picture target. */
    {
        uint32_t rc = cfg->rrate_ctrl_init;

        if ((rc & 0x00ffffffu) < 0x1000u)
            rc = (rc & 0xff000000u) | 0x1900u;
        if (cfg->classify_engine_enable || cfg->mb_rc_enable) {
            uint32_t bits = 0u;

            if (cfg->binned_image_output_enable)
                bits |= 1u << 25;
            if (cfg->use_hp_filter)
                bits |= 1u << 26;
            if (cfg->qp_mad_sse_output_flag)
                bits |= 1u << 27;
            if (cfg->mb_rc_enable)
                bits |= 1u << 30;
            if (cfg->classify_engine_enable)
                bits |= 1u << 31;
            rc = bits | (cfg->target_bits & 0x00ffffffu);
        }
        regs[0x2cu / 4u] = rc;
    }

    /* 0x30/0x34/0x38: classify-engine MAD thresholds, stored from context every
     * frame so the fixed table is never left zero (spec sec. 5.6). */
    regs[0x30u / 4u] = cfg->classify_th[0];
    regs[0x34u / 4u] = cfg->classify_th[1];
    regs[0x38u / 4u] = cfg->classify_th[2];

    /* 0x3c: ctu_info output buffer physical address (0 when the buffer is unset). */
    regs[0x3cu / 4u] = cfg->ctu_info_phy;

    /* 0x40: adaptive_control | HP coefficients | mode_ctrl_en (separate context fields). */
    regs[0x40u / 4u] = cfg->rsmart_func
                     | ((cfg->hp_coef_th & 7u) << 26)
                     | ((cfg->hp_coef_shift & 7u) << 29)
                     | (cfg->mode_ctrl_en ? (1u << 25) : 0u);

    /* 0x44: hp_variance_control with contrast/mad bytes replaced by the HP thresholds.
     * v5 only (IC version > 0x2100f). */
    {
        uint32_t v = 0u;

        if (cfg->ic_version > 0x2100fu) {
            v = (cfg->rtemporal_para_v5 & 0xffff0000u)
              | (cfg->hp_contrast_th & 0xffu)
              | ((cfg->hp_mad_th & 0xffu) << 8);
        }
        regs[0x44u / 4u] = v;
    }

    /* 0x48/0x4c: dynamic-ME thresholds (the motion-estimation init step),
     * stored from context every frame so they are never left zero (spec sec. 5.6). */
    regs[0x48u / 4u] = (cfg->dyn_me_th[0] & 0xffffu)
                     | ((cfg->dyn_me_th[1] & 0xffffu) << 16);
    regs[0x4cu / 4u] = (cfg->dyn_me_th[2] & 0xffffu)
                     | ((cfg->dyn_me_th[3] & 0xffffu) << 16);

    /* 0x78/0x7c: long-term reference picture planes (idx = lt_ref), else zero. */
    if (cfg->long_term_ref_enable) {
        i = pic_idx(cfg->lt_ref);
        regs[0x78u / 4u] = cfg->enc_pic_y[i];
        regs[0x7cu / 4u] = cfg->enc_pic_c[i];
    } else {
        regs[0x78u / 4u] = 0u;
        regs[0x7cu / 4u] = 0u;
    }

    /* 0x80/0x84: bitstream buffer physical bounds. */
    regs[0x80u / 4u] = cfg->bitstream_base_phy;
    regs[0x84u / 4u] = cfg->bitstream_end_phy;

    /* 0x88 write offset / 0x8c end offset in bits. End clamps to 0x0FFF0000 when
     * greater and IC version <= 0x2110F; V833 (0x21110/0x21210) is not clamped. */
    regs[0x88u / 4u] = cfg->out_buffer_offset << 3;
    {
        uint32_t end_bits =
            (cfg->out_buffer_offset + cfg->valid_buffer_size) * 8u;

        if (end_bits > 0x0fff0000u && cfg->ic_version <= 0x0002110fu)
            end_bits = 0x0fff0000u;
        regs[0x8cu / 4u] = end_bits;
    }

    /* 0x94: 3D-filter threshold T << 23 (9-bit field), only when the filter is
     * enabled, else left at the reset value 0 (3D-filter spec §2; the captured
     * level << 24 is T << 23 in the QP 20..30 band, where T = 2 x level). */
    if (freecodec_h264_3d_enabled(cfg->filter_3d_level, cfg->picture_index))
        regs[0x94u / 4u] = (cfg->filter_3d_threshold & 0x1ffu) << 23;
    else
        regs[0x94u / 4u] = 0u;

    /* 0x9c: macroblock rate-control buffer physical address, stored from context
     * every frame so it is never left zero (spec sec. 5.6). */
    regs[0x9cu / 4u] = cfg->mb_rate_ctrl_phy;

    /* 0xa0/0xa4 reference + 0xb0/0xb4 reconstruction planes. Non-long-term
     * (default) path: reference the ring's last slot, reconstruct into current. */
    if (cfg->long_term_ref_enable == 0u) {
        i = pic_idx(cfg->main_ring_last_idx);
        regs[0xa0u / 4u] = cfg->enc_pic_y[i];
        regs[0xa4u / 4u] = cfg->enc_pic_c[i];
        i = pic_idx(cfg->main_ring_curr_idx);
        regs[0xb0u / 4u] = cfg->enc_pic_y[i];
        regs[0xb4u / 4u] = cfg->enc_pic_c[i];
        regs[0xb8u / 4u] = cfg->sub_enc_pic_y[pic_idx(cfg->sub_ring_last_idx)];
        regs[0xbcu / 4u] = cfg->sub_enc_pic_y[pic_idx(cfg->sub_ring_curr_idx)];
    } else {
        if (cfg->virtual_period == 0u ||
            (cfg->frame_count % cfg->virtual_period) != 0u) {
            i = pic_idx(cfg->st_ref);
            regs[0xb8u / 4u] = cfg->sub_enc_pic_y[pic_idx(cfg->sub_st_ref)];
        } else {
            i = pic_idx(cfg->lt_ref);
            regs[0xb8u / 4u] = cfg->sub_enc_pic_y[pic_idx(cfg->sub_lt_ref)];
        }
        regs[0xa0u / 4u] = cfg->enc_pic_y[i];
        regs[0xa4u / 4u] = cfg->enc_pic_c[i];
        i = pic_idx(cfg->rec);
        regs[0xb0u / 4u] = cfg->enc_pic_y[i];
        regs[0xb4u / 4u] = cfg->enc_pic_c[i];
        regs[0xbcu / 4u] = cfg->sub_enc_pic_y[pic_idx(cfg->sub_rec)];
    }

    /* 0xa8/0xac: 3D-filter planes, zero when off. 0xac (current slot) on every
     * picture with level != 0; 0xa8 (last slot) only when the filter is enabled,
     * so the first picture leaves it 0 (3D-filter spec §4). */
    regs[0xa8u / 4u] = 0u;
    regs[0xacu / 4u] = 0u;
    if (cfg->filter_3d_level != 0u) {
        regs[0xacu / 4u] = cfg->filter_3d_phy[cfg->main_ring_curr_idx < 2u
                                              ? cfg->main_ring_curr_idx : 0u];
        if (freecodec_h264_3d_enabled(cfg->filter_3d_level, cfg->picture_index))
            regs[0xa8u / 4u] = cfg->filter_3d_phy[cfg->main_ring_last_idx < 2u
                                                  ? cfg->main_ring_last_idx : 0u];
    }

    /* 0xc0/0xc4: macroblock-info / deblock buffers. */
    regs[0xc0u / 4u] = cfg->mb_info_phy;
    regs[0xc4u / 4u] = cfg->dblk_phy;

    /* 0xc8: sub-picture LT plane, zero when LT is off. */
    if (cfg->long_term_ref_enable)
        regs[0xc8u / 4u] = cfg->sub_enc_pic_y[pic_idx(cfg->sub_lt_ref)];
    else
        regs[0xc8u / 4u] = 0u;

    /* 0xf8: img-bin output plane, P-slices only, else zero. */
    if (cfg->slice_type == 0u && cfg->use_img_bin_flag)
        regs[0xf8u / 4u] = cfg->img_bin_phy;
    else
        regs[0xf8u / 4u] = 0u;

    /* 0xfc: ctu-mode-control buffer, zero when the feature is off. */
    if (cfg->mode_ctrl_en)
        regs[0xfcu / 4u] = cfg->ctu_mode_ctrl_phy;
    else
        regs[0xfcu / 4u] = 0u;
}
