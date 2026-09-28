/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.265 VPS/SPS/PPS and slice-header builders (spec h265/13-headers.md): an
 * MSB-first bit writer, Exp-Golomb mapping, emulation prevention and the exact
 * field values decoded from the captured streams. */

#include <string.h>

#include "freecodec/h265_headers.h"

/* One parameter-set RBSP is well under this. */
#define H265_BW_MAX_BYTES 512

typedef struct h265_bw {
    unsigned char buf[H265_BW_MAX_BYTES];
    unsigned int  nbits;
    int           overflow;
} h265_bw;

static void bw_init(h265_bw *bw)
{
    memset(bw, 0, sizeof(*bw));
}

static void bw_bit(h265_bw *bw, unsigned int bit)
{
    if (bw->nbits >= H265_BW_MAX_BYTES * 8u) {
        bw->overflow = 1;
        return;
    }
    if (bit & 1u)
        bw->buf[bw->nbits >> 3] |= (unsigned char)(0x80u >> (bw->nbits & 7u));
    bw->nbits++;
}

/* Append the low `n` bits of `val`, most-significant bit first. */
static void bw_bits(h265_bw *bw, unsigned int val, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        bw_bit(bw, (val >> (n - 1u - i)) & 1u);
}

/* ue(v): unsigned Exp-Golomb (H.265 9.2.1). */
static void bw_ue(h265_bw *bw, unsigned int val)
{
    unsigned int code = val + 1u;
    unsigned int lead = 0;
    unsigned int x = code;

    while (x > 1u) {
        x >>= 1;
        lead++;
    }
    bw_bits(bw, 0u, lead);
    bw_bits(bw, code, lead + 1u);
}

/* se(v): signed Exp-Golomb (H.265 9.2.2). */
static void bw_se(h265_bw *bw, int val)
{
    unsigned int code;

    if (val <= 0)
        code = (unsigned int)(-2 * val);
    else
        code = (unsigned int)(2 * val - 1);
    bw_ue(bw, code);
}

static void bw_trailing(h265_bw *bw)
{
    bw_bit(bw, 1u);
    while (bw->nbits & 7u)
        bw_bit(bw, 0u);
}

static int bw_bytes(const h265_bw *bw)
{
    return (int)((bw->nbits + 7u) >> 3);
}

/* profile_tier_level(profilePresentFlag=1, max_sub_layers_minus1) for
 * max_sub_layers_minus1 == 0: the general block, then general_level_idc. */
static void bw_ptl(h265_bw *bw, const freecodec_h265_ptl_cfg *ptl)
{
    bw_bits(bw, ptl->profile_space, 2);
    bw_bit(bw, ptl->tier_flag);
    bw_bits(bw, ptl->profile_idc, 5);
    bw_bits(bw, ptl->compatibility, 32);
    /* progressive / interlaced / non-packed / frame-only, all zero. */
    bw_bit(bw, 0u); bw_bit(bw, 0u); bw_bit(bw, 0u); bw_bit(bw, 0u);
    /* Constraint and reserved field: 44 bits, all zero (13 section 4). */
    bw_bits(bw, 0u, 44);
    bw_bits(bw, ptl->level_idc, 8);
    /* max_sub_layers_minus1 == 0: no sub-layer PTL flags. */
}

int freecodec_h265_write_ue(unsigned char *out, int cap, unsigned int val)
{
    h265_bw bw;

    if (!out || cap <= 0)
        return -1;
    bw_init(&bw);
    bw_ue(&bw, val);
    if (bw.overflow || bw_bytes(&bw) > cap)
        return -1;
    memcpy(out, bw.buf, (size_t)bw_bytes(&bw));
    return bw_bytes(&bw);
}

int freecodec_h265_write_se(unsigned char *out, int cap, int val)
{
    h265_bw bw;

    if (!out || cap <= 0)
        return -1;
    bw_init(&bw);
    bw_se(&bw, val);
    if (bw.overflow || bw_bytes(&bw) > cap)
        return -1;
    memcpy(out, bw.buf, (size_t)bw_bytes(&bw));
    return bw_bytes(&bw);
}

/* Emulation prevention (H.265 7.4.2.1): insert 0x03 after two zero bytes when
 * the next payload byte is 0..3. */
static int add_epb(const unsigned char *in, int n, unsigned char *out, int cap)
{
    int i, o = 0, zeros = 0;

    for (i = 0; i < n; i++) {
        if (zeros >= 2 && in[i] <= 3u) {
            if (o >= cap)
                return -1;
            out[o++] = 0x03;
            zeros = 0;
        }
        if (o >= cap)
            return -1;
        out[o++] = in[i];
        if (in[i] == 0u)
            zeros++;
        else
            zeros = 0;
    }
    return o;
}

/* Assemble: four-byte start code, two-byte NAL header (byte0 = type << 1,
 * byte1 = 0x01), then the RBSP with emulation prevention (13 section 2). */
static int assemble(unsigned char *out, int cap, unsigned int nal_type,
                    const unsigned char *rbsp, int rbsp_len)
{
    unsigned char ebsp[H265_BW_MAX_BYTES];
    int el;

    el = add_epb(rbsp, rbsp_len, ebsp, (int)sizeof(ebsp));
    if (el < 0 || 6 + el > cap)
        return -1;
    out[0] = 0x00; out[1] = 0x00; out[2] = 0x00; out[3] = 0x01;
    out[4] = (unsigned char)(nal_type << 1);
    out[5] = 0x01;
    memcpy(out + 6, ebsp, (size_t)el);
    return 6 + el;
}

int freecodec_h265_rbsp_removed_size(const unsigned char *nal, int nal_bytes)
{
    int i, zeros = 0, n = 0;

    if (!nal || nal_bytes <= 0)
        return 0;
    for (i = 0; i < nal_bytes; i++) {
        if (zeros >= 2 && nal[i] == 0x03u) {
            zeros = 0;
            continue;
        }
        n++;
        if (nal[i] == 0u)
            zeros++;
        else
            zeros = 0;
    }
    return n;
}

/* ---------------------------------------------------------------- VPS -- */

int freecodec_h265_build_vps(const freecodec_h265_vps_cfg *cfg,
                             unsigned char *out, int cap)
{
    h265_bw bw;

    if (!cfg || !out || cap < 0)
        return -1;
    bw_init(&bw);

    bw_bits(&bw, 0u, 4);                 /* vps_video_parameter_set_id      */
    bw_bit(&bw, 1u);                     /* vps_base_layer_internal_flag    */
    bw_bit(&bw, 1u);                     /* vps_base_layer_available_flag   */
    bw_bits(&bw, 0u, 6);                 /* vps_max_layers_minus1           */
    bw_bits(&bw, cfg->ptl.max_sub_layers_minus1, 3);
    bw_bit(&bw, 1u);                     /* vps_temporal_id_nesting_flag    */
    bw_bits(&bw, 0xffffu, 16);           /* reserved 'ff'                   */
    bw_ptl(&bw, &cfg->ptl);
    bw_bit(&bw, 1u);                     /* vps_sub_layer_ordering_info_present */
    bw_ue(&bw, cfg->max_num_ref_pics);   /* max_dec_pic_buffering_minus1    */
    bw_ue(&bw, 0u);                      /* max_num_reorder_pics            */
    bw_ue(&bw, 0u);                      /* max_latency_increase_plus1      */
    bw_bits(&bw, 0u, 6);                 /* vps_max_layer_id                */
    bw_ue(&bw, 0u);                      /* vps_num_layer_sets_minus1       */
    bw_bit(&bw, 0u);                     /* vps_timing_info_present_flag    */
    bw_bit(&bw, 0u);                     /* vps_extension_flag              */
    bw_trailing(&bw);

    if (bw.overflow)
        return -1;
    return assemble(out, cap, 32u, bw.buf, bw_bytes(&bw));
}

/* ---------------------------------------------------------------- SPS -- */

/* Short-term reference-picture set syntax (spec 13 section 7). Set 0 is
 * explicit with one used entry; the middle sets predict from the previous one;
 * the final set is explicit-empty. */
static void bw_rps_sets(h265_bw *bw, unsigned int num_sets)
{
    unsigned int i;

    for (i = 0; i < num_sets; i++) {
        if (i == 0u) {
            /* stRpsIdx == 0: no inter_ref_pic_set_prediction_flag. */
            bw_ue(bw, 1u);               /* num_negative_pics               */
            bw_ue(bw, 0u);               /* num_positive_pics               */
            bw_ue(bw, 0u);               /* delta_poc_s0_minus1             */
            bw_bit(bw, 1u);              /* used_by_curr_pic_s0_flag        */
        } else {
            /* stRpsIdx != 0 always carries the 1-bit prediction flag,
             * including the final set (spec 13 section 7). */
            unsigned int predict = (i + 1u == num_sets) ? 0u : 1u;
            bw_bit(bw, predict);         /* inter_ref_pic_set_prediction_flag */
            if (!predict) {
                bw_ue(bw, 0u);           /* num_negative_pics               */
                bw_ue(bw, 0u);           /* num_positive_pics               */
            } else {
                /* delta_idx_minus1 never emitted: only at idx == num sets. */
                bw_bit(bw, 1u);          /* delta_rps_sign                  */
                bw_ue(bw, 0u);           /* abs_delta_rps_minus1            */
                /* The reference set has one delta POC; H.265 7.3.2.2.2 loops
                 * inclusively over NumDeltaPocs+1, so two used flags and one
                 * use_delta flag are emitted: used[0]=0 with use_delta[0]=0,
                 * then used[1]=1 (spec 13 section 7). */
                bw_bit(bw, 0u);          /* used_by_curr_pic_flag[0]        */
                bw_bit(bw, 0u);          /* use_delta_flag[0]               */
                bw_bit(bw, 1u);          /* used_by_curr_pic_flag[1]        */
            }
        }
    }
}

int freecodec_h265_build_sps(const freecodec_h265_sps_cfg *cfg,
                             unsigned char *out, int cap)
{
    h265_bw bw;

    if (!cfg || !out || cap < 0)
        return -1;
    bw_init(&bw);

    bw_bits(&bw, 0u, 4);                 /* sps_video_parameter_set_id      */
    bw_bits(&bw, cfg->ptl.max_sub_layers_minus1, 3);
    bw_bit(&bw, 1u);                     /* sps_temporal_id_nesting_flag    */
    bw_ptl(&bw, &cfg->ptl);
    bw_ue(&bw, 0u);                      /* sps_seq_parameter_set_id        */
    bw_ue(&bw, 1u);                      /* chroma_format_idc = 4:2:0       */
    bw_ue(&bw, cfg->pic_width_in_luma_samples);
    bw_ue(&bw, cfg->pic_height_in_luma_samples);
    bw_bit(&bw, 1u);                     /* conformance_window_flag         */
    bw_ue(&bw, cfg->conf_win_left_offset);
    bw_ue(&bw, cfg->conf_win_right_offset);
    bw_ue(&bw, cfg->conf_win_top_offset);
    bw_ue(&bw, cfg->conf_win_bottom_offset);
    bw_ue(&bw, 0u);                      /* bit_depth_luma_minus8           */
    bw_ue(&bw, 0u);                      /* bit_depth_chroma_minus8         */
    bw_ue(&bw, cfg->log2_max_pic_order_cnt_lsb_minus4);
    bw_bit(&bw, 1u);                     /* sps_sub_layer_ordering_info_present */
    bw_ue(&bw, cfg->max_num_ref_pics);
    bw_ue(&bw, 0u);                      /* max_num_reorder_pics            */
    bw_ue(&bw, 0u);                      /* max_latency_increase_plus1      */
    bw_ue(&bw, 0u);                      /* log2_min_luma_coding_block_size_minus3 */
    bw_ue(&bw, 2u);                      /* log2_diff_max_min_luma_coding_block_size */
    bw_ue(&bw, 0u);                      /* log2_min_luma_transform_block_size_minus2 */
    bw_ue(&bw, 3u);                      /* log2_diff_max_min_luma_transform_block_size */
    bw_ue(&bw, 1u);                      /* max_transform_hierarchy_depth_inter */
    bw_ue(&bw, 1u);                      /* max_transform_hierarchy_depth_intra */
    bw_bit(&bw, 0u);                     /* scaling_list_enabled_flag       */
    bw_bit(&bw, 0u);                     /* amp_enabled_flag                */
    bw_bit(&bw, cfg->sao_enabled);       /* sample_adaptive_offset_enabled_flag */
    bw_bit(&bw, 0u);                     /* pcm_enabled_flag                */
    bw_ue(&bw, cfg->num_short_term_ref_pic_sets);
    /* st_ref_pic_set() sits immediately after num_short_term_ref_pic_sets,
     * before long_term_ref_pics_present_flag (H.265 7.3.2.2.1; spec 13 s5/s7). */
    bw_rps_sets(&bw, cfg->num_short_term_ref_pic_sets);
    bw_bit(&bw, 0u);                     /* long_term_ref_pics_present_flag */
    bw_bit(&bw, cfg->temporal_mvp_enabled);
    bw_bit(&bw, 0u);                     /* strong_intra_smoothing_enabled_flag */
    bw_bit(&bw, 0u);                     /* vui_parameters_present_flag     */
    bw_bit(&bw, 0u);                     /* sps_extension_present_flag      */
    bw_trailing(&bw);

    if (bw.overflow)
        return -1;
    return assemble(out, cap, 33u, bw.buf, bw_bytes(&bw));
}

/* ---------------------------------------------------------------- PPS -- */

int freecodec_h265_build_pps(const freecodec_h265_pps_cfg *cfg,
                             unsigned char *out, int cap)
{
    h265_bw bw;

    if (!cfg || !out || cap < 0)
        return -1;
    bw_init(&bw);

    bw_ue(&bw, 0u);                      /* pps_pic_parameter_set_id        */
    bw_ue(&bw, 0u);                      /* pps_seq_parameter_set_id        */
    bw_bit(&bw, 0u);                     /* dependent_slice_segments_enabled_flag */
    bw_bit(&bw, 0u);                     /* output_flag_present_flag        */
    bw_bits(&bw, 0u, 3);                 /* num_extra_slice_header_bits     */
    bw_bit(&bw, 0u);                     /* sign_data_hiding_enabled_flag   */
    bw_bit(&bw, cfg->cabac_init_present);
    bw_ue(&bw, 0u);                      /* num_ref_idx_l0_default_active_minus1 */
    bw_ue(&bw, 0u);                      /* num_ref_idx_l1_default_active_minus1 */
    bw_se(&bw, 0);                       /* init_qp_minus26                 */
    bw_bit(&bw, 0u);                     /* constrained_intra_pred_flag     */
    bw_bit(&bw, cfg->transform_skip_enabled);
    bw_bit(&bw, cfg->cu_qp_delta_enabled);
    if (cfg->cu_qp_delta_enabled)
        bw_ue(&bw, cfg->diff_cu_qp_delta_depth);
    bw_se(&bw, 0);                       /* pps_cb_qp_offset                */
    bw_se(&bw, 0);                       /* pps_cr_qp_offset                */
    bw_bit(&bw, 0u);                     /* pps_slice_chroma_qp_offsets_present_flag */
    bw_bit(&bw, 0u);                     /* weighted_pred_flag              */
    bw_bit(&bw, 0u);                     /* weighted_bipred_flag            */
    bw_bit(&bw, 0u);                     /* transquant_bypass_enabled_flag  */
    bw_bit(&bw, 0u);                     /* tiles_enabled_flag              */
    bw_bit(&bw, 0u);                     /* entropy_coding_sync_enabled_flag */
    bw_bit(&bw, cfg->loop_filter_across_slices_enabled);
    bw_bit(&bw, cfg->deblocking_filter_control_present);
    if (cfg->deblocking_filter_control_present) {
        bw_bit(&bw, cfg->deblocking_filter_override_enabled);
        bw_bit(&bw, cfg->deblocking_filter_disabled);
        bw_se(&bw, 0);                   /* pps_beta_offset_div2            */
        bw_se(&bw, 0);                   /* pps_tc_offset_div2              */
    }
    bw_bit(&bw, 0u);                     /* pps_scaling_list_data_present_flag */
    bw_bit(&bw, 0u);                     /* lists_modification_present_flag */
    bw_ue(&bw, 0u);                      /* log2_parallel_merge_level_minus2 */
    bw_bit(&bw, 0u);                     /* slice_segment_header_extension_present_flag */
    bw_bit(&bw, 0u);                     /* pps_extension_present_flag      */
    bw_trailing(&bw);

    if (bw.overflow)
        return -1;
    return assemble(out, cap, 34u, bw.buf, bw_bytes(&bw));
}

/* ---------------------------------------------------------------- slice -- */

int freecodec_h265_build_slice(const freecodec_h265_slice_cfg *cfg,
                               unsigned char *out, int cap, int *out_bits)
{
    h265_bw bw;
    unsigned char ebsp[H265_BW_MAX_BYTES];
    int el, total, irap;
    unsigned int nal_type;

    if (!cfg || !out || cap < 0)
        return -1;
    bw_init(&bw);

    nal_type = cfg->nal_unit_type;
    irap = (nal_type >= 16u && nal_type <= 23u) ? 1 : 0;

    bw_bit(&bw, 1u);                     /* first_slice_segment_in_pic_flag */
    if (irap)
        bw_bit(&bw, 0u);                 /* no_output_of_prior_pics_flag    */
    bw_ue(&bw, 0u);                      /* slice_pic_parameter_set_id      */
    /* num_extra_slice_header_bits is 0, so no bits here. */
    bw_ue(&bw, cfg->is_i_picture ? 2u : 1u);   /* slice_type: I = 2, P = 1  */

    if (!irap) {
        bw_bits(&bw, cfg->pic_order_cnt_lsb, 8);
        bw_bit(&bw, 1u);                 /* short_term_ref_pic_set_sps_flag */
        bw_bits(&bw, cfg->short_term_ref_pic_set_idx, 5); /* ceil(log2(21)) */
        bw_bit(&bw, cfg->temporal_mvp_enabled);
    }

    /* SAO flags sit outside the IRAP guard (13 section 8 item 8). */
    bw_bit(&bw, cfg->sao_luma_flag);
    bw_bit(&bw, cfg->sao_chroma_flag);

    if (!cfg->is_i_picture) {
        bw_bit(&bw, cfg->num_ref_idx_active_override_flag);
        if (cfg->num_ref_idx_active_override_flag)
            bw_ue(&bw, cfg->num_ref_idx_l0_active_minus1);
        bw_bit(&bw, cfg->cabac_init_flag);
        /* collocated_ref_idx is present only when the L0 list has more than
         * one entry (H.265 7.3.6.1). The PPS default is one active ref
         * (num_ref_idx_l0_default_active_minus1 = 0) and there is no
         * override, so with temporal_mvp enabled the field is absent. */
        if (cfg->temporal_mvp_enabled &&
            cfg->num_ref_idx_active_override_flag &&
            cfg->num_ref_idx_l0_active_minus1 > 0u)
            bw_ue(&bw, cfg->collocated_ref_idx);
        bw_ue(&bw, cfg->five_minus_max_num_merge_cand);
    }

    bw_se(&bw, cfg->slice_qp - 26);      /* slice_qp_delta                  */

    if (cfg->loop_filter_across_slices_enabled)
        bw_bit(&bw, 1u);                 /* slice_loop_filter_across_slices_enabled_flag */

    /* byte_alignment(): a 1 bit then zeros. */
    bw_bit(&bw, 1u);
    while (bw.nbits & 7u)
        bw_bit(&bw, 0u);

    if (bw.overflow)
        return -1;

    /* Start code and NAL header carry no emulation prevention; the RBSP does. */
    el = add_epb(bw.buf, bw_bytes(&bw), ebsp, (int)sizeof(ebsp));
    if (el < 0 || 6 + el > cap)
        return -1;
    out[0] = 0x00; out[1] = 0x00; out[2] = 0x00; out[3] = 0x01;
    out[4] = (unsigned char)(nal_type << 1);
    out[5] = 0x01;
    memcpy(out + 6, ebsp, (size_t)el);
    total = 6 + el;
    if (out_bits != NULL)
        *out_bits = total * 8;
    return total;
}
