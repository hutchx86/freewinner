/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Spec r2/05 part B records, value sets and device table, shared by
 * vencoder.h, src/fenc/fenc_abi.h and mw_headers/vencoder.h. Layouts are
 * 32-bit ARM EABI facts asserted at the end under __arm__; C identifiers are
 * local, link symbols are declared by the header that owns them. */

#ifndef FREECODEC_VENC_TYPES_H
#define FREECODEC_VENC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================== value sets */

/* H.264 profile_idc (H.264 A.2). */
typedef enum fwm_venc_h264_profile_e {
    FWM_VENC_H264_PROFILE_BASELINE = 66,
    FWM_VENC_H264_PROFILE_MAIN     = 77,
    FWM_VENC_H264_PROFILE_HIGH     = 100
} fwm_venc_h264_profile_e;

/* H.264 level_idc: the level number times ten (H.264 A.3). */
typedef enum fwm_venc_h264_level_e {
    FWM_VENC_H264_LEVEL32 = 32,
    FWM_VENC_H264_LEVEL51 = 51
} fwm_venc_h264_level_e;

/* Picture structure of the coded stream. */
typedef enum fwm_venc_coding_mode_e {
    FWM_VENC_CODING_FRAME = 0,
    FWM_VENC_CODING_FIELD = 1,
    FWM_VENC_CODING_MB16  = 2
} fwm_venc_coding_mode_e;

/* Input picture layouts. */
typedef enum fwm_venc_pixel_format_e {
    FWM_VENC_PIXEL_YUV420SP = 0,    /* Y plane + interleaved UV (NV12)            */
    FWM_VENC_PIXEL_YVU420SP = 1,    /* Y plane + interleaved VU (NV21)            */
    FWM_VENC_PIXEL_LBC      = 19    /* engine-specific frame-compressed input     */
} fwm_venc_pixel_format_e;

typedef enum fwm_venc_rc_mode_e {
    FWM_VENC_RC_CBR  = 0,           /* constant bit rate                          */
    FWM_VENC_RC_VBR  = 1,           /* variable bit rate                          */
    FWM_VENC_RC_AVBR = 2            /* average variable bit rate                  */
} fwm_venc_rc_mode_e;

typedef enum fwm_venc_gop_mode_e {
    FWM_VENC_GOP_NORMAL_P = 1       /* I followed by a plain chain of P pictures  */
} fwm_venc_gop_mode_e;

/* H.265 general_profile_idc; only Main is produced by this encoder (spec H265
 * 13 section 4). */
typedef enum fwm_venc_h265_profile_e {
    FWM_VENC_H265_PROFILE_MAIN = 1
} fwm_venc_h265_profile_e;

/* H.265 rate-control selection carried in VencH265Param.sRcParam.eRcMode
 * (spec H265 10 section 1). */
typedef enum fwm_venc_h265_rc_mode_e {
    FWM_VENC_H265_RC_CBR   = 0,     /* classify engine, CBR tables             */
    FWM_VENC_H265_RC_VBR   = 1,     /* as CBR plus img-bin output from f1      */
    FWM_VENC_H265_RC_ABR   = 2,     /* MB-level RC, zeroed MAD tables          */
    FWM_VENC_H265_RC_AVBR  = 3,     /* ABR family                                */
    FWM_VENC_H265_RC_FIXQP = 4      /* classify engine off, QP anchored        */
} fwm_venc_h265_rc_mode_e;

/* H.265 GOP control modes (spec H265 10 section 1); only AW_NORMALP is used. */
typedef enum fwm_venc_h265_gop_mode_e {
    FWM_VENC_H265_GOP_NORMAL_P = 1  /* single-reference P chain                 */
} fwm_venc_h265_gop_mode_e;

typedef enum fwm_venc_overlay_argb_type_e {
    FWM_VENC_OVERLAY_ARGB1555 = 2
} fwm_venc_overlay_argb_type_e;

typedef enum fwm_venc_overlay_type_e {
    FWM_VENC_OVERLAY_NORMAL       = 0,
    FWM_VENC_OVERLAY_LUMA_REVERSE = 2   /* normal bitmap + per-frame luma inversion */
} fwm_venc_overlay_type_e;

/* Codec requested from the framework at create time. */
typedef enum fwm_venc_codec_e {
    FWM_VENC_CODEC_H264      = 0,
    FWM_VENC_CODEC_JPEG      = 1,
    FWM_VENC_CODEC_H264_VER2 = 2,   /* H.264 on the version-2 engine class        */
    FWM_VENC_CODEC_H265      = 3,
    FWM_VENC_CODEC_VP8       = 4
} fwm_venc_codec_e;

/* Index argument of the get/set-parameter calls. The device and the framework
 * share one set; a value the consumer never uses is forwarded unchanged. */
typedef enum fwm_venc_param_e {
    FWM_VENC_PARAM_BITRATE                = 0x000,
    FWM_VENC_PARAM_FRAME_RATE             = 0x001,
    FWM_VENC_PARAM_MAX_KEY_INTERVAL       = 0x002,
    FWM_VENC_PARAM_I_FILTER               = 0x003,
    FWM_VENC_PARAM_ROTATION               = 0x004,
    FWM_VENC_PARAM_SLICE_HEIGHT           = 0x005,
    FWM_VENC_PARAM_FORCE_KEY_FRAME        = 0x006,
    FWM_VENC_PARAM_ROI_CONFIG             = 0x00b,
    FWM_VENC_PARAM_STRIDE                 = 0x00c,
    FWM_VENC_PARAM_COLOUR_FORMAT          = 0x00d,
    FWM_VENC_PARAM_SIZE                   = 0x00e,
    FWM_VENC_PARAM_BITSTREAM_SIZE         = 0x00f,
    FWM_VENC_PARAM_BITSTREAM_STATUS       = 0x010,
    FWM_VENC_PARAM_P_SKIP                 = 0x012,
    FWM_VENC_PARAM_HORIZONTAL_FLIP        = 0x015,
    FWM_VENC_PARAM_H264_CONFIG            = 0x100,
    FWM_VENC_PARAM_H264_SPS_PPS           = 0x101,
    FWM_VENC_PARAM_H264_QP_RANGE          = 0x102,
    FWM_VENC_PARAM_H264_PROFILE_LEVEL     = 0x103,
    FWM_VENC_PARAM_H264_CABAC             = 0x104,
    FWM_VENC_PARAM_H264_FIXED_QP          = 0x106,
    FWM_VENC_PARAM_H264_TEMPORAL_SKIP     = 0x107,
    FWM_VENC_PARAM_FAST_ENCODE            = 0x109,
    FWM_VENC_PARAM_CHROMA_GRAY            = 0x10c,
    FWM_VENC_PARAM_I_QP_OFFSET            = 0x10d,
    FWM_VENC_PARAM_FRAME_LENGTH_THRESHOLD = 0x205,
    FWM_VENC_PARAM_BITRATE_RANGE          = 0x207,
    FWM_VENC_PARAM_H265_CONFIG            = 0x300,
    FWM_VENC_PARAM_H265_GOP               = 0x301,
    FWM_VENC_PARAM_H265_TOTAL_FRAMES      = 0x302,
    FWM_VENC_PARAM_H265_UPDATE_LT_REF     = 0x303,
    FWM_VENC_PARAM_H265_HEADER            = 0x304,
    FWM_VENC_PARAM_H265_TENDENCY          = 0x305,
    FWM_VENC_PARAM_H265_TRANSFORM         = 0x306,
    FWM_VENC_PARAM_H265_SAO               = 0x307,
    FWM_VENC_PARAM_H265_DEBLOCK           = 0x308,
    FWM_VENC_PARAM_H265_TIMING            = 0x309,
    FWM_VENC_PARAM_H265_INTRA_PERIOD      = 0x30a,
    FWM_VENC_PARAM_H265_MB_MODE_CTRL      = 0x30b,
    FWM_VENC_PARAM_H265_MB_INFO_OUTPUT    = 0x30d,
    FWM_VENC_PARAM_H265_ENC_TIME          = 0x40d,
    FWM_VENC_PARAM_ALTER_FRAME            = 0x400,
    FWM_VENC_PARAM_CHANNEL                = 0x402,
    FWM_VENC_PARAM_OVERLAY                = 0x404,
    FWM_VENC_PARAM_FILTER_3D              = 0x40e,
    FWM_VENC_PARAM_NULL_FRAME             = 0x500,
    FWM_VENC_PARAM_CBR_FILLING            = 0x505,
    FWM_VENC_PARAM_ROI                    = 0x506
} fwm_venc_param_e;

/* Result codes of the device, the framework and the support library. */
#define FWM_VENC_RESULT_ERROR             (-1)
#define FWM_VENC_RESULT_OK                0
#define FWM_VENC_RESULT_NO_FRAME_BUFFER   1    /* no input picture queued         */
#define FWM_VENC_RESULT_BITSTREAM_IS_FULL 2    /* bitstream buffer full           */
#define FWM_VENC_RESULT_ILLEGAL_PARAM     3
#define FWM_VENC_RESULT_NOT_SUPPORT       4
#define FWM_VENC_RESULT_BITSTREAM_IS_EMPTY 5   /* no finished frame waiting       */
#define FWM_VENC_RESULT_NO_MEMORY         6
#define FWM_VENC_RESULT_NO_RESOURCE       7
#define FWM_VENC_RESULT_NULL_PTR          8
#define FWM_VENC_RESULT_DROP_FRAME        9    /* frame dropped                   */
#define FWM_VENC_RESULT_EFUSE_ERROR       25   /* e-fuse check failed             */

/* Output-frame flags. */
#define FWM_VENC_FRAME_KEYFRAME 0x1

/* P-skip factors accepted by FWM_VENC_PARAM_P_SKIP. */
#define FWM_VENC_PSKIP_4 4
#define FWM_VENC_PSKIP_8 8

/* Overlay (OSD) regions per picture. */
#define FWM_VENC_OVERLAY_MAX_REGIONS 64

/* =============================================================== records */

/* Rectangle in pixels (16 bytes). */
typedef struct fwm_venc_rect {
    int left;
    int top;
    int width;
    int height;
} fwm_venc_rect_t;

/* One region of interest (32 bytes). */
typedef struct fwm_venc_roi {
    int             enable;
    int             index;          /* region slot, 0..7                         */
    int             qp_offset;      /* QP delta, or absolute QP (flag below)     */
    unsigned char   qp_absolute_en; /* non-zero: qp_offset is an absolute QP     */
    fwm_venc_rect_t rect;
} fwm_venc_roi_t;

/* Profile and level (8 bytes). */
typedef struct fwm_venc_h264_profile_level {
    fwm_venc_h264_profile_e profile;
    fwm_venc_h264_level_e   level;
} fwm_venc_h264_profile_level_t;

/* Slice-QP bounds (8 bytes). */
typedef struct fwm_venc_qp_range {
    int qp_max;
    int qp_min;
} fwm_venc_qp_range_t;

/* Reference options of the GOP block (16 bytes; only the first word is read). */
typedef struct fwm_venc_ref_options {
    int          advanced_ref_en;
    unsigned int _reserved[3];
} fwm_venc_ref_options_t;

/* GOP structure (32 bytes). */
typedef struct fwm_venc_gop {
    unsigned char          gop_control_en;
    fwm_venc_gop_mode_e    gop_mode;
    int                    virtual_i_interval;
    int                    sp_interval;
    fwm_venc_ref_options_t ref_options;
} fwm_venc_gop_t;

/* Variable-bit-rate options (12 bytes). */
typedef struct fwm_venc_vbr {
    unsigned int max_bitrate;
    int          motion_threshold;
    int          quality;
} fwm_venc_vbr_t;

/* Fixed-QP mode (12 bytes). */
typedef struct fwm_venc_fixed_qp {
    int enable;
    int i_qp;
    int p_qp;
} fwm_venc_fixed_qp_t;

/* Per-macroblock QP map (8 bytes on the target). */
typedef struct fwm_venc_qp_map {
    unsigned int mode_ctrl_en;
    void        *info;
} fwm_venc_qp_map_t;

/* Rate-control block (128 bytes); the unnamed words are not read. */
typedef struct fwm_venc_rate_control {
    fwm_venc_rc_mode_e  mode;
    unsigned int        _gap0[13];
    fwm_venc_vbr_t      vbr;
    fwm_venc_fixed_qp_t fixed_qp;
    fwm_venc_qp_map_t   qp_map;
    unsigned int        _gap1[10];
} fwm_venc_rate_control_t;

/* H.264 stream parameters (200 bytes). */
typedef struct fwm_venc_h264_config {
    fwm_venc_h264_profile_level_t profile_level;
    int                           cabac_en;
    fwm_venc_qp_range_t           qp_range;
    int                           frame_rate;
    int                           source_frame_rate;
    int                           bitrate;
    int                           key_interval_max;
    int                           coding_mode;
    fwm_venc_gop_t                gop;
    fwm_venc_rate_control_t       rate_control;
} fwm_venc_h264_config_t;

/* Bit-rate bounds for the rate controller (8 bytes). */
typedef struct fwm_venc_bitrate_range {
    unsigned int bitrate_max;
    unsigned int bitrate_min;
} fwm_venc_bitrate_range_t;

/* Device configuration handed to init (44 bytes). The framework fills in the
 * three engine/memory pointer fields. */
typedef struct fwm_venc_base_config {
    unsigned char   nalu_en;             /* emit NAL units with start codes    */
    unsigned int    input_width;         /* input picture, px                  */
    unsigned int    input_height;
    unsigned int    output_width;        /* coded picture, px; 0 = input size  */
    unsigned int    output_height;
    unsigned int    input_stride;        /* input line stride, px; 0 = width   */
    fwm_venc_pixel_format_e input_format;
    void           *memops;              /* support-library memory table       */
    void           *engine_ops;          /* engine operations table            */
    void           *engine;              /* engine instance                    */
    unsigned char   write_back_only_en;
    unsigned char   lbc_lossy_2x_en;
    unsigned char   lbc_lossy_2_5x_en;
    unsigned char   bitstream_uncached_en;
} fwm_venc_base_config_t;

/* One input picture (344 bytes). The support library uses only id, the four
 * plane addresses and pool_owned_en; everything else is carried opaquely. */
typedef struct fwm_venc_input_picture {
    unsigned long   id;
    long long       pts;                 /* presentation time stamp            */
    unsigned int    flags;
    unsigned char  *luma_phys;           /* luma, physical                     */
    unsigned char  *chroma_phys;         /* chroma, physical                   */
    unsigned char  *luma_virt;           /* luma, CPU mapping                  */
    unsigned char  *chroma_virt;         /* chroma, CPU mapping                */
    int             crop_en;
    fwm_venc_rect_t crop;
    int             isp_variance;        /* picture variance from the ISP      */
    int             isp_variance_chroma;
    int             roi_en;              /* per-picture ROI                    */
    fwm_venc_roi_t  rois[8];
    int             pool_owned_en;       /* allocated by the support library   */
    int             dmabuf_fd;           /* shared dma-buf fd                  */
    unsigned char   csi_colour_format_en;
    int             csi_colour_format;
    int             light_value;         /* scene light value                  */
} fwm_venc_input_picture_t;

/* Per-frame coding statistics (20 bytes). */
typedef struct fwm_venc_frame_stats {
    int qp;
    int qp_average;
    int gop_index;
    int frame_index;
    int total_index;
} fwm_venc_frame_stats_t;

/* One finished frame (64 bytes). A frame that wraps around the end of the
 * bitstream ring comes in two parts; part 2 is an optional extra. */
typedef struct fwm_venc_output_frame {
    int                    id;
    long long              pts;
    unsigned int           flags;
    unsigned int           size0;
    unsigned int           size1;
    unsigned char         *data0;
    unsigned char         *data1;
    fwm_venc_frame_stats_t stats;
    unsigned int           size2;
    unsigned char         *data2;
} fwm_venc_output_frame_t;

/* A byte blob (8 bytes): the SPS + PPS headers. */
typedef struct fwm_venc_header_blob {
    unsigned char *data;
    unsigned int   length;
} fwm_venc_header_blob_t;

/* Four bytes of cover colour, 2-byte aligned. */
typedef union fwm_venc_cover_colour {
    unsigned short _reserved[2];
} fwm_venc_cover_colour_t;

/* One overlay region (40 bytes); coordinates in macroblocks. */
typedef struct fwm_venc_overlay_region {
    unsigned short          start_mb_x;
    unsigned short          end_mb_x;
    unsigned short          start_mb_y;
    unsigned short          end_mb_y;
    unsigned char           extra_alpha_en;
    unsigned char           extra_alpha;
    fwm_venc_cover_colour_t cover_colour;
    fwm_venc_overlay_type_e overlay_type;
    unsigned char          *bitmap;
    unsigned int            bitmap_size;
    unsigned int            force_reverse_en;
    unsigned int            reverse_unit_mb_w_minus1;   /* invert unit, MB - 1  */
    unsigned int            reverse_unit_mb_h_minus1;
} fwm_venc_overlay_region_t;

/* Overlay configuration (2576 bytes). */
typedef struct fwm_venc_overlay {
    unsigned char                region_count;
    fwm_venc_overlay_argb_type_e argb_type;
    fwm_venc_overlay_region_t    regions[FWM_VENC_OVERLAY_MAX_REGIONS];
    unsigned int                 invert_mode;
    unsigned int                 invert_threshold;
} fwm_venc_overlay_t;

/* Picture size (8 bytes). */
typedef struct fwm_venc_size {
    int width;
    int height;
} fwm_venc_size_t;

/* Bitstream buffer status (16 bytes). */
typedef struct fwm_venc_bitstream_status {
    unsigned int vbv_size;
    unsigned int coded_frame_num;
    unsigned int coded_size;
    unsigned int max_frame_length;
} fwm_venc_bitstream_status_t;

/* Temporal layering / frame skipping (28 bytes). */
typedef struct fwm_venc_temporal_skip {
    int          temporal_layers;
    int          skip_frames;
    int          layer_ratio_en;
    unsigned int layer_ratio[4];
} fwm_venc_temporal_skip_t;

/* Input-picture pool request (12 bytes). */
typedef struct fwm_venc_input_pool {
    unsigned int count;
    unsigned int luma_size;
    unsigned int chroma_size;
} fwm_venc_input_pool_t;

/* ===================================================== H.265 records (spec 12) */

/* Profile / level (8 bytes). level is the H.265 general_level_idc: 123 = 4.1. */
typedef struct fwm_venc_h265_profile_level {
    fwm_venc_h265_profile_e profile;
    int                     level;
} fwm_venc_h265_profile_level_t;

/* GOP control (16 bytes; spec H265 12 section 3.1 index 0x301, 10 section 1).
 * The vendor record is 0x1f90 bytes; only these fields are read. */
typedef struct fwm_venc_h265_gop {
    unsigned char            gop_control_en;
    fwm_venc_h265_gop_mode_e gop_mode;
    int                      gop_size;
    int                      total_frames_num;
} fwm_venc_h265_gop_t;

/* Tendency coefficients (12 bytes; spec H265 12 section 3.1 index 0x305). */
typedef struct fwm_venc_h265_tendency {
    unsigned int inter;
    unsigned int skip;
    unsigned int merge_sub;
} fwm_venc_h265_tendency_t;

/* Transform flags (8 bytes; index 0x306, spec H265 11 section 3.2 / 13 section 6). */
typedef struct fwm_venc_h265_transform {
    int transform_skip_en;   /* PPS transform_skip_enabled_flag                   */
    int transform_8x8_en;    /* command-block +0x08 bit 27                        */
} fwm_venc_h265_transform_t;

/* Sample-adaptive-offset flags (12 bytes; index 0x307, spec H265 13 section 8). */
typedef struct fwm_venc_h265_sao {
    int enabled;
    int slice_sao_luma;      /* slice_sao_luma_flag                               */
    int slice_sao_chroma;    /* slice_sao_chroma_flag                             */
} fwm_venc_h265_sao_t;

/* Deblocking controls (16 bytes; index 0x308, spec H265 11 section 3.1). */
typedef struct fwm_venc_h265_deblock {
    int deblock_idc;
    int tc_offset_div2;      /* command-block +0x04 bits [19:16]                  */
    int beta_offset_div2;    /* command-block +0x04 bits [23:20]                  */
    int _reserved;
} fwm_venc_h265_deblock_t;

/* Timing (16 bytes; index 0x309). */
typedef struct fwm_venc_h265_timing {
    unsigned int num_units_in_tick;
    unsigned int time_scale;
    int          num_ticks_poc_diff_one;
    unsigned int framerate;
} fwm_venc_h265_timing_t;

/* H.265 stream parameters (spec H265 12 section 3.3). The vendor block is
 * 0x18c+ bytes; these are the fields this device reads. */
typedef struct fwm_venc_h265_config {
    fwm_venc_h265_profile_level_t profile_level;
    fwm_venc_qp_range_t           qp_range;
    int                           frame_rate;
    int                           source_frame_rate;
    int                           bitrate;
    int                           idr_period;      /* handler forces 40        */
    int                           intra_period;    /* handler forces 40        */
    int                           gop_size;        /* handler forces 20        */
    int                           qp_init;
    fwm_venc_h265_rc_mode_e       rc_mode;
    int                           min_i_qp;
    fwm_venc_vbr_t                vbr;
    fwm_venc_fixed_qp_t           fixed_qp;
    fwm_venc_h265_gop_t           gop;
} fwm_venc_h265_config_t;

/* ========================================================== device table */

/* One encoder device: 12 slots (48 bytes on the target). Handles are opaque. */
typedef struct fwm_venc_device {
    const char *name;
    void *(*open)(fwm_venc_base_config_t *base_config, unsigned int ic_version);
    int   (*init)(void *handle, fwm_venc_base_config_t *base_config); /* allocates buffers */
    int   (*uninit)(void *handle);
    void  (*close)(void *handle);
    int   (*encode)(void *handle, fwm_venc_input_picture_t *in);      /* one picture */
    int   (*get_parameter)(void *handle, int index, void *param);
    int   (*set_parameter)(void *handle, int index, void *param);
    int   (*ready_frame_count)(void *handle);                         /* finished, waiting */
    int   (*get_frame)(void *handle, fwm_venc_output_frame_t *out);
    int   (*release_frame)(void *handle, fwm_venc_output_frame_t *out);
    int   (*reset_frames)(void *handle);                              /* discard all */
} fwm_venc_device_t;

/* Exported device tables (fixed link names, spec r2/05 B.1): the two H.264
 * entries describe the working device; the others fail every call. */
extern fwm_venc_device_t video_encoder_h264_ver1;
extern fwm_venc_device_t video_encoder_h264_ver2;
extern fwm_venc_device_t video_encoder_h265;
extern fwm_venc_device_t video_encoder_jpeg;

/* ========================================================= layout checks */

#if defined(__arm__)
_Static_assert(sizeof(fwm_venc_h264_profile_e) == 4, "fwm_venc_h264_profile_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_h264_level_e) == 4, "fwm_venc_h264_level_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_coding_mode_e) == 4, "fwm_venc_coding_mode_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_pixel_format_e) == 4, "fwm_venc_pixel_format_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_rc_mode_e) == 4, "fwm_venc_rc_mode_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_gop_mode_e) == 4, "fwm_venc_gop_mode_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_overlay_argb_type_e) == 4, "fwm_venc_overlay_argb_type_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_overlay_type_e) == 4, "fwm_venc_overlay_type_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_codec_e) == 4, "fwm_venc_codec_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_param_e) == 4, "fwm_venc_param_e is 4 bytes");

/* B.4.1 rectangle */
_Static_assert(sizeof(fwm_venc_rect_t) == 16, "rect is 16 bytes");
_Static_assert(offsetof(fwm_venc_rect_t, left) == 0, "rect.left");
_Static_assert(offsetof(fwm_venc_rect_t, top) == 4, "rect.top");
_Static_assert(offsetof(fwm_venc_rect_t, width) == 8, "rect.width");
_Static_assert(offsetof(fwm_venc_rect_t, height) == 12, "rect.height");

/* B.4.2 ROI entry */
_Static_assert(sizeof(fwm_venc_roi_t) == 32, "roi is 32 bytes");
_Static_assert(offsetof(fwm_venc_roi_t, enable) == 0, "roi.enable");
_Static_assert(offsetof(fwm_venc_roi_t, index) == 4, "roi.index");
_Static_assert(offsetof(fwm_venc_roi_t, qp_offset) == 8, "roi.qp_offset");
_Static_assert(offsetof(fwm_venc_roi_t, qp_absolute_en) == 12, "roi.qp_absolute_en");
_Static_assert(offsetof(fwm_venc_roi_t, rect) == 16, "roi.rect");

/* B.4.3 profile / level */
_Static_assert(sizeof(fwm_venc_h264_profile_level_t) == 8, "profile_level is 8 bytes");
_Static_assert(offsetof(fwm_venc_h264_profile_level_t, profile) == 0, "profile_level.profile");
_Static_assert(offsetof(fwm_venc_h264_profile_level_t, level) == 4, "profile_level.level");

/* B.4.4 QP range */
_Static_assert(sizeof(fwm_venc_qp_range_t) == 8, "qp_range is 8 bytes");
_Static_assert(offsetof(fwm_venc_qp_range_t, qp_max) == 0, "qp_range.qp_max");
_Static_assert(offsetof(fwm_venc_qp_range_t, qp_min) == 4, "qp_range.qp_min");

/* B.4.5 GOP block */
_Static_assert(sizeof(fwm_venc_ref_options_t) == 16, "ref_options is 16 bytes");
_Static_assert(offsetof(fwm_venc_ref_options_t, advanced_ref_en) == 0, "ref_options.advanced_ref_en");
_Static_assert(sizeof(fwm_venc_gop_t) == 32, "gop is 32 bytes");
_Static_assert(offsetof(fwm_venc_gop_t, gop_control_en) == 0, "gop.gop_control_en");
_Static_assert(offsetof(fwm_venc_gop_t, gop_mode) == 4, "gop.gop_mode");
_Static_assert(offsetof(fwm_venc_gop_t, virtual_i_interval) == 8, "gop.virtual_i_interval");
_Static_assert(offsetof(fwm_venc_gop_t, sp_interval) == 12, "gop.sp_interval");
_Static_assert(offsetof(fwm_venc_gop_t, ref_options) == 16, "gop.ref_options");

/* B.4.6 VBR / fixed-QP / QP-map blocks */
_Static_assert(sizeof(fwm_venc_vbr_t) == 12, "vbr is 12 bytes");
_Static_assert(offsetof(fwm_venc_vbr_t, max_bitrate) == 0, "vbr.max_bitrate");
_Static_assert(offsetof(fwm_venc_vbr_t, motion_threshold) == 4, "vbr.motion_threshold");
_Static_assert(offsetof(fwm_venc_vbr_t, quality) == 8, "vbr.quality");
_Static_assert(sizeof(fwm_venc_fixed_qp_t) == 12, "fixed_qp is 12 bytes");
_Static_assert(offsetof(fwm_venc_fixed_qp_t, enable) == 0, "fixed_qp.enable");
_Static_assert(offsetof(fwm_venc_fixed_qp_t, i_qp) == 4, "fixed_qp.i_qp");
_Static_assert(offsetof(fwm_venc_fixed_qp_t, p_qp) == 8, "fixed_qp.p_qp");
_Static_assert(sizeof(fwm_venc_qp_map_t) == 8, "qp_map is 8 bytes");
_Static_assert(offsetof(fwm_venc_qp_map_t, mode_ctrl_en) == 0, "qp_map.mode_ctrl_en");
_Static_assert(offsetof(fwm_venc_qp_map_t, info) == 4, "qp_map.info");

/* B.4.7 rate-control block */
_Static_assert(sizeof(fwm_venc_rate_control_t) == 128, "rate_control is 128 bytes");
_Static_assert(offsetof(fwm_venc_rate_control_t, mode) == 0, "rate_control.mode");
_Static_assert(offsetof(fwm_venc_rate_control_t, vbr) == 56, "rate_control.vbr");
_Static_assert(offsetof(fwm_venc_rate_control_t, fixed_qp) == 68, "rate_control.fixed_qp");
_Static_assert(offsetof(fwm_venc_rate_control_t, qp_map) == 80, "rate_control.qp_map");

/* B.4.8 H.264 parameter block */
_Static_assert(sizeof(fwm_venc_h264_config_t) == 200, "h264_config is 200 bytes");
_Static_assert(offsetof(fwm_venc_h264_config_t, profile_level) == 0, "h264_config.profile_level");
_Static_assert(offsetof(fwm_venc_h264_config_t, cabac_en) == 8, "h264_config.cabac_en");
_Static_assert(offsetof(fwm_venc_h264_config_t, qp_range) == 12, "h264_config.qp_range");
_Static_assert(offsetof(fwm_venc_h264_config_t, frame_rate) == 20, "h264_config.frame_rate");
_Static_assert(offsetof(fwm_venc_h264_config_t, source_frame_rate) == 24, "h264_config.source_frame_rate");
_Static_assert(offsetof(fwm_venc_h264_config_t, bitrate) == 28, "h264_config.bitrate");
_Static_assert(offsetof(fwm_venc_h264_config_t, key_interval_max) == 32, "h264_config.key_interval_max");
_Static_assert(offsetof(fwm_venc_h264_config_t, coding_mode) == 36, "h264_config.coding_mode");
_Static_assert(offsetof(fwm_venc_h264_config_t, gop) == 40, "h264_config.gop");
_Static_assert(offsetof(fwm_venc_h264_config_t, rate_control) == 72, "h264_config.rate_control");

/* bit-rate bounds */
_Static_assert(sizeof(fwm_venc_bitrate_range_t) == 8, "bitrate_range is 8 bytes");
_Static_assert(offsetof(fwm_venc_bitrate_range_t, bitrate_max) == 0, "bitrate_range.bitrate_max");
_Static_assert(offsetof(fwm_venc_bitrate_range_t, bitrate_min) == 4, "bitrate_range.bitrate_min");

/* B.4.9 base configuration */
_Static_assert(sizeof(fwm_venc_base_config_t) == 44, "base_config is 44 bytes");
_Static_assert(offsetof(fwm_venc_base_config_t, nalu_en) == 0, "base_config.nalu_en");
_Static_assert(offsetof(fwm_venc_base_config_t, input_width) == 4, "base_config.input_width");
_Static_assert(offsetof(fwm_venc_base_config_t, input_height) == 8, "base_config.input_height");
_Static_assert(offsetof(fwm_venc_base_config_t, output_width) == 12, "base_config.output_width");
_Static_assert(offsetof(fwm_venc_base_config_t, output_height) == 16, "base_config.output_height");
_Static_assert(offsetof(fwm_venc_base_config_t, input_stride) == 20, "base_config.input_stride");
_Static_assert(offsetof(fwm_venc_base_config_t, input_format) == 24, "base_config.input_format");
_Static_assert(offsetof(fwm_venc_base_config_t, memops) == 28, "base_config.memops");
_Static_assert(offsetof(fwm_venc_base_config_t, engine_ops) == 32, "base_config.engine_ops");
_Static_assert(offsetof(fwm_venc_base_config_t, engine) == 36, "base_config.engine");
_Static_assert(offsetof(fwm_venc_base_config_t, write_back_only_en) == 40, "base_config.write_back_only_en");
_Static_assert(offsetof(fwm_venc_base_config_t, lbc_lossy_2x_en) == 41, "base_config.lbc_lossy_2x_en");
_Static_assert(offsetof(fwm_venc_base_config_t, lbc_lossy_2_5x_en) == 42, "base_config.lbc_lossy_2_5x_en");
_Static_assert(offsetof(fwm_venc_base_config_t, bitstream_uncached_en) == 43, "base_config.bitstream_uncached_en");

/* B.4.10 input picture */
_Static_assert(sizeof(fwm_venc_input_picture_t) == 344, "input_picture is 344 bytes");
_Static_assert(offsetof(fwm_venc_input_picture_t, id) == 0, "input_picture.id");
_Static_assert(offsetof(fwm_venc_input_picture_t, pts) == 8, "input_picture.pts");
_Static_assert(offsetof(fwm_venc_input_picture_t, flags) == 16, "input_picture.flags");
_Static_assert(offsetof(fwm_venc_input_picture_t, luma_phys) == 20, "input_picture.luma_phys");
_Static_assert(offsetof(fwm_venc_input_picture_t, chroma_phys) == 24, "input_picture.chroma_phys");
_Static_assert(offsetof(fwm_venc_input_picture_t, luma_virt) == 28, "input_picture.luma_virt");
_Static_assert(offsetof(fwm_venc_input_picture_t, chroma_virt) == 32, "input_picture.chroma_virt");
_Static_assert(offsetof(fwm_venc_input_picture_t, crop_en) == 36, "input_picture.crop_en");
_Static_assert(offsetof(fwm_venc_input_picture_t, crop) == 40, "input_picture.crop");
_Static_assert(offsetof(fwm_venc_input_picture_t, isp_variance) == 56, "input_picture.isp_variance");
_Static_assert(offsetof(fwm_venc_input_picture_t, isp_variance_chroma) == 60, "input_picture.isp_variance_chroma");
_Static_assert(offsetof(fwm_venc_input_picture_t, roi_en) == 64, "input_picture.roi_en");
_Static_assert(offsetof(fwm_venc_input_picture_t, rois) == 68, "input_picture.rois");
_Static_assert(offsetof(fwm_venc_input_picture_t, pool_owned_en) == 324, "input_picture.pool_owned_en");
_Static_assert(offsetof(fwm_venc_input_picture_t, dmabuf_fd) == 328, "input_picture.dmabuf_fd");
_Static_assert(offsetof(fwm_venc_input_picture_t, csi_colour_format_en) == 332, "input_picture.csi_colour_format_en");
_Static_assert(offsetof(fwm_venc_input_picture_t, csi_colour_format) == 336, "input_picture.csi_colour_format");
_Static_assert(offsetof(fwm_venc_input_picture_t, light_value) == 340, "input_picture.light_value");

/* B.4.11 output frame and frame statistics */
_Static_assert(sizeof(fwm_venc_output_frame_t) == 64, "output_frame is 64 bytes");
_Static_assert(offsetof(fwm_venc_output_frame_t, id) == 0, "output_frame.id");
_Static_assert(offsetof(fwm_venc_output_frame_t, pts) == 8, "output_frame.pts");
_Static_assert(offsetof(fwm_venc_output_frame_t, flags) == 16, "output_frame.flags");
_Static_assert(offsetof(fwm_venc_output_frame_t, size0) == 20, "output_frame.size0");
_Static_assert(offsetof(fwm_venc_output_frame_t, size1) == 24, "output_frame.size1");
_Static_assert(offsetof(fwm_venc_output_frame_t, data0) == 28, "output_frame.data0");
_Static_assert(offsetof(fwm_venc_output_frame_t, data1) == 32, "output_frame.data1");
_Static_assert(offsetof(fwm_venc_output_frame_t, stats) == 36, "output_frame.stats");
_Static_assert(offsetof(fwm_venc_output_frame_t, size2) == 56, "output_frame.size2");
_Static_assert(offsetof(fwm_venc_output_frame_t, data2) == 60, "output_frame.data2");
_Static_assert(sizeof(fwm_venc_frame_stats_t) == 20, "frame_stats is 20 bytes");
_Static_assert(offsetof(fwm_venc_frame_stats_t, qp) == 0, "frame_stats.qp");
_Static_assert(offsetof(fwm_venc_frame_stats_t, qp_average) == 4, "frame_stats.qp_average");
_Static_assert(offsetof(fwm_venc_frame_stats_t, gop_index) == 8, "frame_stats.gop_index");
_Static_assert(offsetof(fwm_venc_frame_stats_t, frame_index) == 12, "frame_stats.frame_index");
_Static_assert(offsetof(fwm_venc_frame_stats_t, total_index) == 16, "frame_stats.total_index");

/* B.4.12 header blob */
_Static_assert(sizeof(fwm_venc_header_blob_t) == 8, "header_blob is 8 bytes");
_Static_assert(offsetof(fwm_venc_header_blob_t, data) == 0, "header_blob.data");
_Static_assert(offsetof(fwm_venc_header_blob_t, length) == 4, "header_blob.length");

/* B.4.13 overlay region and overlay block */
_Static_assert(sizeof(fwm_venc_cover_colour_t) == 4, "cover_colour is 4 bytes");
_Static_assert(sizeof(fwm_venc_overlay_region_t) == 40, "overlay_region is 40 bytes");
_Static_assert(offsetof(fwm_venc_overlay_region_t, start_mb_x) == 0, "overlay_region.start_mb_x");
_Static_assert(offsetof(fwm_venc_overlay_region_t, end_mb_x) == 2, "overlay_region.end_mb_x");
_Static_assert(offsetof(fwm_venc_overlay_region_t, start_mb_y) == 4, "overlay_region.start_mb_y");
_Static_assert(offsetof(fwm_venc_overlay_region_t, end_mb_y) == 6, "overlay_region.end_mb_y");
_Static_assert(offsetof(fwm_venc_overlay_region_t, extra_alpha_en) == 8, "overlay_region.extra_alpha_en");
_Static_assert(offsetof(fwm_venc_overlay_region_t, extra_alpha) == 9, "overlay_region.extra_alpha");
_Static_assert(offsetof(fwm_venc_overlay_region_t, cover_colour) == 10, "overlay_region.cover_colour");
_Static_assert(offsetof(fwm_venc_overlay_region_t, overlay_type) == 16, "overlay_region.overlay_type");
_Static_assert(offsetof(fwm_venc_overlay_region_t, bitmap) == 20, "overlay_region.bitmap");
_Static_assert(offsetof(fwm_venc_overlay_region_t, bitmap_size) == 24, "overlay_region.bitmap_size");
_Static_assert(offsetof(fwm_venc_overlay_region_t, force_reverse_en) == 28, "overlay_region.force_reverse_en");
_Static_assert(offsetof(fwm_venc_overlay_region_t, reverse_unit_mb_w_minus1) == 32, "overlay_region.reverse_unit_mb_w_minus1");
_Static_assert(offsetof(fwm_venc_overlay_region_t, reverse_unit_mb_h_minus1) == 36, "overlay_region.reverse_unit_mb_h_minus1");
_Static_assert(sizeof(fwm_venc_overlay_t) == 2576, "overlay is 2576 bytes");
_Static_assert(offsetof(fwm_venc_overlay_t, region_count) == 0, "overlay.region_count");
_Static_assert(offsetof(fwm_venc_overlay_t, argb_type) == 4, "overlay.argb_type");
_Static_assert(offsetof(fwm_venc_overlay_t, regions) == 8, "overlay.regions");
_Static_assert(offsetof(fwm_venc_overlay_t, invert_mode) == 2568, "overlay.invert_mode");
_Static_assert(offsetof(fwm_venc_overlay_t, invert_threshold) == 2572, "overlay.invert_threshold");

/* B.4.14 size, bitstream status, temporal-skip block */
_Static_assert(sizeof(fwm_venc_size_t) == 8, "size is 8 bytes");
_Static_assert(offsetof(fwm_venc_size_t, width) == 0, "size.width");
_Static_assert(offsetof(fwm_venc_size_t, height) == 4, "size.height");
_Static_assert(sizeof(fwm_venc_bitstream_status_t) == 16, "bitstream_status is 16 bytes");
_Static_assert(offsetof(fwm_venc_bitstream_status_t, vbv_size) == 0, "bitstream_status.vbv_size");
_Static_assert(offsetof(fwm_venc_bitstream_status_t, coded_frame_num) == 4, "bitstream_status.coded_frame_num");
_Static_assert(offsetof(fwm_venc_bitstream_status_t, coded_size) == 8, "bitstream_status.coded_size");
_Static_assert(offsetof(fwm_venc_bitstream_status_t, max_frame_length) == 12, "bitstream_status.max_frame_length");
_Static_assert(sizeof(fwm_venc_temporal_skip_t) == 28, "temporal_skip is 28 bytes");
_Static_assert(offsetof(fwm_venc_temporal_skip_t, temporal_layers) == 0, "temporal_skip.temporal_layers");
_Static_assert(offsetof(fwm_venc_temporal_skip_t, skip_frames) == 4, "temporal_skip.skip_frames");
_Static_assert(offsetof(fwm_venc_temporal_skip_t, layer_ratio_en) == 8, "temporal_skip.layer_ratio_en");
_Static_assert(offsetof(fwm_venc_temporal_skip_t, layer_ratio) == 12, "temporal_skip.layer_ratio");

/* B.4.15 allocation request */
_Static_assert(sizeof(fwm_venc_input_pool_t) == 12, "input_pool is 12 bytes");
_Static_assert(offsetof(fwm_venc_input_pool_t, count) == 0, "input_pool.count");
_Static_assert(offsetof(fwm_venc_input_pool_t, luma_size) == 4, "input_pool.luma_size");
_Static_assert(offsetof(fwm_venc_input_pool_t, chroma_size) == 8, "input_pool.chroma_size");

/* H.265 records (spec H265 12 section 3). */
_Static_assert(sizeof(fwm_venc_h265_profile_e) == 4, "fwm_venc_h265_profile_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_h265_rc_mode_e) == 4, "fwm_venc_h265_rc_mode_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_h265_gop_mode_e) == 4, "fwm_venc_h265_gop_mode_e is 4 bytes");
_Static_assert(sizeof(fwm_venc_h265_profile_level_t) == 8, "h265_profile_level is 8 bytes");
_Static_assert(offsetof(fwm_venc_h265_profile_level_t, profile) == 0, "h265_profile_level.profile");
_Static_assert(offsetof(fwm_venc_h265_profile_level_t, level) == 4, "h265_profile_level.level");
_Static_assert(sizeof(fwm_venc_h265_gop_t) == 16, "h265_gop is 16 bytes");
_Static_assert(offsetof(fwm_venc_h265_gop_t, gop_control_en) == 0, "h265_gop.gop_control_en");
_Static_assert(offsetof(fwm_venc_h265_gop_t, gop_mode) == 4, "h265_gop.gop_mode");
_Static_assert(offsetof(fwm_venc_h265_gop_t, gop_size) == 8, "h265_gop.gop_size");
_Static_assert(offsetof(fwm_venc_h265_gop_t, total_frames_num) == 12, "h265_gop.total_frames_num");
_Static_assert(sizeof(fwm_venc_h265_tendency_t) == 12, "h265_tendency is 12 bytes");
_Static_assert(offsetof(fwm_venc_h265_tendency_t, inter) == 0, "h265_tendency.inter");
_Static_assert(offsetof(fwm_venc_h265_tendency_t, skip) == 4, "h265_tendency.skip");
_Static_assert(offsetof(fwm_venc_h265_tendency_t, merge_sub) == 8, "h265_tendency.merge_sub");
_Static_assert(sizeof(fwm_venc_h265_transform_t) == 8, "h265_transform is 8 bytes");
_Static_assert(offsetof(fwm_venc_h265_transform_t, transform_skip_en) == 0, "h265_transform.transform_skip_en");
_Static_assert(offsetof(fwm_venc_h265_transform_t, transform_8x8_en) == 4, "h265_transform.transform_8x8_en");
_Static_assert(sizeof(fwm_venc_h265_sao_t) == 12, "h265_sao is 12 bytes");
_Static_assert(offsetof(fwm_venc_h265_sao_t, enabled) == 0, "h265_sao.enabled");
_Static_assert(offsetof(fwm_venc_h265_sao_t, slice_sao_luma) == 4, "h265_sao.slice_sao_luma");
_Static_assert(offsetof(fwm_venc_h265_sao_t, slice_sao_chroma) == 8, "h265_sao.slice_sao_chroma");
_Static_assert(sizeof(fwm_venc_h265_deblock_t) == 16, "h265_deblock is 16 bytes");
_Static_assert(offsetof(fwm_venc_h265_deblock_t, deblock_idc) == 0, "h265_deblock.deblock_idc");
_Static_assert(offsetof(fwm_venc_h265_deblock_t, tc_offset_div2) == 4, "h265_deblock.tc_offset_div2");
_Static_assert(offsetof(fwm_venc_h265_deblock_t, beta_offset_div2) == 8, "h265_deblock.beta_offset_div2");
_Static_assert(sizeof(fwm_venc_h265_timing_t) == 16, "h265_timing is 16 bytes");
_Static_assert(offsetof(fwm_venc_h265_timing_t, num_units_in_tick) == 0, "h265_timing.num_units_in_tick");
_Static_assert(offsetof(fwm_venc_h265_timing_t, time_scale) == 4, "h265_timing.time_scale");
_Static_assert(offsetof(fwm_venc_h265_timing_t, num_ticks_poc_diff_one) == 8, "h265_timing.num_ticks_poc_diff_one");
_Static_assert(offsetof(fwm_venc_h265_timing_t, framerate) == 12, "h265_timing.framerate");
_Static_assert(sizeof(fwm_venc_h265_config_t) == 92, "h265_config is 92 bytes");
_Static_assert(offsetof(fwm_venc_h265_config_t, profile_level) == 0, "h265_config.profile_level");
_Static_assert(offsetof(fwm_venc_h265_config_t, qp_range) == 8, "h265_config.qp_range");
_Static_assert(offsetof(fwm_venc_h265_config_t, frame_rate) == 16, "h265_config.frame_rate");
_Static_assert(offsetof(fwm_venc_h265_config_t, source_frame_rate) == 20, "h265_config.source_frame_rate");
_Static_assert(offsetof(fwm_venc_h265_config_t, bitrate) == 24, "h265_config.bitrate");
_Static_assert(offsetof(fwm_venc_h265_config_t, idr_period) == 28, "h265_config.idr_period");
_Static_assert(offsetof(fwm_venc_h265_config_t, intra_period) == 32, "h265_config.intra_period");
_Static_assert(offsetof(fwm_venc_h265_config_t, gop_size) == 36, "h265_config.gop_size");
_Static_assert(offsetof(fwm_venc_h265_config_t, qp_init) == 40, "h265_config.qp_init");
_Static_assert(offsetof(fwm_venc_h265_config_t, rc_mode) == 44, "h265_config.rc_mode");
_Static_assert(offsetof(fwm_venc_h265_config_t, min_i_qp) == 48, "h265_config.min_i_qp");
_Static_assert(offsetof(fwm_venc_h265_config_t, vbr) == 52, "h265_config.vbr");
_Static_assert(offsetof(fwm_venc_h265_config_t, fixed_qp) == 64, "h265_config.fixed_qp");
_Static_assert(offsetof(fwm_venc_h265_config_t, gop) == 76, "h265_config.gop");

/* B.3 device table: 12 slots, slot n at 4 * n */
_Static_assert(sizeof(fwm_venc_device_t) == 48, "device is 48 bytes");
_Static_assert(offsetof(fwm_venc_device_t, name) == 0, "device.name");
_Static_assert(offsetof(fwm_venc_device_t, open) == 4, "device.open");
_Static_assert(offsetof(fwm_venc_device_t, init) == 8, "device.init");
_Static_assert(offsetof(fwm_venc_device_t, uninit) == 12, "device.uninit");
_Static_assert(offsetof(fwm_venc_device_t, close) == 16, "device.close");
_Static_assert(offsetof(fwm_venc_device_t, encode) == 20, "device.encode");
_Static_assert(offsetof(fwm_venc_device_t, get_parameter) == 24, "device.get_parameter");
_Static_assert(offsetof(fwm_venc_device_t, set_parameter) == 28, "device.set_parameter");
_Static_assert(offsetof(fwm_venc_device_t, ready_frame_count) == 32, "device.ready_frame_count");
_Static_assert(offsetof(fwm_venc_device_t, get_frame) == 36, "device.get_frame");
_Static_assert(offsetof(fwm_venc_device_t, release_frame) == 40, "device.release_frame");
_Static_assert(offsetof(fwm_venc_device_t, reset_frames) == 44, "device.reset_frames");
#endif

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_VENC_TYPES_H */
