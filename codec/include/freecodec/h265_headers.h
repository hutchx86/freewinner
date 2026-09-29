/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.265 (HEVC) VPS/SPS/PPS and slice-header generation, per the clean-room
 * behaviour spec h265/13-headers.md. Complete Annex-B NALs are produced:
 * four-byte start code, two-byte NAL header, then the RBSP with emulation-
 * prevention bytes. All bit syntax follows ITU-T H.265 clause 7.3.2/7.3.3 and
 * the field values recorded in 13 sections 3-8. */

#ifndef FREECODEC_H265_HEADERS_H
#define FREECODEC_H265_HEADERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Profile-tier-level inputs (13 section 4). */
typedef struct freecodec_h265_ptl_cfg {
    unsigned int profile_space;        /* general_profile_space              */
    unsigned int tier_flag;            /* general_tier_flag                  */
    unsigned int profile_idc;          /* general_profile_idc, 1 = Main      */
    unsigned int compatibility;        /* general_profile_compatibility_flag[32] */
    unsigned int level_idc;            /* general_level_idc, 123 = 4.1       */
    unsigned int max_sub_layers_minus1;/* no sub-layer PTL flags are emitted */
} freecodec_h265_ptl_cfg;

/* VPS inputs (13 section 3). */
typedef struct freecodec_h265_vps_cfg {
    freecodec_h265_ptl_cfg ptl;
    unsigned int max_num_ref_pics;     /* max_dec_pic_buffering_minus1       */
} freecodec_h265_vps_cfg;

/* SPS inputs (13 section 5). Sizes are in luma samples. */
typedef struct freecodec_h265_sps_cfg {
    freecodec_h265_ptl_cfg ptl;
    unsigned int pic_width_in_luma_samples;
    unsigned int pic_height_in_luma_samples;
    unsigned int conf_win_left_offset;
    unsigned int conf_win_right_offset;
    unsigned int conf_win_top_offset;
    unsigned int conf_win_bottom_offset;
    unsigned int max_num_ref_pics;
    unsigned int num_short_term_ref_pic_sets;    /* gop_size + 1            */
    unsigned int log2_max_pic_order_cnt_lsb_minus4;
    unsigned int sao_enabled;
    unsigned int temporal_mvp_enabled;
} freecodec_h265_sps_cfg;

/* PPS inputs (13 section 6). */
typedef struct freecodec_h265_pps_cfg {
    unsigned int cabac_init_present;
    unsigned int transform_skip_enabled;
    unsigned int cu_qp_delta_enabled;
    unsigned int diff_cu_qp_delta_depth;
    unsigned int deblocking_filter_control_present;
    unsigned int deblocking_filter_override_enabled;
    unsigned int deblocking_filter_disabled;
    unsigned int loop_filter_across_slices_enabled;
} freecodec_h265_pps_cfg;

/* Slice-header inputs (13 section 8). `nal_unit_type` is the H.265 type: 19 for
 * IDR_W_RADL, 1 for TRAIL_R, 21 for the special intra mode. */
typedef struct freecodec_h265_slice_cfg {
    unsigned int nal_unit_type;
    unsigned int is_i_picture;             /* I (no POC/RPS/TMVP) or P        */
    unsigned int pic_order_cnt_lsb;        /* 8 bits, non-IRAP                */
    unsigned int short_term_ref_pic_set_idx; /* 5 bits, non-IRAP              */
    unsigned int temporal_mvp_enabled;
    unsigned int sao_luma_flag;
    unsigned int sao_chroma_flag;
    unsigned int cabac_init_flag;          /* P only, PPS enables it          */
    unsigned int collocated_ref_idx;       /* P only, TMVP on                 */
    unsigned int five_minus_max_num_merge_cand;
    unsigned int num_ref_idx_active_override_flag;
    unsigned int num_ref_idx_l0_active_minus1;
    int          slice_qp;
    unsigned int loop_filter_across_slices_enabled;
} freecodec_h265_slice_cfg;

/* Each builder returns the NAL byte count, or -1 when `cap` is too small. */
int freecodec_h265_build_vps(const freecodec_h265_vps_cfg *cfg,
                             unsigned char *out, int cap);
int freecodec_h265_build_sps(const freecodec_h265_sps_cfg *cfg,
                             unsigned char *out, int cap);
int freecodec_h265_build_pps(const freecodec_h265_pps_cfg *cfg,
                             unsigned char *out, int cap);

/* Slice header: start code + NAL header + RBSP (RBSP bytes get emulation
 * prevention). `out_bits` receives the exact bit length to feed the bit-writer
 * when non-NULL; storage is zero-padded to a byte. Returns bytes or -1. */
int freecodec_h265_build_slice(const freecodec_h265_slice_cfg *cfg,
                               unsigned char *out, int cap, int *out_bits);

/* Slice-header RBSP only (no start code, NAL header or emulation prevention),
 * for the engine bit-writer; `out_bits` is the exact bit length. */
int freecodec_h265_build_slice_rbsp(const freecodec_h265_slice_cfg *cfg,
                                    unsigned char *out, int cap, int *out_bits);

/* Encode one ue(v) / se(v) into a caller buffer (exposed for the tests). */
int freecodec_h265_write_ue(unsigned char *out, int cap, unsigned int val);
int freecodec_h265_write_se(unsigned char *out, int cap, int val);

/* Net byte size of a parameter-set RBSP after emulation-prevention removal. */
int freecodec_h265_rbsp_removed_size(const unsigned char *nal, int nal_bytes);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H265_HEADERS_H */
