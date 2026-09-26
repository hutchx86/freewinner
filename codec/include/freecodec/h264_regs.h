/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 register-shadow unit (spec/h264-reg-shadow.md, spec/h264-slice-regs.md),
 * golden-verified. The shadow is the packed image programmed into the VE window. */

#ifndef FREECODEC_H264_REGS_H
#define FREECODEC_H264_REGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ver2 register shadow (the encoder context's shadow v2 block). Each word name
 * describes the encoder-block register it carries (spec 11 §1/§2); the word
 * order is the shadow layout, not the register order. */
typedef struct freecodec_h264_shadow_v2 {
    uint32_t picture_control;
    uint32_t slice_stride_control;
    uint32_t hw_slice_header;
    uint32_t me_control;
    uint32_t start_control;
    uint32_t mb_rc_control;
    uint32_t rate_ctrl_param_0;
    uint32_t rate_ctrl_param_1;
    uint32_t rate_ctrl_param_2;
    uint32_t adaptive_control;
    uint32_t temporal_param;
    uint32_t hp_variance_control;
    uint32_t dynamic_me_param_0;
    uint32_t dynamic_me_param_1;
    uint32_t bright_threshold;
    uint32_t r3DFilterThreshold;
    uint32_t rIntraRefresh;
    uint32_t roi_qp_offset;
    uint32_t roi_qp_offset_v5;
} freecodec_h264_shadow_v2;

/* Inputs to the shadow builder (the encoder context fields it reads). */
typedef struct freecodec_h264_reg_info_cfg {
    unsigned int profile_idc;
    unsigned int ic_version;
    unsigned int entropy_coding_mode_flag;
    unsigned int cabac_context_index;
    unsigned int deblock_idc;
    int          alpha_offset_div2;
    int          beta_offset_div2;
    int          slice_qp;
    unsigned int dst_width_mb;
    unsigned int dst_height_mb;
    unsigned int display_height_16align;
    unsigned int chroma_qp_index_offset;
    unsigned int alter_frame_en;
    unsigned int transform8x8_mode_flag;
    unsigned int use_intra_in_p;
    unsigned int use_intra_4x4;
    unsigned int mv_addr_vir_null;
    unsigned int binned_image_output_enable;
    unsigned int use_hp_filter;
    unsigned int qp_mad_sse_output_en;
    unsigned int fix_qp_enable;   /* 1 => the shadow's classification-engine-enable bit is cleared */
    unsigned int fast_enc;
    unsigned int filter_3d_level;
    unsigned int th_smart_img_bin;
    unsigned int smart_en;
    unsigned int smart_shift_bits;
    unsigned int hvs_en;
    unsigned int th_hvs_dir;
    unsigned int th_hvs_coef_shift;
    unsigned int skip_tend_pic_en;
    unsigned int mode_ctrl_en;
    unsigned int hpfilter_coef_th;
    unsigned int hpfilter_coef_shift;
    unsigned int hpfilter_contrast_th;
    unsigned int hpfilter_mad_th;
    unsigned int th_bright;
    unsigned int th_dark;
} freecodec_h264_reg_info_cfg;

/* Everything `freecodec_h264_init_reg_info` writes. */
typedef struct freecodec_h264_reg_info_out {
    freecodec_h264_shadow_v2 shadow;
    uint32_t first_frame;
    uint32_t mb_rc_enable;
    uint32_t ctx_flag_a;             /* always 1 */
    uint32_t filter_3d_level;        /* raw 3D-filter level */
    uint32_t dynamic_me_enable;
    uint32_t over_time_mb_count;
    uint32_t ctx_flag_b;             /* always 0 */
    uint32_t transform8x8_mode_flag; /* may be cleared (non-High profile) */
} freecodec_h264_reg_info_out;

/* Build the ver2 register shadow and the associated context flags. */
void freecodec_h264_init_reg_info(const freecodec_h264_reg_info_cfg *cfg,
                                  freecodec_h264_reg_info_out *out);

/* ver1 slice-header shadow (the encoder context's shadow v1 block). */
typedef struct freecodec_h264_shadow_v1 {
    uint32_t w[8];
} freecodec_h264_shadow_v1;

/* Slice-header inputs shared by `freecodec_h264_hw_slice_header` and the
 * `freecodec_h264_set_reg_ver1`/`freecodec_h264_set_reg_ver2` writers. */
typedef struct freecodec_h264_slice_cfg {
    unsigned int coding_type;      /* 18 = IDR, 0 = P */
    unsigned int slice_type;       /* H.264 codes: 2 = I, 1 = B, 0 = P */
    unsigned int model_number;
    unsigned int entropy_coding_mode_flag;
    int          slice_qp;
    unsigned int pic_type;         /* 0 = frame, 1 = top, 2 = bottom */
    unsigned int frame_num;
    unsigned int pic_order_cnt_type;
    unsigned int log2_max_pic_order_cnt_lsb;
    int          top_poc;
    int          bottom_poc;
    unsigned int n_slice_height;
    unsigned int long_term_ref_enable;
    unsigned int frame_count;
    unsigned int n_long_ref_poc;
    unsigned int virtual_period;
    unsigned int motion_parameter;
    /* Persisted shadow from `freecodec_h264_init_reg_info`; the slice step
     * read-modify-writes it. NULL seeds a zero shadow (isolated use). */
    const freecodec_h264_shadow_v2 *seed_shadow;
} freecodec_h264_slice_cfg;

/* Result of `freecodec_h264_hw_slice_header` and input to the set_reg writers;
 * the slice inputs are retained so the writers need no separate config. */
typedef struct freecodec_h264_slice_state {
    unsigned int nal_reference_idc;   /* out (recomputed from coding_type) */
    unsigned int pic_order_cnt_lsb;   /* out */
    freecodec_h264_shadow_v1 v1;
    freecodec_h264_shadow_v2 v2;
    freecodec_h264_slice_cfg cfg;     /* inputs retained by freecodec_h264_hw_slice_header */
} freecodec_h264_slice_state;

/* Fill the slice-header shadow (version 0 = ver1, 1 = ver2) and the
 * nal_reference_idc / pic_order_cnt_lsb values. `arg` selects the POC source. */
void freecodec_h264_hw_slice_header(const freecodec_h264_slice_cfg *cfg,
                                    int arg, int shadow_version,
                                    freecodec_h264_slice_state *st);

/* Apply the slice-header register writes. `regs` receives the six register
 * words (base+0x04, 0x08, 0x0c, 0x10, 0x24, 0x60). */
void freecodec_h264_set_reg_ver1(freecodec_h264_slice_state *st,
                                 unsigned int ic_version,
                                 uint32_t over_time_mb_count,
                                 uint32_t mv_addr_phy,
                                 uint32_t regs[6]);
void freecodec_h264_set_reg_ver2(freecodec_h264_slice_state *st,
                                 unsigned int dram_type,
                                 uint32_t over_time_mb_count,
                                 uint32_t mv_addr_phy,
                                 uint32_t regs[6]);

/* Per-frame VETOP register script (register-config step). `regs` is the
 * 0x800-byte window, word index = offset/4; regs[0x1c/4] must hold 0x200 on entry. */
typedef struct freecodec_h264_config_reg_cfg {
    unsigned int out_buffer_offset;
    unsigned int valid_buffer_size;

    unsigned int ic_version;
    unsigned int profile_idc;
    unsigned int level_idc;
    unsigned int dst_width_mb;
    unsigned int dst_height_mb;
    unsigned int display_width_16align;
    unsigned int display_height_16align;

    unsigned int n_rencode_flag;
    unsigned int slice_type;
    unsigned int coding_type;
    unsigned int n_slice_height;
    int          slice_qp;
    unsigned int curr_frm_idx;
    unsigned int frame_count;

    unsigned int long_term_ref_enable;
    unsigned int temporal_svc;
    unsigned int virtual_period;

    unsigned int use_hp_filter;
    unsigned int hp_coef_th;
    unsigned int hp_coef_shift;
    unsigned int hp_contrast_th;
    unsigned int hp_mad_th;
    unsigned int filter_3d_level;
    unsigned int classify_engine_enable;
    unsigned int mb_rc_enable;
    unsigned int b_test_i_frame;
    unsigned int b_enable_layer_ratio;
    unsigned int dynamic_me_enable;
    unsigned int use_smart_flag;
    unsigned int rc_mode;
    unsigned int qp_mad_sse_output_flag;
    unsigned int mode_ctrl_en;
    unsigned int use_img_bin_flag;
    unsigned int binned_image_output_enable;

    unsigned int intra_refresh_enable;
    unsigned int intra_refresh_block_number;
    unsigned int n_intra_refresh_part;

    /* register shadow words (already built by the shadow unit) */
    unsigned int rpara1;
    unsigned int rrate_ctrl_init;
    unsigned int rsmart_func;
    unsigned int rtemporal_para;
    unsigned int rtemporal_para_v5;
    unsigned int rpara0_v1;

    /* buffer physical addresses (already >>8) */
    unsigned int bitstream_base_phy;
    unsigned int bitstream_end_phy;
    unsigned int mb_info_phy;
    unsigned int dblk_phy;
    unsigned int ctu_mode_ctrl_phy;
    unsigned int ctu_info_phy;
    unsigned int img_bin_phy;
    unsigned int mb_rate_ctrl_phy;

    /* picture address arrays */
    unsigned int enc_pic_y[5];
    unsigned int enc_pic_c[5];
    unsigned int sub_enc_pic_y[5];
    unsigned int main_ring_last_idx, main_ring_curr_idx, lt_ref, st_ref, rec;
    unsigned int sub_ring_last_idx, sub_ring_curr_idx, sub_lt_ref, sub_st_ref, sub_rec;

    /* Inputs the captured context header does not expose; the caller supplies
     * the helper-computed values (0x2c; 0xa8/0xac; 0x08; 0x48/0x4c; 0x30/0x34/0x38). */
    unsigned int target_bits;
    unsigned int filter_3d_phy[2];
    unsigned int dyn_me_extra;
    unsigned int dyn_me_th[4];
    unsigned int classify_th[3];
} freecodec_h264_config_reg_cfg;

void freecodec_h264_config_registers(const freecodec_h264_config_reg_cfg *cfg,
                                     uint32_t regs[0x200]);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H264_REGS_H */
