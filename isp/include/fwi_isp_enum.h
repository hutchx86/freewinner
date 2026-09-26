// SPDX-License-Identifier: AGPL-3.0-only
#ifndef FWI_ISP_ENUM_H
#define FWI_ISP_ENUM_H
typedef enum fwi_awb_light_class_e {
    FWI_AWB_LIGHT_CLASS_AH = 0,
    FWI_AWB_LIGHT_CLASS_FLUO = 1,
    FWI_AWB_LIGHT_CLASS_DAY = 2,
    FWI_AWB_LIGHT_CLASS_AH2FLUO = 3,
    FWI_AWB_LIGHT_CLASS_FLUO2DAY = 4,
    FWI_AWB_LIGHT_CLASS_GREENZONE = 5,
    FWI_AWB_LIGHT_CLASS_SHADOW = 6,
    FWI_AWB_LIGHT_CLASS_BLUESKY = 7,
    FWI_AWB_LIGHT_CLASS_FLASH = 8,
    FWI_AWB_LIGHT_CLASS_OUTLIER = 9,
    FWI_AWB_LIGHT_CLASS_MAX = 10,
} fwi_awb_light_class_e;
typedef enum fwi_channel_gain_e {
    FWI_R_GAIN = 0,
    FWI_B_GAIN = 1,
    FWI_GAIN_COUNT = 2,
} fwi_channel_gain_e;
typedef enum fwi_colour_channel_e {
    FWI_AWB_R_CH = 0,
    FWI_AWB_G_CH = 1,
    FWI_AWB_B_CH = 2,
    FWI_AWB_CH_COUNT = 3,
} fwi_colour_channel_e;
typedef enum fwi_judge_comp_cfg_e {
    FWI_STATUS_STATIC = 0,
    FWI_STATUS_SUS_STATIC = 1,
    FWI_STATUS_SUS_MOTION = 2,
    FWI_STATUS_MOTION = 3,
    FWI_STATUS_JUDGE_MAX = 4,
} fwi_judge_comp_cfg_e;
typedef enum fwi_tone_mapping_cfg_ind_e {
    FWI_GTM_HIST_EQ_GAIN = 0,
    FWI_GTM_HIST_EQ_EQ_RATIO = 1,
    FWI_GTM_HIST_EQ_EQ_SMOOTH = 2,
    FWI_GTM_HIST_EQ_BLACK = 3,
    FWI_GTM_HIST_EQ_WHITE = 4,
    FWI_GTM_HIST_EQ_BLACK_ALPHA = 5,
    FWI_GTM_HIST_EQ_WHITE_ALPHA = 6,
    FWI_GTM_HIST_EQ_GAMMA_IND = 7,
    FWI_GTM_HIST_EQ_GAMMA_PLUS = 8,
    FWI_GTM_HIST_EQ_MAX = 9,
} fwi_tone_mapping_cfg_ind_e;
typedef enum fwi_ae_bins_id_e {
    FWI_AE_DARK_BINS = 0,
    FWI_AE_BRIGHT_BINS = 1,
    FWI_AE_OTHER_BINS = 2,
    FWI_AE_MAX_BINS = 3,
} fwi_ae_bins_id_e;
typedef enum fwi_ae_metering_mode_e {
    FWI_AE_METERING_MODE_AVERAGE = 0,
    FWI_AE_METERING_MODE_CENTER = 1,
    FWI_AE_METERING_MODE_SPOT = 2,
    FWI_AE_METERING_MODE_MATRIX = 3,
} fwi_ae_metering_mode_e;
typedef enum fwi_ae_mode_e {
    FWI_AE_NORM = 0,
    FWI_AE_WDR = 1,
} fwi_ae_mode_e;
typedef enum fwi_ae_status_e {
    FWI_AE_STATUS_IDLE = 0,
    FWI_AE_STATUS_BUSY = 1,
    FWI_AE_STATUS_DONE = 2,
} fwi_ae_status_e;
typedef enum fwi_ae_table_change_e {
    FWI_AE_TABLE_EXPOSURE_CHANGED = 0,
    FWI_AE_TABLE_IRIS_CHANGED = 1,
    FWI_AE_TABLE_GAIN_CHANGED = 2,
} fwi_ae_table_change_e;
typedef enum fwi_ae_table_mode_e {
    FWI_SCENE_MODE_PREVIEW = 0,
    FWI_SCENE_MODE_CAPTURE = 1,
    FWI_SCENE_MODE_VIDEO = 2,
    FWI_SCENE_MODE_BACKLIGHT = 3,
    FWI_SCENE_MODE_BEACH_SNOW = 4,
    FWI_SCENE_MODE_FIREWORKS = 5,
    FWI_SCENE_MODE_LANDSCAPE = 6,
    FWI_SCENE_MODE_NIGHT = 7,
    FWI_SCENE_MODE_SPORTS = 8,
    FWI_SCENE_MODE_USER_DEF0 = 9,
    FWI_SCENE_MODE_USER_DEF1 = 10,
    FWI_SCENE_MODE_USER_DEF2 = 11,
    FWI_SCENE_MODE_USER_DEF3 = 12,
    FWI_SCENE_MODE_USER_DEF4 = 13,
    FWI_SCENE_MODE_USER_DEF5 = 14,
    FWI_SCENE_MODE_SENSOR_DRIVER = 15,
    FWI_SCENE_MODE_MAX = 16,
} fwi_ae_table_mode_e;
typedef enum fwi_afs_point_character_e {
    FWI_AFS_PEAK = 0,
    FWI_AFS_VALLEY = 1,
    FWI_AFS_FLAT = 2,
} fwi_afs_point_character_e;
typedef enum fwi_afs_trendline_e {
    FWI_AFS_INCREASE = 0,
    FWI_AFS_DECREASE = 1,
    FWI_AFS_FIXED = 2,
} fwi_afs_trendline_e;
typedef enum fwi_auto_focus_metering_mode_e {
    FWI_AUTO_FOCUS_METERING_AVERAGE = 0,
    FWI_AUTO_FOCUS_METERING_CENTER_WEIGHTED = 1,
    FWI_AUTO_FOCUS_METERING_SPOT = 2,
    FWI_AUTO_FOCUS_METERING_MATRIX = 3,
} fwi_auto_focus_metering_mode_e;
typedef enum fwi_auto_focus_range_new_e {
    FWI_AUTO_FOCUS_RANGE_AUTO = 0,
    FWI_AUTO_FOCUS_RANGE_NORMAL = 1,
    FWI_AUTO_FOCUS_RANGE_MACRO = 2,
    FWI_AUTO_FOCUS_RANGE_INFINITY = 3,
} fwi_auto_focus_range_new_e;
typedef enum fwi_auto_focus_run_mode_e {
    FWI_AUTO_FOCUS_MANUAL = 0,
    FWI_AUTO_FOCUS_CONTINUOUS = 1,
    FWI_AUTO_FOCUS_TOUCH = 2,
    FWI_AUTO_FOCUS_SNAP = 3,
} fwi_auto_focus_run_mode_e;
typedef enum fwi_auto_focus_status_e {
    FWI_AUTO_FOCUS_STATUS_IDLE = 0,
    FWI_AUTO_FOCUS_STATUS_BUSY = 1,
    FWI_AUTO_FOCUS_STATUS_REACHED = 2,
    FWI_AUTO_FOCUS_STATUS_APPROACH = 3,
    FWI_AUTO_FOCUS_STATUS_REFOCUS = 4,
    FWI_AUTO_FOCUS_STATUS_FAILED = 5,
} fwi_auto_focus_status_e;
typedef enum fwi_black_level_e {
    FWI_ISP_BLACK_LEVEL_R_OFFSET = 0,
    FWI_ISP_BLACK_LEVEL_GR_OFFSET = 1,
    FWI_ISP_BLACK_LEVEL_GB_OFFSET = 2,
    FWI_ISP_BLACK_LEVEL_B_OFFSET = 3,
    FWI_ISP_BLACK_LEVEL_MAX = 4,
} fwi_black_level_e;
typedef enum fwi_colour_effect_e {
    FWI_ISP_COLORFX_NONE = 0,
    FWI_ISP_COLORFX_GRAY = 1,
    FWI_ISP_COLORFX_NEGATIVE = 2,
    FWI_ISP_COLORFX_ANTIQUE = 3,
    FWI_ISP_COLORFX_RTONE = 4,
    FWI_ISP_COLORFX_GTONE = 5,
    FWI_ISP_COLORFX_BTONE = 6,
} fwi_colour_effect_e;
typedef enum fwi_detected_flicker_type_e {
    FWI_FLICKER_NO = 0,
    FWI_FLICKER_50HZ = 1,
    FWI_FLICKER_60HZ = 2,
} fwi_detected_flicker_type_e;
typedef enum fwi_direction_e {
    FWI_MD_LEFT = 0,
    FWI_MD_RIGHT = 1,
    FWI_MD_UP = 2,
    FWI_MD_DOWN = 3,
    FWI_MD_BACKWARD = 4,
    FWI_MD_FORWARD = 5,
    FWI_MD_NO_MOTION = 6,
} fwi_direction_e;
typedef enum fwi_defect_pixel_cfg_e {
    FWI_ISP_DEFECT_PIXEL_HOT_RATIO = 0,
    FWI_ISP_DEFECT_PIXEL_COLD_RATIO = 1,
    FWI_ISP_DEFECT_PIXEL_NBHD_DIFF_RATIO = 2,
    FWI_ISP_DEFECT_PIXEL_NEAREST_DIFF_RATIO = 3,
    FWI_ISP_DEFECT_PIXEL_SLOPE_THRESHOLD = 4,
    FWI_ISP_DEFECT_PIXEL_COLD_ABS_THRESHOLD = 5,
    FWI_ISP_DEFECT_PIXEL_MAX = 6,
} fwi_defect_pixel_cfg_e;
typedef enum fwi_e3a_settings_flags_e {
    FWI_ISP_SET_SCENE_MODE = 1,
    FWI_ISP_SET_AWB_MODE = 2,
    FWI_ISP_SET_FLICKER_MODE = 4,
    FWI_ISP_SET_SHARPNESS = 8,
    FWI_ISP_SET_BRIGHTNESS = 16,
    FWI_ISP_SET_SATURATION = 32,
    FWI_ISP_SET_EFFECT = 64,
    FWI_ISP_SET_AF_METERING_MODE = 128,
    FWI_ISP_SET_AE_METERING_MODE = 256,
    FWI_ISP_SET_CONTRAST = 512,
    FWI_ISP_SET_HUE = 1024,
    FWI_ISP_SET_GAIN_STR = 2048,
    FWI_ISP_SETTING_MAX = 2049,
    FWI_ISP_SETTINGS_ALL = 4095,
} fwi_e3a_settings_flags_e;
typedef enum fwi_enable_flag_e {
    FWI_DISABLE = 0,
    FWI_ENABLE = 1,
} fwi_enable_flag_e;
typedef enum fwi_exposure_cfg_type_e {
    FWI_ANTI_EXPOSURE_WINDOW_OVER = 0,
    FWI_ANTI_EXPOSURE_WINDOW_UNDER = 1,
    FWI_ANTI_EXPOSURE_HIST_OVER = 2,
    FWI_ANTI_EXPOSURE_HIST_UNDER = 3,
    FWI_AE_PREVIEW_SPEED = 4,
    FWI_AE_CAPTURE_SPEED = 5,
    FWI_AE_VIDEO_SPEED = 6,
    FWI_AE_TOUCH_SPEED = 7,
    FWI_AE_TOLERANCE = 8,
    FWI_AE_TARGET = 9,
    FWI_AE_HIST_DARK_WEIGHT_MIN = 10,
    FWI_AE_HIST_DARK_WEIGHT_MAX = 11,
    FWI_AE_HIST_BRIGHT_WEIGHT_MIN = 12,
    FWI_AE_HIST_BRIGHT_WEIGHT_MAX = 13,
    FWI_ISP_EXPOSURE_CFG_MAX = 14,
} fwi_exposure_cfg_type_e;
typedef enum fwi_exposure_mode_e {
    FWI_EXPOSURE_AUTO = 0,
    FWI_EXPOSURE_MANUAL = 1,
    FWI_SHUTTER_PRIORITY = 2,
    FWI_APERTURE_PRIORITY = 3,
} fwi_exposure_mode_e;
typedef enum fwi_flash_mode_e {
    FWI_FLASH_MODE_OFF = 0,
    FWI_FLASH_MODE_ON = 1,
    FWI_FLASH_MODE_TORCH = 2,
    FWI_FLASH_MODE_AUTO = 3,
    FWI_FLASH_MODE_RED_EYE = 4,
    FWI_FLASH_MODE_NONE = 5,
} fwi_flash_mode_e;
typedef enum fwi_iso_mode_e {
    FWI_ISO_MANUAL = 0,
    FWI_ISO_AUTO = 1,
} fwi_iso_mode_e;
typedef enum fwi_ae_check_delay_type_e {
    FWI_ISP_AE_DELAY_WDR_EXPOSURE_SHORT = 0,
    FWI_ISP_AE_DELAY_WDR_EXPOSURE_LONG = 1,
    FWI_ISP_AE_DELAY_WDR_AGAIN = 2,
    FWI_ISP_AE_DELAY_WDR_ISP_HARDWARE = 3,
    FWI_ISP_AE_DELAY_DG_ISP_HARDWARE = 4,
    FWI_ISP_AE_DELAY_MAX = 5,
} fwi_ae_check_delay_type_e;
typedef enum fwi_ae_stage_e {
    FWI_AE_BEFORE_WDR = 0,
    FWI_AE_AFTER_WDR = 1,
    FWI_AE_AFTER_CHROMA_DENOISE = 2,
} fwi_ae_stage_e;
typedef enum fwi_ae_param_type_e {
    FWI_ISP_AE_INIT_DATA = 0,
    FWI_ISP_AE_UPDATE_AE_TABLE = 1,
    FWI_ISP_AE_SET_EXPOSURE_INDEX = 2,
    FWI_ISP_AE_BUILD_TOUCH_WEIGHT = 3,
    FWI_ISP_AE_PARAM_TYPE_MAX = 4,
} fwi_ae_param_type_e;
typedef enum fwi_af_mode_e {
    FWI_AF_BEFORE_D2D = 0,
    FWI_AF_AFTER_D2D = 1,
} fwi_af_mode_e;
typedef enum fwi_af_param_type_e {
    FWI_ISP_AF_INIT_DATA = 0,
    FWI_ISP_AF_TRIGGER = 1,
    FWI_ISP_AF_PARAM_TYPE_MAX = 2,
} fwi_af_param_type_e;
typedef enum fwi_afs_param_type_e {
    FWI_ISP_AFS_PARAM_TYPE_MAX = 0,
} fwi_afs_param_type_e;
typedef enum fwi_awb_mode_e {
    FWI_AWB_AFTER_WDR = 0,
    FWI_AWB_AFTER_PLTM = 1,
} fwi_awb_mode_e;
typedef enum fwi_awb_param_type_e {
    FWI_ISP_AWB_INIT_DATA = 0,
    FWI_ISP_AWB_PARAM_TYPE_MAX = 1,
} fwi_awb_param_type_e;
typedef enum fwi_demosaic_mode_e {
    FWI_DEMOSAIC_NORM_MODE = 0,
    FWI_DEMOSAIC_BW_MODE = 1,
} fwi_demosaic_mode_e;
typedef enum fwi_contrast_cfg_e {
    FWI_ISP_CONTRAST_MIN_VAL = 0,
    FWI_ISP_CONTRAST_MAX_VAL = 1,
    FWI_ISP_CONTRAST_BLACK_LEVEL = 2,
    FWI_ISP_CONTRAST_WHITE_LEVEL = 3,
    FWI_ISP_CONTRAST_BLACK_CLIP = 4,
    FWI_ISP_CONTRAST_WHITE_CLIP = 5,
    FWI_ISP_CONTRAST_PLAT_THRESHOLD = 6,
    FWI_ISP_CONTRAST_BLACK_GAIN = 7,
    FWI_ISP_CONTRAST_BLACK_OFFSET = 8,
    FWI_ISP_CONTRAST_WHITE_GAIN = 9,
    FWI_ISP_CONTRAST_WHITE_OFFSET = 10,
    FWI_ISP_CONTRAST_MAX = 11,
} fwi_contrast_cfg_e;
typedef enum fwi_d3d_mode_e {
    FWI_D3D_MAX = 0,
    FWI_D3D_MIN = 1,
    FWI_D3D_WT = 2,
} fwi_d3d_mode_e;
typedef enum fwi_denoise_cfg_e {
    FWI_ISP_DENOISE_BLACK_GAIN = 0,
    FWI_ISP_DENOISE_BLACK_OFFSET = 1,
    FWI_ISP_DENOISE_WHITE_GAIN = 2,
    FWI_ISP_DENOISE_WHITE_OFFSET = 3,
    FWI_ISP_DENOISE_HF_RATIO = 4,
    FWI_ISP_DENOISE_BF_RATIO = 5,
    FWI_ISP_DENOISE_LF_RATIO = 6,
    FWI_ISP_DENOISE_LP0_NP_SIDE_RATIO = 7,
    FWI_ISP_DENOISE_LP1_NP_SIDE_RATIO = 8,
    FWI_ISP_DENOISE_LP2_NP_SIDE_RATIO = 9,
    FWI_ISP_DENOISE_LP0_THRESHOLD_RATIO = 10,
    FWI_ISP_DENOISE_LP1_THRESHOLD_RATIO = 11,
    FWI_ISP_DENOISE_LP2_THRESHOLD_RATIO = 12,
    FWI_ISP_DENOISE_LP3_THRESHOLD_RATIO = 13,
    FWI_ISP_DENOISE_LP0_PCNT_RATIO = 14,
    FWI_ISP_DENOISE_LP1_PCNT_RATIO = 15,
    FWI_ISP_DENOISE_LP2_PCNT_RATIO = 16,
    FWI_ISP_DENOISE_LP3_PCNT_RATIO = 17,
    FWI_ISP_DENOISE_HI_THRESHOLD = 18,
    FWI_ISP_DENOISE_LOW_THRESHOLD = 19,
    FWI_ISP_DENOISE_MAX = 20,
} fwi_denoise_cfg_e;
typedef enum fwi_dg_mode_e {
    FWI_DG_AFTER_WDR = 0,
    FWI_DG_AFTER_SENSOR_OFFSET = 1,
    FWI_DG_BEFORE_WDR = 2,
} fwi_dg_mode_e;
typedef enum fwi_defect_pixel_mode_e {
    FWI_DEFECT_PIXEL_NORMAL = 0,
    FWI_DEFECT_PIXEL_STRONG = 1,
    FWI_DEFECT_PIXEL_PEPPER = 2,
} fwi_defect_pixel_mode_e;
typedef enum fwi_features_flags_e {
    FWI_ISP_FEATURES_AE = 1,
    FWI_ISP_FEATURES_LINEARIZE = 2,
    FWI_ISP_FEATURES_WDR = 4,
    FWI_ISP_FEATURES_DEFECT_PIXEL = 8,
    FWI_ISP_FEATURES_D2D = 16,
    FWI_ISP_FEATURES_D3D = 32,
    FWI_ISP_FEATURES_AWB = 64,
    FWI_ISP_FEATURES_WB = 128,
    FWI_ISP_FEATURES_LENS_SHADING = 256,
    FWI_ISP_FEATURES_GAMMA = 512,
    FWI_ISP_FEATURES_SHARP = 1024,
    FWI_ISP_FEATURES_AF = 2048,
    FWI_ISP_FEATURES_RGB2RGB = 4096,
    FWI_ISP_FEATURES_RGB_DRC = 8192,
    FWI_ISP_FEATURES_PLTM = 16384,
    FWI_ISP_FEATURES_COLOUR_ENHANCE = 32768,
    FWI_ISP_FEATURES_AFS = 65536,
    FWI_ISP_FEATURES_HIST = 131072,
    FWI_ISP_FEATURES_BLACK_LEVEL = 262144,
    FWI_ISP_FEATURES_DG = 524288,
    FWI_ISP_FEATURES_SENSOR_OFFSET = 1048576,
    FWI_ISP_FEATURES_CROSSTALK = 2097152,
    FWI_ISP_FEATURES_CONTRAST = 4194304,
    FWI_ISP_FEATURES_CHROMA_DENOISE = 8388608,
    FWI_ISP_FEATURES_SATURATION = 16777216,
    FWI_ISP_FEATURES_DEMOSAIC = 33554432,
    FWI_ISP_FEATURES_MODE = 67108864,
    FWI_ISP_FEATURES_LATERAL_CA = 134217728,
    FWI_ISP_FEATURES_GREEN_CA = 268435456,
    FWI_ISP_FEATURES_MESH_SHADING = 536870912,
    FWI_ISP_FEATURES_RGB2YUV = 1073741824,
} fwi_features_flags_e;
typedef enum fwi_gamma_type_e {
    FWI_ISP_GTM_GAMMA_FIXED = 0,
    FWI_ISP_GTM_GAMMA_DYNAMIC = 1,
    FWI_ISP_GTM_GAMMA_TYPE_MAX = 2,
} fwi_gamma_type_e;
typedef enum fwi_green_ca_cfg_e {
    FWI_ISP_GREEN_CA_R_PARA0 = 0,
    FWI_ISP_GREEN_CA_R_PARA1 = 1,
    FWI_ISP_GREEN_CA_R_PARA2 = 2,
    FWI_ISP_GREEN_CA_B_PARA0 = 3,
    FWI_ISP_GREEN_CA_B_PARA1 = 4,
    FWI_ISP_GREEN_CA_B_PARA2 = 5,
    FWI_ISP_GREEN_CA_INT_CNS = 6,
    FWI_ISP_GREEN_CA_MAX = 7,
} fwi_green_ca_cfg_e;
typedef enum fwi_gtm_comm_cfg_e {
    FWI_ISP_GTM_GAIN = 0,
    FWI_ISP_GTM_EQ_RATIO = 1,
    FWI_ISP_GTM_EQ_SMOOTH = 2,
    FWI_ISP_GTM_BLACK = 3,
    FWI_ISP_GTM_WHITE = 4,
    FWI_ISP_GTM_BLACK_ALPHA = 5,
    FWI_ISP_GTM_WHITE_ALPHA = 6,
    FWI_ISP_GTM_GAMMA_IND = 7,
    FWI_ISP_GTM_GAMMA_PLUS = 8,
    FWI_ISP_GTM_HIST_EQ_MAX = 9,
} fwi_gtm_comm_cfg_e;
typedef enum fwi_gtm_param_type_e {
    FWI_ISP_GTM_INIT_DATA = 0,
    FWI_ISP_GTM_PARAM_TYPE_MAX = 1,
} fwi_gtm_param_type_e;
typedef enum fwi_gtm_type_e {
    FWI_ISP_GTM_FIXED = 0,
    FWI_ISP_GTM_DYNAMIC_DRC = 1,
    FWI_ISP_GTM_DYNAMIC_GAMMA = 2,
    FWI_ISP_GTM_KEEP_LUMINANCE_DRC = 3,
    FWI_ISP_GTM_TYPE_MAX = 4,
} fwi_gtm_type_e;
typedef enum fwi_hist_mode_e {
    FWI_AVERAGE_MODE = 0,
    FWI_MIN_MODE = 1,
    FWI_MAX_MODE = 2,
} fwi_hist_mode_e;
typedef enum fwi_hist_source_e {
    FWI_HIST_AFTER_WDR = 0,
    FWI_HIST_AFTER_CHROMA_DENOISE = 1,
} fwi_hist_source_e;
typedef enum fwi_iso_param_type_e {
    FWI_ISP_ISO_UPDATE_PARAMS = 0,
    FWI_ISP_ISO_PARAM_TYPE_MAX = 1,
} fwi_iso_param_type_e;
typedef enum fwi_input_seq_e {
    FWI_INPUT_SEQ_BGGR = 4,
    FWI_INPUT_SEQ_RGGB = 5,
    FWI_INPUT_SEQ_GBRG = 6,
    FWI_INPUT_SEQ_GRBG = 7,
} fwi_input_seq_e;
typedef enum fwi_lateral_ca_cfg_e {
    FWI_ISP_LATERAL_CA_GF_COR_RATIO = 0,
    FWI_ISP_LATERAL_CA_PF_COR_RATIO = 1,
    FWI_ISP_LATERAL_CA_LUMINANCE_THRESHOLD = 2,
    FWI_ISP_LATERAL_CA_GRAD_THRESHOLD = 3,
    FWI_ISP_LATERAL_CA_CLRS_GTH = 4,
    FWI_ISP_LATERAL_CA_PF_RSHF = 5,
    FWI_ISP_LATERAL_CA_PF_BSLP = 6,
    FWI_ISP_LATERAL_CA_CLRS_LUMINANCE_THRESHOLD = 7,
    FWI_ISP_LATERAL_CA_PF_CLRC_RATIO = 8,
    FWI_ISP_LATERAL_CA_GF_CLRC_RATIO = 9,
    FWI_ISP_LATERAL_CA_PF_DECR_RATIO = 10,
    FWI_ISP_LATERAL_CA_MAX = 11,
} fwi_lateral_ca_cfg_e;
typedef enum fwi_md_param_type_e {
    FWI_ISP_MD_PARAM_TYPE_MAX = 0,
} fwi_md_param_type_e;
typedef enum fwi_output_speed_e {
    FWI_ISP_OUTPUT_SPEED_0 = 0,
    FWI_ISP_OUTPUT_SPEED_1 = 1,
    FWI_ISP_OUTPUT_SPEED_2 = 2,
    FWI_ISP_OUTPUT_SPEED_3 = 3,
} fwi_output_speed_e;
typedef enum fwi_pltm_comm_cfg_e {
    FWI_ISP_PLTM_MODE = 0,
    FWI_ISP_PLTM_ORIGINAL_PICTURE_RATIO = 1,
    FWI_ISP_PLTM_TR_ORDER = 2,
    FWI_ISP_PLTM_LAST_ORDER_RATIO = 3,
    FWI_ISP_PLTM_POW_TBL = 4,
    FWI_ISP_PLTM_F_TBL = 5,
    FWI_ISP_PLTM_LSS_SWITCH = 6,
    FWI_ISP_PLTM_LUMINANCE_RATIO = 7,
    FWI_ISP_PLTM_LP_HALO_RES = 8,
    FWI_ISP_PLTM_WHITE_LEVEL = 9,
    FWI_ISP_PLTM_SPATIAL_ASM = 10,
    FWI_ISP_PLTM_INTENS_ASYM = 11,
    FWI_ISP_PLTM_BLOCK_V_COUNT = 12,
    FWI_ISP_PLTM_BLOCK_H_COUNT = 13,
    FWI_ISP_PLTM_CONTRAST = 14,
    FWI_ISP_PLTM_TOLERANCE = 15,
    FWI_ISP_PLTM_SPEED = 16,
    FWI_ISP_PLTM_STEP = 17,
    FWI_ISP_PLTM_INTERVAL_FRAME = 18,
    FWI_ISP_PLTM_MAX = 19,
} fwi_pltm_comm_cfg_e;
typedef enum fwi_pltm_param_type_e {
    FWI_ISP_PLTM_PARAM_TYPE_MAX = 0,
} fwi_pltm_param_type_e;
typedef enum fwi_raw_ch_e {
    FWI_ISP_RAW_CH_R = 0,
    FWI_ISP_RAW_CH_GR = 1,
    FWI_ISP_RAW_CH_GB = 2,
    FWI_ISP_RAW_CH_G = 3,
    FWI_ISP_RAW_CH_MAX = 4,
} fwi_raw_ch_e;
typedef enum fwi_rolloff_param_type_e {
    FWI_ISP_ROLLOFF_INIT_DATA = 0,
    FWI_ISP_ROLLOFF_PARAM_TYPE_MAX = 1,
} fwi_rolloff_param_type_e;
typedef enum fwi_saturation_cfg_e {
    FWI_ISP_SATURATION_SATURATION_R = 0,
    FWI_ISP_SATURATION_SATURATION_G = 1,
    FWI_ISP_SATURATION_SATURATION_B = 2,
    FWI_ISP_SATURATION_SATURATION_MODE = 3,
    FWI_ISP_SATURATION_SATURATION_TBL_SG1 = 4,
    FWI_ISP_SATURATION_SATURATION_TBL_SG2 = 5,
    FWI_ISP_SATURATION_SATURATION_TBL_THRESHOLD = 6,
    FWI_ISP_SATURATION_MAX = 7,
} fwi_saturation_cfg_e;
typedef enum fwi_sharp_cfg_e {
    FWI_ISP_SHARP_MIN_VAL = 0,
    FWI_ISP_SHARP_MAX_VAL = 1,
    FWI_ISP_SHARP_BLACK_LEVEL = 2,
    FWI_ISP_SHARP_WHITE_LEVEL = 3,
    FWI_ISP_SHARP_BLACK_CLIP = 4,
    FWI_ISP_SHARP_WHITE_CLIP = 5,
    FWI_ISP_SHARP_BLACK_GAIN = 6,
    FWI_ISP_SHARP_BLACK_OFFSET = 7,
    FWI_ISP_SHARP_WHITE_GAIN = 8,
    FWI_ISP_SHARP_WHITE_OFFSET = 9,
    FWI_ISP_SHARP_EDGE_SCALE_RATIO = 10,
    FWI_ISP_SHARP_HIGH_FREQUENCY_SCALE_RATIO = 11,
    FWI_ISP_SHARP_EDGE_CONV_PARA = 12,
    FWI_ISP_SHARP_HIGH_FREQUENCY_CONV_PARA = 13,
    FWI_ISP_SHARP_DIR_EQ_RATIO = 14,
    FWI_ISP_SHARP_DIR_CLIP_VAL = 15,
    FWI_ISP_SHARP_NS_LW_THRESHOLD = 16,
    FWI_ISP_SHARP_NS_HI_THRESHOLD = 17,
    FWI_ISP_SHARP_EDGE_THRESHOLD = 18,
    FWI_ISP_SHARP_HV_EDGE_SMOOTHING_RATIO = 19,
    FWI_ISP_SHARP_AA_EDGE_SMOOTHING_RATIO = 20,
    FWI_ISP_SHARP_EDGE_WHITE_STRENGTH = 21,
    FWI_ISP_SHARP_EDGE_BLACK_STRENGTH = 22,
    FWI_ISP_SHARP_HIGH_FREQUENCY_WHITE_STRENGTH = 23,
    FWI_ISP_SHARP_HIGH_FREQUENCY_BLACK_STRENGTH = 24,
    FWI_ISP_SHARP_OVER_AREA_CTRL = 25,
    FWI_ISP_SHARP_UNDER_AREA_CTRL = 26,
    FWI_ISP_SHARP_OVER_VAL_CTRL = 27,
    FWI_ISP_SHARP_UNDER_VAL_CTRL = 28,
    FWI_ISP_SHARP_MAX = 29,
} fwi_sharp_cfg_e;
typedef enum fwi_denoise_3d_cfg_e {
    FWI_ISP_DENOISE_3D_NOISE_CLIP_RATIO = 0,
    FWI_ISP_DENOISE_3D_DIFF_CLIP_RATIO = 1,
    FWI_ISP_DENOISE_3D_K_3D_S = 2,
    FWI_ISP_DENOISE_3D_DIFF_CALIBRATION_MODE = 3,
    FWI_ISP_DENOISE_3D_BLACK_GAIN = 4,
    FWI_ISP_DENOISE_3D_BLACK_OFFSET = 5,
    FWI_ISP_DENOISE_3D_WHITE_GAIN = 6,
    FWI_ISP_DENOISE_3D_WHITE_OFFSET = 7,
    FWI_ISP_DENOISE_3D_REF_BLACK_GAIN = 8,
    FWI_ISP_DENOISE_3D_REF_BLACK_OFFSET = 9,
    FWI_ISP_DENOISE_3D_REF_WHITE_GAIN = 10,
    FWI_ISP_DENOISE_3D_REF_WHITE_OFFSET = 11,
    FWI_ISP_DENOISE_3D_LUMINANCE_DIFF_CLIP_RATIO = 12,
    FWI_ISP_DENOISE_3D_BRIGHT_DIFF_CLIP_RATIO = 13,
    FWI_ISP_DENOISE_3D_BRIGHT_DIFF_RATIO = 14,
    FWI_ISP_DENOISE_3D_MV_ORI_RATIO = 15,
    FWI_ISP_DENOISE_3D_ST_2D_RATIO = 16,
    FWI_ISP_DENOISE_3D_C_WEIGHT1 = 17,
    FWI_ISP_DENOISE_3D_C_WEIGHT2 = 18,
    FWI_ISP_DENOISE_3D_C_WEIGHT3 = 19,
    FWI_ISP_DENOISE_3D_LTF_UPDATE_FRAME = 20,
    FWI_ISP_DENOISE_3D_LTF_EN = 21,
    FWI_ISP_DENOISE_3D_MAX = 22,
} fwi_denoise_3d_cfg_e;
typedef enum fwi_trigger_type_e {
    FWI_ISP_TRIGGER_BY_LUMINANCE_INDEX = 0,
    FWI_ISP_TRIGGER_BY_GAIN_INDEX = 1,
    FWI_ISP_TRIGGER_MAX = 2,
} fwi_trigger_type_e;
typedef enum fwi_light_mode_e {
    FWI_NORMAL_LIGHT = 0,
    FWI_HI_LIGHT_PRIORITY = 1,
    FWI_LOW_LIGHT_PRIORITY = 2,
} fwi_light_mode_e;
typedef enum fwi_pltm_dynamic_cfg_e {
    FWI_ISP_PLTM_DYNAMIC_AUTO_STRENGTH = 0,
    FWI_ISP_PLTM_DYNAMIC_MANUAL_STRENGTH = 1,
    FWI_ISP_PLTM_DYNAMIC_AE_COMP = 2,
    FWI_ISP_PLTM_DYNAMIC_MIN_THRESHOLD = 3,
    FWI_ISP_PLTM_DYNAMIC_MAX = 4,
} fwi_pltm_dynamic_cfg_e;
typedef enum fwi_saturation_mode_e {
    FWI_SATURATION_NORM_MODE = 0,
    FWI_SATURATION_STRONG_MODE = 1,
} fwi_saturation_mode_e;
typedef enum fwi_sensor_offset_e {
    FWI_ISP_SENSOR_OFFSET_R_OFFSET = 0,
    FWI_ISP_SENSOR_OFFSET_GR_OFFSET = 1,
    FWI_ISP_SENSOR_OFFSET_GB_OFFSET = 2,
    FWI_ISP_SENSOR_OFFSET_B_OFFSET = 3,
    FWI_ISP_SENSOR_OFFSET_MAX = 4,
} fwi_sensor_offset_e;
typedef enum fwi_wdr_cfg_type_e {
    FWI_WDR_EXPOSURE_RATIO = 0,
    FWI_WDR_LOW_THRESHOLD = 1,
    FWI_WDR_HI_THRESHOLD = 2,
    FWI_WDR_MODE = 3,
    FWI_ISP_WDR_CFG_MAX = 4,
} fwi_wdr_cfg_type_e;
typedef enum fwi_wdr_mode_e {
    FWI_DOL_WDR = 0,
    FWI_COMMANDING_WDR = 1,
} fwi_wdr_mode_e;
typedef enum fwi_wdr_output_mode_e {
    FWI_WDR_OUTPUT_STITCH = 0,
    FWI_WDR_OUTPUT_LONG = 1,
    FWI_WDR_OUTPUT_SHORT = 2,
    FWI_ISP_WDR_OUTPUT_MODE_MAX = 3,
} fwi_wdr_output_mode_e;
typedef enum fwi_white_balance_mode_e {
    FWI_WB_MANUAL = 0,
    FWI_WB_AUTO = 1,
    FWI_WB_INCANDESCENT = 2,
    FWI_WB_FLUORESCENT = 3,
    FWI_WB_FLUORESCENT_H = 4,
    FWI_WB_HORIZON = 5,
    FWI_WB_DAYLIGHT = 6,
    FWI_WB_FLASH = 7,
    FWI_WB_CLOUDY = 8,
    FWI_WB_SHADE = 9,
    FWI_WB_TUNGSTEN = 10,
} fwi_white_balance_mode_e;
#endif
