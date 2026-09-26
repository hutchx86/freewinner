/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 SPS/PPS/slice-header generation: full Annex-B NALs (start code + NAL
 * header + RBSP), golden-verified (spec/h264-internals.md). */

#ifndef FREECODEC_H264_HEADERS_H
#define FREECODEC_H264_HEADERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sequence-parameter-set inputs. Counts are in macroblocks / map units, not
 * the minus-one syntax values. */
typedef struct freecodec_h264_seq_cfg {
    unsigned int profile_idc;                 /* 66/77/100 ...                */
    unsigned int level_idc;                   /* e.g. 51 for level 5.1        */
    unsigned int log2_max_frame_num;          /* e.g. 4                       */
    unsigned int pic_order_cnt_type;          /* 0 (type 0 supported)         */
    unsigned int log2_max_pic_order_cnt_lsb;  /* e.g. 4                       */
    unsigned int num_ref_frames;
    unsigned int gaps_in_frame_num_allowed;
    unsigned int pic_width_in_mbs;            /* e.g. 144                     */
    unsigned int pic_height_in_map_units;     /* e.g. 81                      */
    unsigned int frame_mbs_only_flag;         /* 1 for progressive            */
    unsigned int direct_8x8_inference_flag;
    unsigned int crop_right_units;            /* 0 unless cropping            */
    unsigned int crop_bottom_units;           /* 0 unless cropping            */
    unsigned int crop_left_units;             /* 0 unless a centred window    */
    unsigned int crop_top_units;              /* 0 unless a centred window    */
    unsigned int vui_flag;                    /* 0 = no VUI                   */
    unsigned int vui_video_signal;            /* used only if vui_flag = 1    */
} freecodec_h264_seq_cfg;

/* Picture-parameter-set inputs. */
typedef struct freecodec_h264_pic_cfg {
    unsigned int profile_idc;
    unsigned int entropy_coding_mode_flag;    /* 0 = CAVLC                   */
    unsigned int pic_init_qp_minus26;
    unsigned int chroma_qp_index_offset;
    unsigned int constrained_intra_pred_flag;
    unsigned int transform_8x8_mode_flag;     /* profile 100 only            */
    unsigned int num_ref_idx_l0_default_active; /* count, e.g. 1             */
    unsigned int num_ref_idx_l1_default_active; /* count, e.g. 1             */
    unsigned int deblocking_filter_control_present;
} freecodec_h264_pic_cfg;

/* Write the SPS (start code + NAL header 0x67 + RBSP). Returns bytes written,
 * or negative if `cap` is too small. */
int freecodec_h264_build_sps(const freecodec_h264_seq_cfg *cfg,
                             unsigned char *out, int cap);

/* Write the PPS (start code + NAL header 0x68 + RBSP). Returns bytes written,
 * or negative if `cap` is too small. */
int freecodec_h264_build_pps(const freecodec_h264_pic_cfg *cfg,
                             unsigned char *out, int cap);

/* Inputs for an IDR (intra) slice header. Only `pic_order_cnt_type == 0` and
 * a progressive frame are supported. */
typedef struct freecodec_h264_idr_slice_cfg {
    unsigned int log2_max_frame_num;          /* e.g. 4                       */
    unsigned int pic_order_cnt_type;          /* 0                            */
    unsigned int log2_max_pic_order_cnt_lsb;  /* e.g. 4                       */
    unsigned int pic_parameter_set_id;        /* 0                            */
    unsigned int entropy_coding_mode_flag;    /* 0 = CAVLC, 1 = CABAC         */
    unsigned int frame_num;
    unsigned int idr_pic_id;
    unsigned int pic_order_cnt_lsb;
    unsigned int slice_qp;                    /* absolute QP                  */
    unsigned int pic_init_qp;                 /* from the PPS                 */
    unsigned int disable_deblocking_filter_idc;
    int          alpha_offset_div2;
    int          beta_offset_div2;
} freecodec_h264_idr_slice_cfg;

/* Write an IDR I-slice header (start code + NAL header 0x65 + RBSP), zero-padded; returns
 * bytes or negative. `out_bits` gets the exact bit length L (spec/h264-slice-header.md 3a). */
int freecodec_h264_build_idr_slice(const freecodec_h264_idr_slice_cfg *cfg,
                                   unsigned char *out, int cap,
                                   int *out_bits);

/* Inputs for a P-slice header (standard, single reference, no reordering). */
typedef struct freecodec_h264_p_slice_cfg {
    unsigned int log2_max_frame_num;
    unsigned int pic_order_cnt_type;          /* 0                            */
    unsigned int log2_max_pic_order_cnt_lsb;
    unsigned int pic_parameter_set_id;
    unsigned int entropy_coding_mode_flag;    /* 0 = CAVLC, 1 = CABAC         */
    unsigned int cabac_init_idc;              /* CABAC context model (0..2)   */
    unsigned int frame_num;
    unsigned int pic_order_cnt_lsb;
    unsigned int nal_reference_idc;           /* 0..3                         */
    unsigned int num_ref_idx_l0_active;       /* count                        */
    unsigned int pps_num_ref_idx_l0_default;  /* count from the PPS           */
    unsigned int slice_qp;                    /* absolute QP                  */
    unsigned int pic_init_qp;                 /* from the PPS                 */
    unsigned int disable_deblocking_filter_idc;
    int          alpha_offset_div2;
    int          beta_offset_div2;
} freecodec_h264_p_slice_cfg;

/* Write a P-slice header (start code + NAL header + RBSP), zero-padded; returns bytes or
 * negative. `out_bits` gets the exact bit length L fed to the bit-writer (§3a). */
int freecodec_h264_build_p_slice(const freecodec_h264_p_slice_cfg *cfg,
                                 unsigned char *out, int cap,
                                 int *out_bits);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H264_HEADERS_H */
