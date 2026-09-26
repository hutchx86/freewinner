/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 Annex-B SPS/PPS/slice generator: complete NALs (start code + header +
 * RBSP) via an MSB-first Exp-Golomb bit-writer (spec/h264-internals.md §3). */

#include <string.h>

#include "freecodec/h264_headers.h"

/* One SPS/PPS RBSP is well under this; the whole NAL is built here first so a
 * too-small caller buffer is detected without risking an overrun. */
#define H264_BW_MAX_BYTES 256

typedef struct h264_bw {
    unsigned char buf[H264_BW_MAX_BYTES];
    unsigned int  nbits;
    int           overflow;
} h264_bw;

static void h264_bw_init(h264_bw *bw)
{
    memset(bw, 0, sizeof(*bw));
}

/* Append a single bit, MSB-first. */
static void h264_bw_bit(h264_bw *bw, unsigned int bit)
{
    if (bw->nbits >= H264_BW_MAX_BYTES * 8u) {
        bw->overflow = 1;
        return;
    }
    if (bit & 1u)
        bw->buf[bw->nbits >> 3] |=
            (unsigned char)(0x80u >> (bw->nbits & 7u));
    bw->nbits++;
}

/* Append the low `n` bits of `val`, most-significant bit first. */
static void h264_bw_bits(h264_bw *bw, unsigned int val, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++)
        h264_bw_bit(bw, (val >> (n - 1u - i)) & 1u);
}

/* ue(v): Exp-Golomb unsigned, Annex-7 clause 9.1. */
static void h264_bw_ue(h264_bw *bw, unsigned int val)
{
    unsigned int code = val + 1u;
    unsigned int lead = 0;
    unsigned int x = code;

    while (x > 1u) {
        x >>= 1;
        lead++;
    }
    h264_bw_bits(bw, 0, lead);        /* leading_zero_bits */
    h264_bw_bits(bw, code, lead + 1u); /* 1 + info bits      */
}

/* se(v): Exp-Golomb signed, Annex-7 clause 9.1.1. */
static void h264_bw_se(h264_bw *bw, int val)
{
    unsigned int code;

    if (val <= 0)
        code = (unsigned int)(-2 * val);
    else
        code = (unsigned int)(2 * val - 1);
    h264_bw_ue(bw, code);
}

/* rbsp_trailing_bits: stop bit then zero-fill to a byte boundary. */
static void h264_bw_trailing(h264_bw *bw)
{
    h264_bw_bit(bw, 1u);
    while (bw->nbits & 7u)
        h264_bw_bit(bw, 0u);
}

/* Copy the finished RBSP out, enforcing the caller's capacity. */
static int h264_bw_finish(const h264_bw *bw, unsigned char *out, int cap)
{
    int len;

    if (bw->overflow)
        return -1;
    len = (int)((bw->nbits + 7u) >> 3);
    if (len > cap)
        return -1;
    memcpy(out, bw->buf, (size_t)len);
    return len;
}

int freecodec_h264_build_sps(const freecodec_h264_seq_cfg *cfg,
                             unsigned char *out, int cap)
{
    h264_bw bw;
    int      crop;

    if (!cfg || !out || cap < 0)
        return -1;

    h264_bw_init(&bw);

    h264_bw_bits(&bw, 0x00000001u, 32);      /* start code prefix      */
    h264_bw_bits(&bw, 0x67u, 8);             /* nal_ref_idc 3, type 7  */
    h264_bw_bits(&bw, cfg->profile_idc, 8);
    h264_bw_bits(&bw, 0u, 8);                /* constraint_set* + reserved */
    h264_bw_bits(&bw, cfg->level_idc, 8);
    h264_bw_ue(&bw, 0u);                     /* seq_parameter_set_id   */

    /* High-profile family (100/110/122/244/44/83/86/118/128/138/139/134/135):
     * the chroma/bit-depth extension fields follow seq_parameter_set_id. */
    if (cfg->profile_idc == 100u || cfg->profile_idc == 110u ||
        cfg->profile_idc == 122u || cfg->profile_idc == 244u ||
        cfg->profile_idc == 44u  || cfg->profile_idc == 83u  ||
        cfg->profile_idc == 86u  || cfg->profile_idc == 118u ||
        cfg->profile_idc == 128u || cfg->profile_idc == 138u ||
        cfg->profile_idc == 139u || cfg->profile_idc == 134u ||
        cfg->profile_idc == 135u) {
        h264_bw_ue(&bw, 1u);                 /* chroma_format_idc: 4:2:0 */
        h264_bw_ue(&bw, 0u);                 /* bit_depth_luma_minus8    */
        h264_bw_ue(&bw, 0u);                 /* bit_depth_chroma_minus8  */
        h264_bw_bit(&bw, 0u);                /* qpprime_y_zero_transform_bypass */
        h264_bw_bit(&bw, 0u);                /* seq_scaling_matrix_present */
    }

    h264_bw_ue(&bw, cfg->log2_max_frame_num - 4u);
    h264_bw_ue(&bw, cfg->pic_order_cnt_type);
    if (cfg->pic_order_cnt_type == 0u)
        h264_bw_ue(&bw, cfg->log2_max_pic_order_cnt_lsb - 4u);

    h264_bw_ue(&bw, cfg->num_ref_frames);
    h264_bw_bit(&bw, cfg->gaps_in_frame_num_allowed);
    h264_bw_ue(&bw, cfg->pic_width_in_mbs - 1u);
    h264_bw_ue(&bw, cfg->pic_height_in_map_units - 1u);

    h264_bw_bit(&bw, cfg->frame_mbs_only_flag);
    if (!cfg->frame_mbs_only_flag)
        h264_bw_bit(&bw, 0u);                /* mb_adaptive_frame_field_flag */
    h264_bw_bit(&bw, cfg->direct_8x8_inference_flag);

    crop = (cfg->crop_left_units != 0u || cfg->crop_right_units != 0u ||
            cfg->crop_top_units != 0u || cfg->crop_bottom_units != 0u);
    h264_bw_bit(&bw, (unsigned int)crop);
    if (crop) {
        h264_bw_ue(&bw, cfg->crop_left_units);   /* frame_crop_left_offset  */
        h264_bw_ue(&bw, cfg->crop_right_units);
        h264_bw_ue(&bw, cfg->crop_top_units);    /* frame_crop_top_offset   */
        h264_bw_ue(&bw, cfg->crop_bottom_units);
    }

    h264_bw_bit(&bw, cfg->vui_flag);         /* vui_parameters_present_flag */
    h264_bw_trailing(&bw);

    return h264_bw_finish(&bw, out, cap);
}

int freecodec_h264_build_pps(const freecodec_h264_pic_cfg *cfg,
                             unsigned char *out, int cap)
{
    h264_bw bw;

    if (!cfg || !out || cap < 0)
        return -1;

    h264_bw_init(&bw);

    h264_bw_bits(&bw, 0x00000001u, 32);      /* start code prefix      */
    h264_bw_bits(&bw, 0x68u, 8);             /* nal_ref_idc 3, type 8  */
    h264_bw_ue(&bw, 0u);                     /* pic_parameter_set_id   */
    h264_bw_ue(&bw, 0u);                     /* seq_parameter_set_id   */
    h264_bw_bit(&bw, cfg->entropy_coding_mode_flag);
    h264_bw_bit(&bw, 0u);                    /* bottom_field_pic_order_in_frame_present_flag */
    h264_bw_ue(&bw, 0u);                     /* num_slice_groups_minus1 */
    h264_bw_ue(&bw, cfg->num_ref_idx_l0_default_active - 1u);
    h264_bw_ue(&bw, cfg->num_ref_idx_l1_default_active - 1u);
    h264_bw_bit(&bw, 0u);                    /* weighted_pred_flag     */
    h264_bw_bits(&bw, 0u, 2);                /* weighted_bipred_idc    */
    h264_bw_se(&bw, (int)cfg->pic_init_qp_minus26);
    h264_bw_se(&bw, 0);                      /* pic_init_qs_minus26    */
    h264_bw_se(&bw, (int)cfg->chroma_qp_index_offset);
    h264_bw_bit(&bw, cfg->deblocking_filter_control_present);
    h264_bw_bit(&bw, cfg->constrained_intra_pred_flag);
    h264_bw_bit(&bw, 0u);                    /* redundant_pic_cnt_present_flag */

    h264_bw_trailing(&bw);

    return h264_bw_finish(&bw, out, cap);
}

int freecodec_h264_build_idr_slice(const freecodec_h264_idr_slice_cfg *cfg,
                                   unsigned char *out, int cap, int *out_bits)
{
    h264_bw bw;

    if (!cfg || !out || cap < 0)
        return -1;
    if (cfg->pic_order_cnt_type != 0u)
        return -1; /* only POC type 0 is specified */

    h264_bw_init(&bw);

    h264_bw_bits(&bw, 0x00000001u, 32);      /* start code prefix      */
    h264_bw_bits(&bw, 0x65u, 8);             /* nal_ref_idc 3, type 5  */
    h264_bw_ue(&bw, 0u);                     /* first_mb_in_slice      */
    h264_bw_ue(&bw, 7u);                     /* slice_type: I, all     */
    h264_bw_ue(&bw, cfg->pic_parameter_set_id);
    h264_bw_bits(&bw, cfg->frame_num, cfg->log2_max_frame_num);
    h264_bw_ue(&bw, cfg->idr_pic_id);
    h264_bw_bits(&bw, cfg->pic_order_cnt_lsb,
                 cfg->log2_max_pic_order_cnt_lsb);
    h264_bw_bits(&bw, 0u, 2);                /* dec_ref_pic_marking 0,0 */
    h264_bw_se(&bw, (int)cfg->slice_qp - (int)cfg->pic_init_qp);

    h264_bw_ue(&bw, cfg->disable_deblocking_filter_idc);
    if (cfg->disable_deblocking_filter_idc != 1u) {
        h264_bw_se(&bw, cfg->alpha_offset_div2);
        h264_bw_se(&bw, cfg->beta_offset_div2);
    }

    /* CABAC: cabac_alignment_one_bit until byte-aligned (H.264 7.3.3); an I
     * slice has no cabac_init_idc. CAVLC skips this. */
    if (cfg->entropy_coding_mode_flag) {
        while (bw.nbits & 7u)
            h264_bw_bit(&bw, 1u);
    }

    /* No rbsp_trailing_bits after a slice header (spec/h264-slice-header.md 3a):
     * the MB coder continues at bit L; storage is zero-padded by the writer. */
    if (out_bits != NULL)
        *out_bits = (int)bw.nbits;

    return h264_bw_finish(&bw, out, cap);
}

int freecodec_h264_build_p_slice(const freecodec_h264_p_slice_cfg *cfg,
                                 unsigned char *out, int cap, int *out_bits)
{
    h264_bw bw;
    unsigned int override;

    if (!cfg || !out || cap < 0)
        return -1;
    if (cfg->pic_order_cnt_type != 0u)
        return -1; /* only POC type 0 is specified */

    h264_bw_init(&bw);

    h264_bw_bits(&bw, 0x00000001u, 32);      /* start code prefix      */
    h264_bw_bits(&bw, (cfg->nal_reference_idc << 5) | 1u, 8);
    h264_bw_ue(&bw, 0u);                     /* first_mb_in_slice      */
    h264_bw_ue(&bw, 5u);                     /* slice_type: P, all     */
    h264_bw_ue(&bw, cfg->pic_parameter_set_id);
    h264_bw_bits(&bw, cfg->frame_num, cfg->log2_max_frame_num);
    h264_bw_bits(&bw, cfg->pic_order_cnt_lsb,
                 cfg->log2_max_pic_order_cnt_lsb);

    override = (cfg->num_ref_idx_l0_active !=
                cfg->pps_num_ref_idx_l0_default) ? 1u : 0u;
    h264_bw_bit(&bw, override);
    if (override)
        h264_bw_ue(&bw, cfg->num_ref_idx_l0_active - 1u);

    h264_bw_bit(&bw, 0u);                    /* ref_pic_list_reordering_flag_l0 */
    if (cfg->nal_reference_idc != 0u)
        h264_bw_bit(&bw, 0u);                /* adaptive_ref_pic_marking_mode_flag */

    /* CABAC cabac_init_idc (ue(v)) for non-I/SI slices (H.264 7.3.3); value per
     * spec r2/02 section 2.1. */
    if (cfg->entropy_coding_mode_flag)
        h264_bw_ue(&bw, cfg->cabac_init_idc);

    h264_bw_se(&bw, (int)cfg->slice_qp - (int)cfg->pic_init_qp);

    h264_bw_ue(&bw, cfg->disable_deblocking_filter_idc);
    if (cfg->disable_deblocking_filter_idc != 1u) {
        h264_bw_se(&bw, cfg->alpha_offset_div2);
        h264_bw_se(&bw, cfg->beta_offset_div2);
    }

    /* CABAC: cabac_alignment_one_bit until byte-aligned (H.264 7.3.3); skipped
     * for CAVLC, so the CAVLC A/B path is unchanged. */
    if (cfg->entropy_coding_mode_flag) {
        while (bw.nbits & 7u)
            h264_bw_bit(&bw, 1u);
    }

    /* Exact bit length before the storage pad (spec §3a): the bit-writer consumes
     * exactly L bits and the slice data follows at bit L. */
    if (out_bits != NULL)
        *out_bits = (int)bw.nbits;

    /* Zero-pad to a byte; no rbsp_stop_one_bit here (slice data follows). */
    while (bw.nbits & 7u)
        h264_bw_bit(&bw, 0u);

    return h264_bw_finish(&bw, out, cap);
}
