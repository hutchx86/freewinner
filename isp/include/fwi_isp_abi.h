// SPDX-License-Identifier: AGPL-3.0-only
#ifndef FWI_ISP_ABI_H
#define FWI_ISP_ABI_H
#include "fwi_types.h"
#include "fwi_isp_enum.h"
#include <stdio.h>
#include <pthread.h>
union fwi_ctx_lock {
    pthread_mutex_t m;
    uint8_t raw[64];
};
struct fwi_sensor_setting {
    uint8_t rsv_0[176];
} __attribute__((aligned(4)));
struct fwi_ae_table {
    uint32_t min_exposure;
    uint32_t max_exposure;
    uint32_t min_gain;
    uint32_t max_gain;
    uint32_t min_iris;
    uint32_t max_iris;
};
struct fwi_ae_table_info {
    fwi_ae_table_t ae_tbl[10];
    int32_t length;
    int32_t ev_step;
    int32_t shutter_shift;
};
struct fwi_ae_test_cfg {
    int32_t test_mode; /* copied from fwi_tuning_enables.bench_mode */
    int32_t gain; /* copied from fwi_tuning_enables.fixed_gain */
    int32_t exposure_line; /* copied from fwi_tuning_enables.fixed_exposure_lines */
    int32_t ae_forced; /* copied from fwi_tuning_enables.ae_hold */
    int32_t luminance_forced; /* copied from fwi_tuning_enables.luma_hold */
    int32_t test_exposure_time; /* copied from fwi_tuning_enables.sweep_exposure_en */
    int32_t exposure_line_start; /* copied from fwi_tuning_enables.sweep_exposure_first */
    int32_t exposure_line_step; /* copied from fwi_tuning_enables.sweep_exposure_step */
    int32_t exposure_line_end; /* copied from fwi_tuning_enables.sweep_exposure_last */
    int32_t exposure_change_interval; /* copied from fwi_tuning_enables.sweep_exposure_period */
    int32_t test_gain; /* copied from fwi_tuning_enables.sweep_gain_en */
    int32_t gain_start; /* copied from fwi_tuning_enables.sweep_gain_first */
    int32_t gain_step; /* copied from fwi_tuning_enables.sweep_gain_step */
    int32_t gain_end; /* copied from fwi_tuning_enables.sweep_gain_last */
    int32_t gain_change_interval; /* copied from fwi_tuning_enables.sweep_gain_period */
    int32_t ae_en; /* copied from fwi_tuning_enables.auto_exposure_en */
    uint8_t ae_delay_type;
    uint8_t rsv_41[3];
    int32_t ae_check_delay_en;
};
struct fwi_afs_test_cfg {
    int32_t test_mode;
    int32_t afs_en;
};
struct fwi_awb_test_cfg {
    int32_t test_mode;
    int32_t colour_temp;
    int32_t awb_en;
};
struct fwi_ev_setting {
    uint32_t ev_exposure_time;
    uint32_t ev_analog_gain;
    uint32_t ev_digital_gain;
    uint32_t ev_total_gain;
    uint32_t ev_sensor_exposure_line;
    uint32_t ev_sensor_true_exposure_line;
    uint32_t ev_f_number;
    uint32_t ev_fno2;
    uint32_t ev_av;
    uint32_t ev_tv;
    uint32_t ev_sv;
    uint32_t ev_level;
    uint32_t ev;
    int32_t ev_index;
};
struct fwi_gain_cfg {
    int32_t gain_bias;
    int32_t ana_gain_min;
    int32_t ana_gain_max;
    int32_t dig_gain_min;
    int32_t dig_gain_max;
};
struct fwi_gtm_test_cfg {
    int test_mode;
};
struct fwi_iso_test_cfg {
    int32_t test_mode;
};
struct fwi_tuning_3a {
    int32_t define_ae_table;
    int32_t ae_max_level;
    int32_t ae_table_preview_length;
    int32_t ae_table_capture_length;
    int32_t ae_table_video_length;
    int32_t ae_table_preview[42];
    int32_t ae_table_capture[42];
    int32_t ae_table_video[42];
    int32_t ae_zone_weight[64]; /* AE 8x8 zone weights; non-zero sum required */
    int32_t ae_gain_bias;
    int32_t ae_analog_gain_range[2];
    int32_t ae_digital_gain_range[2];
    int32_t ae_gain_range[4];
    int32_t ae_hist_mode_en;
    int32_t ae_hist_select;
    int32_t ae_stat_select;
    int32_t ae_ki;
    int32_t ae_conv_data_index;
    int32_t ae_highlight_guard_en;
    int32_t ae_highlight_guard_level;
    int32_t ae_delay_frame;
    int32_t exposure_delay_frame;
    int32_t gain_delay_frame;
    int32_t exposure_comp_step;
    int32_t ae_touch_distance_ind;
    int32_t ae_iso2gain_ratio;
    int32_t ae_f_number_step[16];
    int32_t wdr_cfg[4];
    int32_t gain_ratio;
    int32_t awb_period_frames;
    int32_t awb_step_speed;
    int32_t awb_stat_select;
    int32_t awb_colour_temper_low;
    int32_t awb_colour_temper_high;
    int32_t awb_base_temper;
    int32_t awb_green_zone_distance;
    int32_t awb_blue_sky_distance;
    int32_t awb_illum_count;
    int32_t awb_extra_illum_count;
    int32_t awb_skin_count;
    int32_t awb_special_count;
    int32_t awb_illum[32][10];
    int32_t awb_extra_illum[32][10];
    int32_t awb_skin[16][10];
    int32_t awb_special[32][10];
    int32_t awb_preference_gain[22];
    int32_t awb_r_bias;
    int32_t awb_b_bias;
    int32_t af_use_otp;
    int32_t vcm_min_code;
    int32_t vcm_max_code;
    int32_t af_interval_time;
    int32_t af_speed_ind;
    int32_t af_auto_fine_en;
    int32_t af_single_fine_en;
    int32_t af_fine_step;
    int32_t af_move_count;
    int32_t af_still_count;
    int32_t af_move_monitor_count;
    int32_t af_still_monitor_count;
    int32_t af_stable_min;
    int32_t af_stable_max;
    int32_t af_low_light_level;
    int32_t af_near_tolerance;
    int32_t af_far_tolerance;
    int32_t af_tolerance_off;
    int32_t af_peak_threshold;
    int32_t af_dir_threshold;
    int32_t af_change_ratio;
    int32_t af_move_minus;
    int32_t af_still_minus;
    int32_t af_scene_motion_threshold;
    int32_t af_tolerance_tbl_len;
    int32_t af_std_code_tbl[20];
    int32_t af_tolerance_value_tbl[20];
};
struct fwi_adjust_setting {
    int32_t contrast;
    int32_t brightness;
    int32_t dehaze_value;
};
struct fwi_h3a_reg_window {
    uint8_t horizontal_count;
    uint8_t vertical_count;
    uint8_t rsv_2[2];
    uint32_t width;
    uint32_t height;
    uint32_t horizontal_start;
    uint32_t vertical_start;
};
struct fwi_ae_cfg {
    fwi_h3a_reg_window_t ae_reg_window;
};
struct fwi_ae_core_ops {
    int32_t (*ae_set_params)(void *, fwi_ae_param_t *, fwi_ae_result_t *);
    int32_t (*ae_get_params)(void *, fwi_ae_param_t * *);
    int32_t (*ae_run)(void *, fwi_ae_stats_t *, fwi_ae_result_t *);
};
struct fwi_ae_stats_desc {
    fwi_ae_stats_t *ae_stats;
};
struct fwi_wdr_ratio {
    int32_t sensor;
    int32_t hardware;
    int32_t tmp;
    int32_t last;
};
struct fwi_ae_result {
    uint8_t ae_status;
    uint8_t rsv_1[3];
    fwi_sensor_setting_t sensor_set;
    fwi_sensor_setting_t sensor_set_short;
    int32_t bright_pixel_value;
    int32_t dark_pixel_value;
    uint32_t ae_gain;
    int32_t ae_target;
    int32_t ae_average_luminance;
    int32_t ae_weight_luminance;
    int32_t ae_delta_exposure_index;
    int32_t ev_level_adj;
    int32_t ae_flash_ev_cumulative;
    uint32_t ae_flash_ok;
    uint32_t ae_flash_led;
    fwi_wdr_ratio_t ae_wdr_ratio;
    int32_t ae_wdr_delay;
    int32_t wdr_hi_threshold;
    int32_t wdr_low_threshold;
    uint16_t hist_low;
    uint16_t hist_mid;
    uint16_t hist_hi;
    uint8_t backlight;
    uint8_t rsv_1b3[5];
    double gain_ratio;
};
struct fwi_ae_entity {
    fwi_ae_param_t *ae_param;
    fwi_ae_stats_desc_t ae_stats;
    fwi_ae_result_t ae_result;
    fwi_ae_core_ops_t *ops;
    void *ae_entity;
};
struct fwi_ae_init_cfg {
    int32_t define_ae_table;
    int32_t ae_max_level;
    int32_t ae_zone_weight[64];
    int32_t ae_hist_mode_en;
    int32_t ae_ki;
    int32_t ae_highlight_guard_en;
    int32_t ae_highlight_guard_level;
    int32_t ae_conv_data_index;
    int32_t ae_delay_frame;
    int32_t exposure_delay_frame;
    int32_t gain_delay_frame;
    int32_t exposure_comp_step;
    int32_t ae_touch_distance_ind;
    int32_t ae_handle_high_fps_en;
    int32_t ae_iso2gain_ratio;
    int32_t ae_f_number_step[16];
    int32_t wdr_cfg[4];
    int32_t ae_gain_bias;
    int32_t ae_gain_range[4];
    int32_t ae_total_gain_range[2];
    int32_t ae_digital_gain_range[2];
    int32_t ae_analog_gain_range[2];
    fwi_ae_table_info_t ae_tbl_scene[16];
    uint8_t rsv_1174[4];
    double gain_ratio;
};
struct fwi_h3a_coord_window {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
};
struct fwi_ae_controls {
    int32_t exposure_compensation;
    uint32_t exposure_absolute;
    int32_t sensor_gain;
    int32_t iso_sensitivity;
    int32_t iris_f_number;
    uint8_t ae_mode;
    uint8_t flash_mode;
    uint8_t exposure_mode;
    uint8_t light_mode;
    uint8_t exposure_metering_mode;
    uint8_t rsv_19[3];
    fwi_h3a_coord_window_t ae_coord;
    fwi_h3a_coord_window_t hist_coord;
    uint8_t iso_mode;
    uint8_t flicker_mode;
    uint8_t flicker_type;
    uint8_t scene_mode;
    uint8_t wdr_output_select;
    bool exposure_lock;
    uint8_t rsv_42[2];
    int32_t exposure_cfg[14];
    int32_t ae_hist_eq_cfg[13];
    int32_t pltm_dynamic_cfg[4];
    int32_t flash_open;
    bool flash_switch_flag;
    uint8_t rsv_c5[3];
    int32_t take_picture_flag;
    int32_t take_pic_start_count;
};
struct fwi_bayer_gain_offset {
    uint16_t r_gain;
    uint16_t gr_gain;
    uint16_t gb_gain;
    uint16_t b_gain;
    int16_t r_offset;
    int16_t gr_offset;
    int16_t gb_offset;
    int16_t b_offset;
};
struct fwi_sensor_state {
    char *name;
    int32_t hflip;
    int32_t vflip;
    uint32_t hts;
    uint32_t vts;
    uint32_t pclk;
    uint32_t fps_fixed;
    uint32_t bin_factor;
    uint32_t gain_min;
    uint32_t gain_max;
    uint32_t exposure_min;
    uint32_t exposure_max;
    int32_t sensor_width;
    int32_t sensor_height;
    uint32_t hoffset;
    uint32_t voffset;
    uint32_t input_seq; /* value enum fwi_input_seq_e; default FWI_INPUT_SEQ_BGGR */
    uint32_t wdr_mode;
    uint32_t colour_space;
    uint32_t exposure_line;
    uint32_t analog_gain;
    uint32_t dig_gain;
    uint32_t total_gain;
    uint32_t ae_tbl_index;
    uint32_t ae_tbl_index_max;
    uint32_t fps;
    uint32_t frame_time;
    int32_t ae_gain;
    uint8_t is_ae_done;
    uint8_t backlight;
    uint8_t rsv_72[2];
    int32_t motion_flag;
    int32_t ae_level;
    fwi_bayer_gain_offset_t gain_offset;
    int32_t is_af_busy;
};
struct fwi_ae_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int32_t platform_id;
    int32_t ae_frame_id;
    int32_t ae_isp_id;
    fwi_ae_init_cfg_t ae_init;
    fwi_ae_controls_t ae_setting;
    int32_t ae_pline_index;
    int32_t sensor_update_done;
    fwi_sensor_state_t ae_sensor_info;
    fwi_ae_test_cfg_t test_cfg;
    int ae_target_comp;
    int commanding_input_bits;
    int commanding_output_bits;
    bool nor_cmd_mode;
    uint8_t rsv_134d[3];
};
struct fwi_ae_stats {
    uint32_t window_pixel_n;
    uint32_t accum_r[16][24];
    uint32_t accum_g[16][24];
    uint32_t accum_b[16][24];
    uint32_t average[384];
    uint32_t hist[256];
};
struct fwi_af_en_cfg {
    uint8_t af_iir0_en;
    uint8_t af_fir0_en;
    uint8_t af_iir0_sec0_en;
    uint8_t af_iir0_sec1_en;
    uint8_t af_iir0_sec2_en;
    uint8_t af_iir0_ldg_en;
    uint8_t af_fir0_ldg_en;
    uint8_t af_iir_downsample_en;
    uint8_t af_fir_downsample_en;
    uint8_t af_offset_en;
    uint8_t af_peak_en;
    uint8_t af_squ_en;
};
struct fwi_af_filter_cfg {
    int16_t af_iir0_g0;
    int16_t af_iir0_g1;
    int16_t af_iir0_g2;
    int16_t af_iir0_g3;
    int16_t af_iir0_g4;
    int16_t af_iir0_g5;
    uint16_t af_iir0_s0;
    uint16_t af_iir0_s1;
    uint16_t af_iir0_s2;
    uint16_t af_iir0_s3;
    char af_fir0_g0;
    char af_fir0_g1;
    char af_fir0_g2;
    char af_fir0_g3;
    char af_fir0_g4;
    uint8_t af_iir0_dilate;
    uint8_t af_iir0_ldg_low_gain;
    uint8_t af_iir0_ldg_high_gain;
    uint8_t af_iir0_ldg_low_threshold;
    uint8_t af_iir0_ldg_high_threshold;
    uint8_t af_fir0_ldg_low_gain;
    uint8_t af_fir0_ldg_high_gain;
    uint8_t af_fir0_ldg_low_threshold;
    uint8_t af_fir0_ldg_high_threshold;
    uint8_t af_iir0_ldg_low_slope;
    uint8_t af_iir0_ldg_high_slope;
    uint8_t af_fir0_ldg_low_slope;
    uint8_t af_fir0_ldg_high_slope;
    uint8_t af_iir0_core_threshold;
    uint8_t af_iir0_core_peak;
    uint8_t af_fir0_core_threshold;
    uint8_t af_fir0_core_peak;
    uint8_t af_iir0_core_slope;
    uint8_t af_fir0_core_slope;
    uint8_t af_hlt_threshold;
    uint8_t rsv_2d[1];
    int16_t af_r_offset;
    int16_t af_g_offset;
    int16_t af_b_offset;
};
struct fwi_af_cfg {
    uint16_t af_sap_limit;
    uint8_t rsv_2[2];
    fwi_h3a_reg_window_t af_reg_window;
    uint8_t af_mode;
    fwi_af_en_cfg_t af_en_cfg;
    uint8_t rsv_25[1];
    fwi_af_filter_cfg_t af_filter_cfg;
    uint8_t af_square_lut[16];
    uint8_t rsv_6a[2];
};
struct fwi_af_core_ops {
    int32_t (*af_set_params)(void *, fwi_af_param_t *, fwi_af_result_t *);
    int32_t (*af_get_params)(void *, fwi_af_param_t * *);
    int32_t (*af_run)(void *, fwi_af_stats_t *, fwi_af_result_t *);
};
struct fwi_af_stats_desc {
    fwi_af_stats_t *af_stats;
};
struct fwi_af_result {
    uint8_t af_status_output;
    uint8_t rsv_1[3];
    uint32_t last_code_output;
    uint32_t real_code_output;
    uint32_t std_code_output;
    uint16_t af_sap_limit_output;
    uint8_t rsv_12[2];
    uint32_t af_sharp_output;
};
struct fwi_af_entity {
    fwi_af_param_t *af_param;
    fwi_af_stats_desc_t af_stats;
    fwi_af_result_t af_result;
    fwi_af_core_ops_t *ops;
    void *af_entity;
};
struct fwi_af_init_cfg {
    int32_t af_use_otp;
    int32_t vcm_min_code;
    int32_t vcm_max_code;
    int32_t af_interval_time;
    int32_t af_speed_ind;
    int32_t af_auto_fine_en;
    int32_t af_single_fine_en;
    int32_t af_fine_step;
    int32_t af_move_count;
    int32_t af_still_count;
    int32_t af_move_monitor_count;
    int32_t af_still_monitor_count;
    int32_t af_stable_min;
    int32_t af_stable_max;
    int32_t af_low_light_level;
    int32_t af_near_tolerance;
    int32_t af_far_tolerance;
    int32_t af_tolerance_off;
    int32_t af_peak_threshold;
    int32_t af_dir_threshold;
    int32_t af_change_ratio;
    int32_t af_move_minus;
    int32_t af_still_minus;
    int32_t af_scene_motion_threshold;
    int32_t af_tolerance_tbl_len;
    int32_t af_std_code_tbl[20];
    int32_t af_tolerance_value_tbl[20];
};
struct fwi_vcm_para {
    int32_t vcm_max_code;
    int32_t vcm_min_code;
};
struct fwi_af_test_cfg {
    int32_t test_mode;
    int32_t test_focus;
    int32_t focus_start;
    int32_t focus_step;
    int32_t focus_end;
    int32_t focus_change_interval;
    int32_t af_en;
};
struct fwi_af_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int32_t platform_id;
    int32_t af_frame_id;
    fwi_af_init_cfg_t af_init;
    int32_t focus_absolute;
    int32_t focus_relative;
    uint8_t af_run_mode;
    uint8_t af_metering_mode;
    uint8_t af_range;
    uint8_t rsv_11b[1];
    fwi_vcm_para_t vcm;
    bool focus_lock;
    uint8_t rsv_125[3];
    fwi_sensor_state_t sensor_info;
    fwi_af_test_cfg_t test_cfg;
    int32_t auto_focus_trigger;
    int32_t mov;
};
struct fwi_af_settings {
    int32_t focus_absolute;
    int32_t focus_relative;
    uint8_t af_mode;
    uint8_t af_metering_mode;
    uint8_t af_range;
    bool focus_lock;
    fwi_h3a_coord_window_t af_coord;
};
struct fwi_af_stats {
    uint64_t af_iir[16][24];
    uint64_t af_fir[16][24];
    uint64_t af_iir_count[16][24];
    uint64_t af_fir_count[16][24];
    uint64_t af_hlt_count[16][24];
    uint64_t af_count[16][24];
    uint64_t af_h_d1[16][24];
    uint64_t af_h_d2[16][24];
    uint64_t af_v_d1[16][24];
    uint64_t af_v_d2[16][24];
};
struct fwi_afs_cfg {
    uint32_t inc_line;
};
struct fwi_afs_core_ops {
    int32_t (*afs_set_params)(void *, fwi_afs_param_t *, fwi_afs_result_t *);
    int32_t (*afs_get_params)(void *, fwi_afs_param_t * *);
    int32_t (*afs_run)(void *, fwi_afs_stats_t *, fwi_afs_result_t *);
};
struct fwi_afs_stats_desc {
    fwi_afs_stats_t *afs_stats;
};
struct fwi_afs_result {
    uint8_t flicker_type_output;
};
struct fwi_afs_entity {
    fwi_afs_param_t *afs_param;
    fwi_afs_stats_desc_t afs_stats;
    fwi_afs_result_t afs_result;
    uint8_t rsv_9[3];
    fwi_afs_core_ops_t *ops;
    void *afs_entity;
};
struct fwi_afs_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int32_t platform_id;
    int32_t afs_frame_id;
    int32_t auto_afs_flag;
    int32_t flicker_ratio;
    int32_t flicker_type_init;
    fwi_sensor_state_t afs_sensor_info;
    fwi_afs_test_cfg_t test_cfg;
    uint8_t flicker_mode;
    uint8_t rsv_b1[3];
};
struct fwi_afs_stats {
    uint32_t pic_width;
    uint32_t pic_height;
    uint32_t afs_sum[128];
};
struct fwi_awb_cfg {
    uint16_t awb_r_saturation_limit;
    uint16_t awb_g_saturation_limit;
    uint16_t awb_b_saturation_limit;
    uint8_t rsv_6[2];
    fwi_h3a_reg_window_t awb_reg_window;
};
struct fwi_awb_core_ops {
    int32_t (*awb_set_params)(void *, fwi_awb_param_t *, fwi_awb_result_t *);
    int32_t (*awb_get_params)(void *, fwi_awb_param_t * *);
    int32_t (*awb_run)(void *, fwi_awb_stats_t *, fwi_awb_result_t *);
};
struct fwi_awb_stats_desc {
    fwi_awb_stats_t *awb_stats;
};
struct fwi_wb_gain {
    uint16_t r_gain;
    uint16_t gr_gain;
    uint16_t gb_gain;
    uint16_t b_gain;
};
struct fwi_awb_result {
    fwi_wb_gain_t wb_gain_output;
    int32_t colour_temp_output;
};
struct fwi_awb_entity {
    fwi_awb_param_t *awb_param;
    fwi_awb_stats_desc_t awb_stats;
    fwi_awb_result_t awb_result;
    fwi_awb_core_ops_t *ops;
    void *awb_entity;
};
struct fwi_awb_init_cfg {
    int32_t awb_period_frames;
    int32_t awb_step_speed;
    int32_t awb_colour_temper_low;
    int32_t awb_colour_temper_high;
    int32_t awb_base_temper;
    int32_t awb_green_zone_distance;
    int32_t awb_blue_sky_distance;
    int32_t awb_illum_count;
    int32_t awb_extra_illum_count;
    int32_t awb_skin_count;
    int32_t awb_special_count;
    int32_t awb_illum[320];
    int32_t awb_extra_illum[320];
    int32_t awb_skin[160];
    int32_t awb_special[320];
    int32_t awb_preference_gain[22];
    int32_t awb_r_bias;
    int32_t awb_b_bias;
};
struct fwi_awb_setting {
    uint8_t wb_mode;
    uint8_t rsv_1[3];
    int32_t wb_temperature;
    bool white_balance_lock;
    uint8_t rsv_9[1];
    fwi_wb_gain_t wb_gain_manual;
    uint8_t rsv_12[2];
    fwi_h3a_coord_window_t awb_coord;
};
struct fwi_awb_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int32_t platform_id;
    int32_t awb_frame_id;
    fwi_awb_setting_t awb_ctrl;
    fwi_awb_init_cfg_t awb_init;
    fwi_sensor_state_t awb_sensor_info;
    fwi_awb_test_cfg_t test_cfg;
};
struct fwi_awb_stats {
    uint32_t awb_sum_r[32][32];
    uint32_t awb_sum_g[32][32];
    uint32_t awb_sum_b[32][32];
    uint32_t awb_sum_count[32][32];
    uint32_t awb_average_r[32][32];
    uint32_t awb_average_g[32][32];
    uint32_t awb_average_b[32][32];
    uint32_t average[32][32];
};
struct fwi_colour_enhance_cfg {
    uint8_t colour_enhance_table[5888];
};
struct fwi_demosaic_cfg {
    int32_t min_rgb;
    uint16_t dir_threshold;
    uint8_t interp_mode;
    uint8_t zig_zag;
};
struct fwi_chroma_denoise_cfg {
    uint16_t c_threshold;
    uint16_t y_threshold;
    uint16_t st_v_yth;
    uint16_t st_h_yth;
};
struct fwi_contrast_cfg {
    uint16_t contrast_min_val;
    uint16_t contrast_max_val;
    uint16_t black_clip;
    uint16_t white_clip;
    uint16_t black_level;
    uint16_t white_level;
    uint16_t plat_threshold;
    uint16_t contrast_val[33];
    uint16_t contrast_luminance[33];
};
struct fwi_crosstalk_cfg {
    uint16_t crosstalk_threshold_max;
    uint16_t crosstalk_threshold_min;
    uint16_t crosstalk_threshold_slope;
    uint16_t crosstalk_dir_wt;
    uint16_t crosstalk_dir_threshold;
};
struct fwi_ctx_callbacks {
    void (*ae_done)(fwi_isp_ctx_t *, fwi_ae_result_t *);
    void (*af_done)(fwi_isp_ctx_t *, fwi_af_result_t *);
    void (*awb_done)(fwi_isp_ctx_t *, fwi_awb_result_t *);
    void (*afs_done)(fwi_isp_ctx_t *, fwi_afs_result_t *);
    void (*md_done)(fwi_isp_ctx_t *, fwi_md_result_t *);
    void (*pltm_done)(fwi_isp_ctx_t *, fwi_pltm_result_t *);
};
struct fwi_d2d_cfg {
    uint8_t lf_ratio;
    uint8_t bf_ratio;
    uint8_t hf_ratio;
    uint8_t lp0_np_core_ratio;
    uint8_t lp1_np_core_ratio;
    uint8_t lp2_np_core_ratio;
    uint8_t lp3_np_core_ratio;
    uint8_t lp0_np_side_ratio;
    uint8_t lp1_np_side_ratio;
    uint8_t lp2_np_side_ratio;
    uint8_t lp0_pcnt_ratio;
    uint8_t lp1_pcnt_ratio;
    uint8_t lp2_pcnt_ratio;
    uint8_t lp3_pcnt_ratio;
    uint16_t d2d_lp0_threshold[33];
    uint16_t d2d_lp1_threshold[33];
    uint16_t d2d_lp2_threshold[33];
    uint16_t d2d_lp3_threshold[33];
    uint16_t bayer_denoise_threshold[33];
};
struct fwi_dehaze_context {
    int32_t min_rgb_pre[8];
    int32_t dehaze_pre;
    int32_t dehaze_changed;
};
struct fwi_dg_gain {
    uint16_t r_gain;
    uint16_t gr_gain;
    uint16_t gb_gain;
    uint16_t b_gain;
};
struct fwi_disc_cfg {
    uint16_t disc_ct_x;
    uint16_t disc_ct_y;
    uint16_t disc_rs_val;
};
struct fwi_defect_pixel_cfg {
    uint8_t hot_ratio;
    uint8_t cold_ratio;
    uint8_t nbhd_diff_ratio;
    uint8_t nearest_diff_ratio;
    uint16_t slope_threshold : 10;
    uint16_t cold_abs_threshold : 9;
};
struct fwi_drc_cfg {
    uint16_t drc_table[256];
    uint16_t drc_table_last[256];
};
struct fwi_size {
    uint32_t width;
    uint32_t height;
};
struct fwi_driver_to_3a_stat {
    fwi_size_t pic_size;
    int32_t min_rgb_saved;
    int32_t c_noise_saved;
};
struct fwi_dynamic_cfg {
    int32_t sharp_cfg[29];
    int32_t contrast_cfg[11];
    int32_t denoise_cfg[20];
    int32_t sensor_offset[4];
    int32_t black_level[4];
    int32_t defect_pixel_cfg[6];
    int32_t pltm_dynamic_cfg[4];
    int32_t dehaze_value;
    int32_t brightness;
    int32_t contrast;
    int32_t saturation_cb;
    int32_t saturation_cr;
    int32_t saturation_cfg[7];
    int32_t colour_enhance_ratio;
    int32_t denoise_3d_cfg[22];
    int32_t colour_denoise;
    int32_t ae_cfg[14];
    int32_t gtm_cfg[9];
    int32_t lateral_ca_cfg[11];
};
struct fwi_dynamic_judge_stats {
    uint32_t accum[16][24];
    uint32_t accum_last1[16][24];
    uint32_t accum_last2[16][24];
    uint32_t accum_last3[16][24];
    int32_t mov_save[7];
    int32_t mov_threshold[6];
    int32_t temporal_denoise_comp[4];
    int32_t temporal_denoise_diff_comp[4];
    int32_t lp_threshold_ratio_comp[4];
    int32_t sharp_high_frequency_comp[4];
    int32_t sharp_edge_comp[4];
    int32_t sharp_under_shoot_comp[4];
    int32_t temporal_denoise_comp_target;
    int32_t temporal_denoise_diff_comp_target;
    int32_t lp_threshold_ratio_comp_target;
    int32_t sharp_high_frequency_comp_target;
    int32_t sharp_edge_comp_target;
    int32_t sharp_under_shoot_comp_target;
    int32_t mov;
    int32_t mov_old;
    bool enable;
    uint8_t rsv_18b5[3];
};
struct fwi_tuning_by_iso {
    uint8_t trigger_selectors[17];
    uint8_t rsv_11[3];
    int32_t luminance_mapping_point[14];
    int32_t gain_mapping_point[14];
    fwi_dynamic_cfg_t dynamic_cfg[14];
};
struct fwi_offset {
    int16_t r_offset;
    int16_t gr_offset;
    int16_t gb_offset;
    int16_t b_offset;
};
struct fwi_gain_offset_cfg {
    fwi_offset_t offset;
    fwi_dg_gain_t gain;
    fwi_offset_t sensor_offset;
};
struct fwi_gamma_cfg {
    uint16_t gamma_tbl[3072];
};
struct fwi_green_ca_cfg {
    uint16_t green_ca_ct_h;
    uint16_t green_ca_ct_w;
    uint16_t green_ca_r_para0;
    uint16_t green_ca_r_para1;
    uint16_t green_ca_r_para2;
    uint16_t green_ca_b_para0;
    uint16_t green_ca_b_para1;
    uint16_t green_ca_b_para2;
    uint16_t green_ca_int_cns;
};
struct fwi_gtm_core_ops {
    int (*gtm_set_params)(void *, fwi_gtm_param_t *, fwi_gtm_result_t *);
    int (*gtm_get_params)(void *, fwi_gtm_param_t * *);
    int (*gtm_run)(void *, fwi_gtm_stats_t *, fwi_gtm_result_t *);
};
struct fwi_gtm_stats_desc {
    fwi_gtm_stats_t *gtm_stats;
};
struct fwi_gtm_result {
    uint16_t hist_max_val;
    uint16_t average_luminance;
    uint16_t average_var;
    uint16_t hist_div;
    double hratio_last;
    int32_t hdr_req;
    uint8_t rsv_14[4];
};
struct fwi_gtm_entity {
    fwi_gtm_param_t *gtm_param;
    fwi_gtm_stats_desc_t gtm_stats;
    fwi_gtm_result_t gtm_result;
    fwi_gtm_core_ops_t *ops;
    void *gtm_entity;
};
struct fwi_gtm_init_cfg {
    uint8_t gtm_type;
    uint8_t gamma_type;
    uint8_t rsv_2[2];
    uint32_t auto_alpha_en;
    int32_t hist_pixel_count;
    int32_t bright_minval;
    int32_t dark_minval;
    int16_t plum_var[9][9];
    uint8_t rsv_b6[2];
    int32_t gtm_cfg[9];
};
struct fwi_gtm_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int platform_id;
    int gtm_frame_id;
    uint8_t gtm_enable;
    uint8_t rsv_d[3];
    fwi_gtm_init_cfg_t gtm_init;
    int contrast;
    int brightness;
    int gtm_bit_offset;
    bool wdr_en;
    uint8_t rsv_f9[3];
    int bright_pixel_value;
    int dark_pixel_value;
    uint16_t *gamma_tbl;
    uint16_t *drc_table;
    uint16_t *drc_table_last;
    fwi_gtm_test_cfg_t test_cfg;
};
struct fwi_gtm_stats {
    uint32_t window_pixel_n;
    uint32_t accum_r[16][24];
    uint32_t accum_g[16][24];
    uint32_t accum_b[16][24];
    uint32_t average[384];
    uint32_t hist[256];
};
struct fwi_hist_cfg {
    int32_t hist_threshold;
    uint8_t hist_mode;
    uint8_t rsv_5[3];
    fwi_h3a_reg_window_t hist_reg_window;
};
struct fwi_to_user_params {
    union {
        uint32_t image_flags;
        struct {
            uint32_t af_sharpness : 16;
            uint32_t hdr_frame_count : 4;
            uint32_t flash_ok : 1;
            uint32_t capture_ok : 1;
            uint32_t fast_capture_ok : 1;
            uint32_t rsv_flags : 9;
        };
    };
};
struct fwi_user_to_isp_params {
    union {
        uint32_t image_flags;
        struct {
            uint32_t af_sharpness : 16;
            uint32_t hdr_frame_count : 4;
            uint32_t flash_ok : 1;
            uint32_t capture_ok : 1;
            uint32_t fast_capture_ok : 1;
            uint32_t rsv_flags : 9;
        };
    };
};
struct fwi_image_params {
    fwi_to_user_params_t image_params;
    fwi_user_to_isp_params_t user_image_params;
};
struct fwi_iso_cfg_core_ops {
    int32_t (*iso_set_params)(void *, fwi_iso_param_t *, fwi_iso_result_t *);
    int32_t (*iso_get_params)(void *, fwi_iso_param_t * *);
    int32_t (*iso_run)(void *, fwi_iso_result_t *);
};
struct fwi_iso_result {
    uint32_t gain_index;
    uint32_t luminance_index;
};
struct fwi_iso_entity {
    fwi_iso_param_t *iso_param;
    fwi_iso_result_t iso_result;
    fwi_iso_core_ops_t *ops;
    void *iso_entity;
};
struct fwi_lateral_ca_cfg {
    uint16_t lateral_ca_gf_cor_ratio;
    uint16_t lateral_ca_pf_cor_ratio;
    uint16_t lateral_ca_luminance_threshold;
    uint16_t lateral_ca_grad_threshold;
    uint16_t lateral_ca_clr_gth;
    uint16_t lateral_ca_pf_rshf;
    uint16_t lateral_ca_pf_bslp;
    uint8_t lateral_ca_clrs_luminance_threshold;
    uint8_t lateral_ca_pf_clrc_ratio;
    uint8_t lateral_ca_gf_clrc_ratio;
    uint8_t lateral_ca_pf_decr_ratio;
    uint8_t lateral_ca_pf_saturation_lut[33];
    uint8_t lateral_ca_gf_saturation_lut[33];
};
struct fwi_lens_shading_cfg {
    uint16_t ct_x;
    uint16_t ct_y;
    uint16_t rs_val;
};
struct fwi_lens_cfg {
    fwi_lens_shading_cfg_t lens_shading_cfg;
    uint16_t lens_r_table[256];
    uint16_t lens_g_table[256];
    uint16_t lens_b_table[256];
};
struct fwi_tune_setting {
    int32_t contrast_level;
    int32_t saturation_level;
    int32_t sharpness_level;
    int32_t brightness_level;
    int32_t denoise_level;
    int32_t hue_level;
    int32_t pltmwdr_level;
    int32_t denoise_3d_level;
    int32_t highlight_level;
    int32_t backlight_level;
    fwi_gain_cfg_t gains;
    uint8_t effect;
    uint8_t rsv_3d[3];
};
struct fwi_mode_cfg {
    uint32_t input_fmt;
    uint32_t wdr_mode;
    uint32_t wdr_cmp_mode;
    uint32_t on_the_fly_defect_pixel_mode;
    uint32_t saturation_mode;
    uint32_t hist_mode;
    uint32_t hist_select;
    uint32_t demosaic_mode;
    uint32_t ae_mode;
    uint32_t awb_mode;
    uint32_t dg_mode;
    uint32_t rsc_mode;
    uint32_t mesh_shading_mode;
};
struct fwi_sharp_cfg {
    uint16_t edge_black_strength;
    uint16_t edge_white_strength;
    uint16_t high_frequency_black_strength;
    uint16_t high_frequency_white_strength;
    uint8_t edge_scale_ratio;
    uint8_t high_frequency_scale_ratio;
    uint8_t edge_conv_para;
    uint8_t high_frequency_conv_para;
    uint16_t ns_lw_threshold;
    uint16_t ns_hi_threshold;
    uint16_t dir_clip_val;
    uint16_t dir_eq_ratio;
    uint8_t edge_threshold;
    uint8_t hv_edge_smoothing_ratio;
    uint8_t aa_edge_smoothing_ratio;
    uint8_t rsv_17[1];
    uint16_t over_val_ctrl;
    uint16_t over_area_ctrl;
    uint16_t under_val_ctrl;
    uint16_t under_area_ctrl;
    uint16_t sharp_edge_luminance[33];
    uint16_t sharp_high_frequency_luminance[33];
    uint16_t sharp_hsv[46];
    uint8_t sharp_s_map[33];
    uint8_t rsv_121[1];
    uint16_t sharp_val[33];
    uint16_t sharp_luminance[33];
};
struct fwi_pltm_cfg {
    uint8_t lss_switch;
    uint8_t cal_en;
    uint8_t frame_smoothing_en;
    uint8_t last_order_ratio;
    uint8_t tr_order;
    uint8_t original_picture_ratio;
    uint8_t intens_asym;
    uint8_t spatial_asm;
    uint8_t white_level;
    uint8_t lp_halo_res;
    uint8_t luminance_ratio;
    uint8_t block_height;
    uint8_t block_width;
    uint8_t block_v_count;
    uint8_t block_h_count;
    uint8_t rsv_f[1];
    uint32_t statistic_div;
    uint8_t pltm_table[1536];
};
struct fwi_wdr_cfg {
    uint16_t wdr_low_threshold;
    uint16_t wdr_hi_threshold;
    uint16_t wdr_exposure_ratio;
    uint16_t wdr_slope;
    uint16_t wdr_mv_threshold;
    uint16_t wdr_mv_scale;
    uint16_t wdr_output_select;
    uint8_t wdr_table[16384];
};
struct fwi_lut_cfg {
    int32_t lut_count;
    uint8_t lut_defect_pixel_src0_table[1024];
    uint8_t lut_defect_pixel_src1_table[1024];
};
struct fwi_mesh_shading_cfg {
    uint16_t mesh_shading_blw_lut[12];
    uint16_t mesh_shading_blh_lut[12];
    uint16_t mesh_shading_blw_delta_lut[12];
    uint16_t mesh_shading_blh_delta_lut[12];
};
struct fwi_rgb2rgb_gain_offset {
    int16_t matrix[3][3];
    int16_t offset[3];
};
struct fwi_rgb2rgb_cfg {
    fwi_rgb2rgb_gain_offset_t colour_matrix;
};
struct fwi_rgb2yuv_gain_offset {
    int16_t matrix[3][3];
    int16_t offset[3];
};
struct fwi_wb_gain_cfg {
    uint32_t clip_val;
    fwi_wb_gain_t wb_gain;
};
struct fwi_temporal_denoise_cfg {
    uint8_t rec_en;
    uint8_t rsv_1[1];
    uint16_t noise_clip_ratio;
    uint8_t bright_diff_ratio;
    uint8_t bright_diff_clip_ratio;
    uint8_t luminance_diff_clip_ratio;
    uint8_t st_2d_ratio;
    uint8_t mv_ori_ratio;
    uint8_t ltf_en;
    uint8_t k3d_increase_mode;
    uint8_t rsv_b[1];
    uint16_t c_weight1;
    uint16_t c_weight2;
    uint16_t c_weight3;
    uint16_t ltf_update_frame;
    uint16_t temporal_denoise_luminance_threshold[33];
    uint16_t temporal_denoise_bri_threshold[33];
    uint8_t temporal_denoise_k_delta[32];
    uint16_t temporal_denoise_threshold[33];
    uint16_t temporal_denoise_ref_noise[33];
    uint8_t temporal_denoise_k[32];
};
struct fwi_saturation_cfg {
    int16_t saturation_r;
    int16_t saturation_g;
    int16_t saturation_b;
    int16_t saturation_gain;
    int16_t saturation_table[256];
};
struct fwi_hw_module_cfg {
    uint32_t dev_id;
    uint32_t platform_id;
    uint32_t module_enable_flag;
    uint32_t table_update;
    fwi_mode_cfg_t mode_cfg;
    fwi_afs_cfg_t afs_cfg;
    fwi_demosaic_cfg_t demosaic_cfg;
    fwi_sharp_cfg_t sharp_cfg;
    fwi_contrast_cfg_t contrast_cfg;
    fwi_d2d_cfg_t bayer_denoise_cfg;
    fwi_pltm_cfg_t pltm_cfg;
    fwi_wdr_cfg_t wdr_cfg;
    fwi_drc_cfg_t drc_cfg;
    fwi_colour_enhance_cfg_t colour_enhance_cfg;
    uint8_t rsv_6502[2];
    fwi_lut_cfg_t lut_cfg;
    fwi_lens_cfg_t lens_cfg;
    fwi_mesh_shading_cfg_t mesh_shading_cfg;
    fwi_gamma_cfg_t gamma_cfg;
    fwi_disc_cfg_t disc_cfg;
    fwi_rgb2rgb_cfg_t rgb2rgb_cfg;
    fwi_rgb2yuv_gain_offset_t rgb2yuv;
    fwi_ae_cfg_t ae_cfg;
    fwi_af_cfg_t af_cfg;
    fwi_awb_cfg_t awb_cfg;
    fwi_hist_cfg_t hist_cfg;
    fwi_gain_offset_cfg_t gain_offset_cfg;
    fwi_wb_gain_cfg_t wb_gain_cfg;
    fwi_defect_pixel_cfg_t on_the_fly_cfg;
    fwi_crosstalk_cfg_t crosstalk_cfg;
    fwi_green_ca_cfg_t green_ca_cfg;
    fwi_lateral_ca_cfg_t lateral_ca_cfg;
    fwi_chroma_denoise_cfg_t chroma_denoise_cfg;
    fwi_temporal_denoise_cfg_t denoise_3d_cfg;
    fwi_saturation_cfg_t saturation_cfg;
    uint8_t output_speed;
    uint8_t rsv_9065[3];
    void *table_mapping1;
    void *lens_table;
    void *mesh_shading_table;
    void *gamma_table;
    void *linearize_table;
    void *wdr_table;
    void *temporal_denoise_table;
    void *pltm_table;
    void *contrast_table;
    void *table_mapping2;
    void *drc_table;
    void *saturation_table;
    void *colour_enhance_table;
    void *fe_table;
    void *bayer_table;
    void *rgb_table;
    void *yuv_table;
    void *dehaze_table;
};
struct fwi_tuning_enables {
    int32_t bench_mode;
    int32_t sweep_exposure_en;
    int32_t sweep_exposure_first;
    int32_t sweep_exposure_step;
    int32_t sweep_exposure_last;
    int32_t sweep_exposure_period;
    int32_t sweep_gain_en;
    int32_t sweep_gain_first;
    int32_t sweep_gain_step;
    int32_t sweep_gain_last;
    int32_t sweep_gain_period;
    int32_t sweep_focus_en;
    int32_t sweep_focus_first;
    int32_t sweep_focus_step;
    int32_t sweep_focus_last;
    int32_t sweep_focus_period;
    int32_t debug_log_mask;
    int32_t fixed_gain;
    int32_t fixed_exposure_lines;
    int32_t fixed_cct_k;
    int32_t ae_hold;
    int32_t luma_hold;
    int32_t manual_mode_en;
    int32_t flicker_detect_en;
    int32_t sharpen_en;
    int32_t local_contrast_en;
    int32_t denoise_2d_en;
    int32_t drc_en;
    int32_t colour_enhance_en;
    int32_t lens_shading_en;
    int32_t mesh_shading_en;
    int32_t gamma_en;
    int32_t colour_matrix_en;
    int32_t auto_exposure_en;
    int32_t auto_focus_en;
    int32_t auto_wb_en;
    int32_t histogram_en;
    int32_t black_level_en;
    int32_t sensor_offset_en;
    int32_t wb_gain_en;
    int32_t defect_pixel_en;
    int32_t demosaic_en;
    int32_t denoise_3d_en;
    int32_t chroma_denoise_en;
    int32_t lateral_ca_en;
    int32_t green_ca_en;
    int32_t saturation_en;
    int32_t dehaze_en;
    int32_t linearize_en;
    int32_t global_tone_en;
    int32_t digital_gain_en;
    int32_t local_tone_en;
    int32_t wdr_merge_en;
    int32_t crosstalk_en;
};
struct fwi_tuning_modules {
    int32_t flash_gain;
    int32_t flash_delay_frame;
    int32_t flicker_type;
    int32_t flicker_ratio;
    int32_t horizontal_visual_angle;
    int32_t vertical_visual_angle;
    int32_t focus_length;
    int32_t gamma_count;
    int32_t rolloff_ratio;
    int32_t gtm_type;
    int32_t gamma_type;
    int32_t auto_alpha_en;
    int32_t hist_pixel_count;
    int32_t dark_minval;
    int32_t bright_minval;
    int16_t plum_var[9][9];
    uint8_t rsv_de[2];
    int32_t demosaic_dir_threshold;
    int32_t demosaic_interp_mode;
    int32_t demosaic_zig_zag;
    uint16_t crosstalk_threshold_max;
    uint16_t crosstalk_threshold_min;
    uint16_t crosstalk_threshold_slope;
    uint16_t crosstalk_dir_wt;
    uint16_t crosstalk_dir_threshold;
    uint8_t rsv_f6[2];
    int32_t bayer_gain[4];
    int32_t lens_shading_mode;
    int32_t ff_mod;
    int32_t lens_shading_center_x;
    int32_t lens_shading_center_y;
    uint16_t lens_shading_tbl[12][768];
    uint16_t lens_shading_trig_cfg[6];
    int32_t mesh_shading_mode;
    int32_t mff_mod;
    int32_t mesh_shading_blw_lut[11];
    int32_t mesh_shading_blh_lut[11];
    int32_t mesh_shading_blw_delta_lut[12];
    int32_t mesh_shading_blh_delta_lut[12];
    uint16_t mesh_shading_trig_cfg[6];
    uint16_t mesh_shading_tbl[12][1452];
    uint16_t gamma_tbl_init[5][3072];
    uint16_t gamma_trig_cfg[5];
    uint16_t linearize_tbl[768];
    uint16_t disc_tbl[512];
    fwi_rgb2rgb_gain_offset_t colour_matrix_init[3];
    uint16_t colour_matrix_trigger[3];
    int32_t green_ca[7];
    uint16_t lateral_ca_pf_saturation_lut[33];
    uint16_t lateral_ca_gf_saturation_lut[33];
    int32_t local_tone_static[19];
    uint16_t bayer_denoise_threshold[33];
    uint16_t temporal_denoise_threshold[33];
    uint16_t temporal_denoise_ref_noise[33];
    uint8_t temporal_denoise_k[32];
    uint16_t contrast_val[33];
    uint16_t contrast_luminance[33];
    uint16_t sharp_val[33];
    uint16_t sharp_luminance[33];
    uint16_t sharp_edge_luminance[33];
    uint16_t sharp_high_frequency_luminance[33];
    uint16_t sharp_hsv[46];
    uint8_t sharp_s_map[33];
    uint8_t d3d_k3d_incre_curve[256];
    uint8_t temporal_denoise_diff[256];
    uint8_t rsv_15a43[1];
    uint16_t contrast_pe[128];
    uint8_t colour_enhance_table[5888];
    uint8_t colour_enhance_table1[5888];
    uint8_t pltm_table[1536];
    uint8_t wdr_table[16384];
};
struct fwi_tuning_image {
    fwi_tuning_enables_t enables;
    fwi_tuning_3a_t a3;
    fwi_tuning_modules_t modules;
    fwi_tuning_by_iso_t by_iso;
};
struct fwi_pltm_stats {
    uint16_t average_before_pltm;
    uint16_t average_after_pltm;
    uint16_t min_before_pltm;
    uint16_t max_before_pltm;
    uint16_t min_after_pltm;
    uint16_t max_after_pltm;
    uint16_t lst[768];
};
struct fwi_stats {
    fwi_ae_stats_t ae_stats;
    fwi_awb_stats_t awb_stats;
    uint8_t rsv_9c04[4];
    fwi_af_stats_t af_stats;
    fwi_afs_stats_t afs_stats;
    fwi_pltm_stats_t pltm_stats;
    uint8_t rsv_11c1c[4];
};
struct fwi_stats_ctx {
    uint32_t pic_w;
    uint32_t pic_h;
    fwi_stats_t stats;
    fwi_wb_gain_t wb_gain_saved;
    fwi_dynamic_judge_stats_t dynamic_stats;
    bool enabled;
    uint8_t rsv_134e9[7];
};
struct fwi_md_stats_desc {
    fwi_md_stats_t *md_stats;
};
struct fwi_md_result {
    int32_t motion_flag;
    uint8_t motion_dir;
    uint8_t rsv_5[3];
};
struct fwi_md_entity {
    fwi_md_param_t *md_param;
    fwi_md_stats_desc_t md_stats;
    fwi_md_result_t md_result;
    fwi_md_core_ops_t *ops;
    void *md_entity;
};
struct fwi_pltm_stats_desc {
    fwi_pltm_stats_t *pltm_stats;
};
struct fwi_pltm_result {
    int pltm_last_order_ratio;
    int pltm_tr_order;
    int pltm_original_picture_ratio;
    int pltm_cal_en;
    int pltm_frame_smoothing_en;
    int pltm_block_height;
    int pltm_block_width;
    uint32_t pltm_statistic_div;
    uint16_t pltm_tbl[768];
    uint8_t pltm_ae_comp;
    uint8_t rsv_621[1];
    uint16_t pltm_old_strength;
    uint16_t pltm_next_strength;
    uint16_t pltm_cal_strength;
    uint16_t pltm_min_threshold;
    uint8_t rsv_62a[2];
};
struct fwi_pltm_entity {
    fwi_pltm_param_t *pltm_param;
    fwi_pltm_stats_desc_t pltm_stats;
    fwi_pltm_result_t pltm_result;
    fwi_pltm_core_ops_t *ops;
    void *pltm_entity;
};
struct fwi_rolloff_stats_desc {
    fwi_stats_t *rolloff_stats;
};
struct fwi_rolloff_result {
    uint16_t lens_table_output[768];
};
struct fwi_rolloff_entity {
    fwi_rolloff_param_t *rolloff_param;
    fwi_rolloff_stats_desc_t rolloff_stats;
    fwi_rolloff_result_t rolloff_result;
    fwi_rolloff_core_ops_t *ops;
    void *rolloff_entity;
};
struct fwi_isp_ctx {
    int32_t isp_index;
    FILE *stats_log_fp;
    FILE *lib_log_fp;
    uint32_t pending_3a_changes;
    fwi_image_params_t image_params;
    fwi_ae_controls_t ae_ctl;
    fwi_af_settings_t af_ctl;
    fwi_awb_setting_t awb_ctl;
    fwi_tune_setting_t picture_ctl;
    fwi_adjust_setting_t adjust_ctl;
    uint8_t rsv_174[4];
    uint64_t awb_frame_count;
    uint64_t ae_frame_count;
    uint64_t af_frame_count;
    uint64_t gtm_frame_count;
    uint64_t md_frame_count;
    uint64_t afs_frame_count;
    uint64_t iso_frame_count;
    uint64_t rolloff_frame_count;
    uint64_t all_frame_count;
    fwi_driver_to_3a_stat_t drv_stats_ref;
    fwi_sensor_state_t sensor;
    fwi_hw_module_cfg_t hw_cfg;
    fwi_tuning_image_t tuning;
    fwi_dehaze_context_t dehaze_state;
    uint8_t rsv_29b04[4];
    fwi_stats_ctx_t stats;
    fwi_af_entity_t af_entity;
    fwi_afs_entity_t afs_entity;
    fwi_md_entity_t md_entity;
    fwi_awb_entity_t awb_entity;
    fwi_ae_entity_t ae_entity;
    fwi_gtm_entity_t gtm_entity;
    fwi_pltm_entity_t pltm_entity;
    fwi_iso_entity_t iso_entity;
    fwi_rolloff_entity_t rolloff_entity;
    const fwi_ctx_callbacks_t *callbacks;
    fwi_ctx_lock_t lock; /* union: pthread_mutex_t (<=64) plus raw[64] */
    const void *stats_buf;
    void *reg_image;
    uint32_t ir_mode;
    int otp_en;
    void *otp_shading_tbl;
    void *otp_wb_tbl;
    float shading_golden[1452];
    float shading_r_ratio;
    int shading_golden_flag[1452];
    float shading_adjust[6];
    float shading_adjust_low[6];
    float wb_golden[3];
    uint16_t inverse_gamma[4096];
    uint8_t rsv_42cbc[4];
};
struct fwi_iso_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int32_t platform_id;
    int32_t iso_frame_id;
    uint16_t colour_enhance_color2gray_threshold;
    uint16_t colour_enhance_color2gray_delta_min;
    uint16_t colour_enhance_color2gray_delta_max;
    uint8_t rsv_12[2];
    uint32_t chroma_denoise_adjust;
    uint32_t sharpness_adjust;
    uint32_t saturation_adjust;
    uint32_t contrast_adjust;
    uint32_t brightness_adjust;
    uint32_t colour_enhance_ratio_adjust;
    uint32_t denoise_adjust;
    uint32_t sensor_offset_adjust;
    uint32_t black_level_adjust;
    uint32_t defect_pixel_adjust;
    uint32_t dehaze_value_adjust;
    uint32_t pltm_dynamic_cfg_adjust;
    uint32_t tdnr_adjust;
    uint32_t ae_cfg_adjust;
    uint32_t gtm_cfg_adjust;
    uint32_t lateral_ca_cfg_adjust;
    uint32_t denoise_lp0_np_core_ratio;
    uint32_t denoise_lp1_np_core_ratio;
    uint32_t denoise_lp2_np_core_ratio;
    uint32_t denoise_lp3_np_core_ratio;
    uint32_t af_cfg_adjust;
    fwi_isp_ctx_t *gen;
    fwi_iso_test_cfg_t test_cfg;
};
struct fwi_md_core_ops {
    int32_t (*md_set_params)(void *, fwi_md_param_t *, fwi_md_result_t *);
    int32_t (*md_get_params)(void *, fwi_md_param_t * *);
    int32_t (*md_run)(void *, fwi_md_stats_t *, fwi_md_result_t *);
};
struct fwi_md_test_cfg {
    int32_t test_mode;
};
struct fwi_md_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int32_t platform_id;
    int32_t md_frame_id;
    int32_t af_scene_motion_threshold;
    fwi_sensor_state_t md_sensor_info;
    fwi_md_test_cfg_t test_cfg;
};
struct fwi_md_stats {
    uint32_t window_pixel_n;
    uint32_t accum_r[16][24];
    uint32_t accum_g[16][24];
    uint32_t accum_b[16][24];
    uint32_t average[384];
    uint32_t hist[256];
};
struct fwi_param_trigger {
    uint8_t sharp_trigger;
    uint8_t contrast_trigger;
    uint8_t denoise_trigger;
    uint8_t sensor_offset_trigger;
    uint8_t black_level_trigger;
    uint8_t defect_pixel_trigger;
    uint8_t dehaze_value_trigger;
    uint8_t pltm_dynamic_trigger;
    uint8_t brightness_trigger;
    uint8_t gcontrast_trigger;
    uint8_t saturation_trigger;
    uint8_t colour_enhance_ratio_trigger;
    uint8_t denoise_3d_trigger;
    uint8_t colour_denoise_trigger;
    uint8_t ae_cfg_trigger;
    uint8_t gtm_cfg_trigger;
    uint8_t lateral_ca_cfg_trigger;
};
struct fwi_pltm_core_ops {
    int (*pltm_set_params)(void *, fwi_pltm_param_t *, fwi_pltm_result_t *);
    int (*pltm_get_params)(void *, fwi_pltm_param_t * *);
    int (*pltm_run)(void *, fwi_pltm_stats_t *, fwi_pltm_result_t *);
};
struct fwi_pltm_init_cfg {
    int32_t pltm_cfg[19];
    int32_t pltm_dynamic_cfg[4];
};
struct fwi_pltm_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int platform_id;
    int pltm_frame_id;
    uint8_t pltm_enable;
    uint8_t ae_enable;
    uint8_t rsv_e[2];
    fwi_pltm_init_cfg_t pltm_init;
    fwi_sensor_state_t sensor_info;
    uint16_t *pltm_table;
    int wdr_bit_offset;
};
struct fwi_rolloff_core_ops {
    int32_t (*rolloff_set_params)(void *, fwi_rolloff_param_t *, fwi_rolloff_result_t *);
    int32_t (*rolloff_get_params)(void *, fwi_rolloff_param_t * *);
    int32_t (*rolloff_run)(void *, fwi_rolloff_stats_t *, fwi_rolloff_result_t *);
};
struct fwi_rolloff_init_cfg {
    int rolloff_ratio;
    uint16_t lens_table_init[768];
};
struct fwi_rolloff_param {
    uint8_t type;
    uint8_t rsv_1[3];
    int platform_id;
    int rolloff_frame_id;
    fwi_sensor_state_t rolloff_sensor_info;
    fwi_lens_shading_cfg_t lens_shading_cfg;
    uint8_t rsv_a2[2];
    fwi_rolloff_init_cfg_t rolloff_init;
};
struct fwi_reg_load {
    void *addr;
    uint32_t size;
};
struct fwi_enable_arg {
    int32_t manual_mode_en;
    int32_t flicker_detect_en;
    int32_t sharpen_en;
    int32_t local_contrast_en;
    int32_t denoise_2d_en;
    int32_t drc_en;
    int32_t colour_enhance_en;
    int32_t lens_shading_en;
    int32_t mesh_shading_en;
    int32_t gamma_en;
    int32_t colour_matrix_en;
    int32_t auto_exposure_en;
    int32_t auto_focus_en;
    int32_t auto_wb_en;
    int32_t histogram_en;
    int32_t black_level_en;
    int32_t sensor_offset_en;
    int32_t wb_gain_en;
    int32_t defect_pixel_en;
    int32_t demosaic_en;
    int32_t denoise_3d_en;
    int32_t chroma_denoise_en;
    int32_t lateral_ca_en;
    int32_t green_ca_en;
    int32_t saturation_en;
    int32_t dehaze_en;
    int32_t linearize_en;
    int32_t global_tone_en;
    int32_t digital_gain_en;
    int32_t local_tone_en;
    int32_t wdr_merge_en;
    int32_t crosstalk_en;
};
struct fwi_ccm_arg {
    uint16_t temperature;
    fwi_rgb2rgb_gain_offset_t value;
};
struct fwi_gamma_table_arg {
    int32_t number;
    uint16_t value[5][3072];
    uint16_t level_triggers[5];
    uint8_t rsv_780e[2];
};
struct fwi_sharp_table_arg {
    uint16_t value[33];
    uint16_t lum[33];
    uint16_t edge_lum[33];
    uint16_t high_frequency_lum[33];
    uint16_t hsv[46];
    uint8_t sharpen_map[33];
    uint8_t rsv_185[1];
};
struct fwi_sensor_settings {
    fwi_ev_setting_t ev_set;
    fwi_ev_setting_t ev_set_last;
    fwi_ev_setting_t ev_set_curr;
    int32_t ev_index_max;
    int32_t ev_index_expect;
};
#endif
