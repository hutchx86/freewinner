// SPDX-License-Identifier: AGPL-3.0-only
#ifndef FWM_MEDIA_ABI_H
#define FWM_MEDIA_ABI_H
#include "fwm_media_enum.h"
#include <stdint.h>
#include <stdbool.h>
#include <linux/videodev2.h>
typedef struct fwm_region_attr_union {
    uint8_t rsv_0[16];
} __attribute__((aligned(4))) fwm_region_attr_union_t;
typedef struct fwm_region_chn_attr_union {
    uint8_t rsv_0[56];
} __attribute__((aligned(4))) fwm_region_chn_attr_union_t;
typedef struct fwm_venc_advanced_ref_param {
    uint8_t rsv_0[16];
} __attribute__((aligned(4))) fwm_venc_advanced_ref_param_t;
typedef struct fwm_bitmap fwm_bitmap_t;
typedef struct fwm_frame_info fwm_frame_info_t;
typedef struct fwm_chn fwm_chn_t;
typedef struct fwm_sys_config fwm_sys_config_t;
typedef struct fwm_region_attr fwm_region_attr_t;
typedef struct fwm_region_chn_attr fwm_region_chn_attr_t;
typedef struct fwm_venc_pack_info fwm_venc_pack_info_t;
typedef struct fwm_venc_pack fwm_venc_pack_t;
typedef struct fwm_video_frame_info fwm_video_frame_info_t;
typedef struct fwm_video_frame fwm_video_frame_t;
typedef struct fwm_vi_attr fwm_vi_attr_t;
typedef struct fwm_vi_shutter_cfg fwm_vi_shutter_cfg_t;
typedef struct fwm_venc_base_config fwm_venc_base_config_t;
typedef struct fwm_venc_bitrate_range fwm_venc_bitrate_range_t;
typedef struct fwm_venc_gop_param fwm_venc_gop_param_t;
typedef struct fwm_venc_h264_param fwm_venc_h264_param_t;
typedef struct fwm_venc_h264_profile_level fwm_venc_h264_profile_level_t;
typedef struct fwm_venc_header_data fwm_venc_header_data_t;
typedef struct fwm_venc_input_buffer fwm_venc_input_buffer_t;
typedef struct fwm_venc_output_buffer fwm_venc_output_buffer_t;
typedef struct fwm_venc_overlay_header fwm_venc_overlay_header_t;
typedef struct fwm_venc_overlay_cover_yuv fwm_venc_overlay_cover_yuv_t;
typedef struct fwm_venc_overlay_info fwm_venc_overlay_info_t;
typedef struct fwm_venc_qp_range fwm_venc_qp_range_t;
typedef struct fwm_venc_roi fwm_venc_roi_t;
typedef struct fwm_venc_rc_param fwm_venc_rc_param_t;
typedef struct fwm_venc_rect fwm_venc_rect_t;
typedef struct fwm_venc_vbr_param fwm_venc_vbr_param_t;
typedef union fwm_venc_data_type fwm_venc_data_type_t;
struct fwm_bitmap {
    uint8_t pixel_format;
    uint8_t rsv_1[3];
    uint32_t width;
    uint32_t height;
    void *data;
};
struct fwm_frame_info {
    int32_t curr_qp;
    int32_t qp;
    int32_t gop_index;
    int32_t frame_index;
    int32_t total_index;
};
struct fwm_chn {
    int32_t mod_id;
    int32_t dev_id;
    int32_t chn_id;
};
struct fwm_sys_config {
    uint32_t align_width;
    char mkfc_tmp_dir[256];
};
struct fwm_region_attr {
    uint8_t type;
    uint8_t rsv_1[3];
    fwm_region_attr_union_t attr;
};
struct fwm_region_chn_attr {
    int32_t show_en;
    uint8_t type;
    uint8_t rsv_5[3];
    fwm_region_chn_attr_union_t chn_attr;
};
struct fwm_venc_pack_info {
    uint32_t size0;
    uint32_t size1;
    uint32_t size2;
};
struct fwm_venc_pack {
    uint8_t *addr0;
    uint8_t *addr1;
    uint8_t *addr2;
    uint32_t len0;
    uint32_t len1;
    uint32_t len2;
    uint64_t pts;
    uint32_t mb_frame_end;
    uint32_t data_type;
    uint32_t offset;
    uint32_t data_count;
    uint32_t pack_info[24];
};
struct fwm_video_frame_info {
    uint32_t v_frame[36];
    uint32_t id;
    uint8_t rsv_94[4];
};
struct fwm_video_frame {
    uint32_t width;
    uint32_t height;
    uint32_t field;
    uint32_t pixel_format;
    uint32_t video_format;
    uint32_t compress_mode;
    uint32_t phy_addr[3];
    void *vir_addr[3];
    uint32_t stride[3];
    uint32_t header_phy_addr[3];
    void *header_vir_addr[3];
    uint32_t header_stride[3];
    int16_t offset_top;
    int16_t offset_bottom;
    int16_t offset_left;
    int16_t offset_right;
    uint64_t mpts;
    uint32_t exposure_time;
    uint32_t framecnt;
    int32_t env_level;
    uint32_t who_set_flag;
    uint64_t flag_pts;
    uint32_t frame_flag;
    uint8_t rsv_8c[4];
};
struct fwm_vi_attr {
    uint32_t type;
    uint32_t memtype;
    struct v4l2_pix_format_mplane format;
    uint32_t nbufs;
    uint32_t nplanes;
    uint32_t fps;
    uint32_t capturemode;
    uint32_t use_current_window;
    uint32_t wdr_mode;
    uint32_t drop_frame_count;
};
struct fwm_vi_shutter_cfg {
    int32_t time;
    int32_t exposure_value;
    int32_t gain_value;
    uint32_t reset_mode;
    uint32_t shutter_mode;
};
struct fwm_venc_base_config {
    uint8_t enc_h264_nalu_en;
    uint8_t rsv_1[3];
    uint32_t input_width;
    uint32_t input_height;
    uint32_t dst_width;
    uint32_t dst_height;
    uint32_t stride;
    uint32_t input_format;
    void *memops;
    void *ve_ops_s;
    void *ve_ops_self;
    uint8_t only_wb_flag_en;
    uint8_t lbc_lossy_com_en_flag2x_en;
    uint8_t lbc_lossy_com_en_flag2_5x_en;
    uint8_t is_vbv_no_cache_en;
};
struct fwm_venc_bitrate_range {
    uint32_t bit_rate_max;
    uint32_t bit_rate_min;
};
struct fwm_venc_gop_param {
    uint8_t use_gop_ctrl_en_en;
    uint8_t rsv_1[3];
    uint32_t gop_mode;
    int32_t virtual_i_frame_interval;
    int32_t sp_interval;
    fwm_venc_advanced_ref_param_t ref_param;
};
struct fwm_venc_h264_profile_level {
    uint32_t profile;
    uint32_t level;
};
struct fwm_venc_qp_range {
    int32_t maxqp;
    int32_t minqp;
};
struct fwm_venc_rc_param {
    uint32_t rc_mode;
    uint32_t gap0[13];
    uint32_t vbr_param[3];
    uint32_t fix_qp[3];
    uint32_t qp_map[2];
    uint32_t gap1[10];
};
struct fwm_venc_h264_param {
    fwm_venc_h264_profile_level_t profile_level;
    int32_t entropy_coding_cabac_en;
    fwm_venc_qp_range_t qp_range;
    int32_t framerate;
    int32_t source_framerate;
    int32_t bitrate;
    int32_t max_key_interval;
    uint32_t coding_mode;
    fwm_venc_gop_param_t gop_param;
    fwm_venc_rc_param_t rc_param;
};
struct fwm_venc_header_data {
    uint8_t *buffer;
    uint32_t length;
};
struct fwm_venc_input_buffer {
    uint32_t id;
    uint8_t rsv_4[4];
    uint64_t pts;
    uint32_t flag;
    uint8_t *addr_phy_y;
    uint8_t *addr_phy_c;
    uint8_t *addr_vir_y;
    uint8_t *addr_vir_c;
    int32_t enable_corp_en;
    uint32_t crop_info[4];
    int32_t isp_pic_var;
    int32_t isp_pic_var_chroma;
    int32_t use_input_buffer_roi_en;
    uint32_t roi_param[64];
    int32_t alloc_mem_self_en;
    int32_t share_buf_fd;
    uint8_t use_csi_color_format_en;
    uint8_t rsv_14d[3];
    int32_t csi_color_format;
    int32_t env_level;
};
struct fwm_venc_output_buffer {
    int32_t id;
    uint8_t rsv_4[4];
    uint64_t pts;
    uint32_t flag;
    uint32_t size0;
    uint32_t size1;
    uint8_t *data0;
    uint8_t *data1;
    uint32_t frame_info[5];
    uint32_t size2;
    uint8_t *data2;
};
struct fwm_venc_overlay_cover_yuv {
    uint8_t reserved[4];
};
struct fwm_venc_overlay_header {
    uint16_t start_mb_x;
    uint16_t end_mb_x;
    uint16_t start_mb_y;
    uint16_t end_mb_y;
    uint8_t extra_alpha_flag;
    uint8_t extra_alpha;
    fwm_venc_overlay_cover_yuv_t cover_yuv;
    uint8_t rsv_e[2];
    uint32_t overlay_type;
    uint8_t *overlay_blk_addr;
    uint32_t bitmap_size;
    uint32_t bforce_reverse_flag;
    uint32_t reverse_unit_mb_w_minus1;
    uint32_t reverse_unit_mb_h_minus1;
};
struct fwm_venc_overlay_info {
    uint8_t blk_count;
    uint8_t rsv_1[3];
    uint32_t argb_type;
    uint32_t overlay_header_list[640];
    uint32_t invert_mode;
    uint32_t invert_threshold;
};
struct fwm_venc_roi {
    int32_t enable_en;
    int32_t index;
    int32_t q_poffset;
    uint8_t roi_abs_flag;
    uint8_t rsv_d[3];
    uint32_t rect[4];
};
struct fwm_venc_rect {
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;
};
struct fwm_venc_vbr_param {
    uint32_t max_bit_rate;
    int32_t moving_threshold;
    int32_t quality;
};
union fwm_venc_data_type {
    uint32_t h264_e_type;
    uint32_t jpege_type;
    uint32_t mpeg4_e_type;
    uint32_t h265_e_type;
    uint32_t val;
    uint8_t rsv_0[4];
};
#endif
