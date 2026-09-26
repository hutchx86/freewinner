/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* H.264 GOP picture-ring scripting interface (spec/h264-gop.md), golden-
 * verified. Tracks in-use slots and the last/current/rec/lt/st references. */

#ifndef FREECODEC_H264_GOP_H
#define FREECODEC_H264_GOP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct freecodec_h264_frame_ring {
    unsigned int is_busy[5];       /* +0x00, one word per slot */
    unsigned int last_frm_idx;     /* +0x14 */
    unsigned int curr_frm_idx;     /* +0x18 */
    unsigned int last_frm_idx_valid;/* +0x1c */
    unsigned int lt_ref_frm_idx;   /* +0x20 */
    unsigned int st_ref_frm_idx;   /* +0x24 */
    unsigned int rec_frm_idx;      /* +0x28 */
} freecodec_h264_frame_ring;

typedef struct freecodec_h264_gop_cfg {
    unsigned int frame_count;
    unsigned int insert_count;
    unsigned int max_key_itl;
    unsigned int alloc_frames;
    unsigned int sub_alloc_frames;
    unsigned int ic_version;
    unsigned int skip_frame;
    unsigned int virtual_period;
    unsigned int first_intra_frame;
    unsigned int b_advanced_ref_en;
} freecodec_h264_gop_cfg;

/* Update the normal-P ring (mediad default). cfg is in/out: on the tail with
 * max_key_itl != 0 and frame_count >= max_key_itl, frame_count/insert_count reset. */
void freecodec_h264_gop_normal_p(freecodec_h264_frame_ring *main_ring,
                                 freecodec_h264_frame_ring *sub_ring,
                                 freecodec_h264_gop_cfg *cfg);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_H264_GOP_H */
