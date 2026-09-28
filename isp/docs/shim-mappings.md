<!-- SPDX-License-Identifier: AGPL-3.0-only -->

# Shim field mappings

How each SDK-boundary shim under `shim/` maps the framework's `fwi_*` records
onto the clean cores' own structures, and the formats of the optional debug
dumps. The shim sources point here with `docs/shim-mappings.md#<module>`.

## base

`isp/shim/base/base_shim.c` translates the SDK `struct fwi_isp_ctx` into the clean `fwi_base_ctx_t` (`include/base.h`) for each isp_base entry point, calls the renamed `clean_*` routine, then writes back only the fields that op can mutate.

### Table provider (`freeisp_get_tables()->base`)

Rebuilt from the SDK runtime tuning (`s->tuning.modules`) on every entry point unless `base_shim_set_tables()` installed an override. "locator" entries come from `base_shim_set_locator()`.

| Clean `base_tables_t` field | SDK source |
| --- | --- |
| gamma_base | gamma_tbl_init[0] |
| gamma_sub[0..3] | gamma_tbl_init[1..4] |
| gamma_trig | gamma_trig_cfg (converted to int32) |
| lsc[0..11] | lens_shading_tbl[0..11] |
| lsc_trig | lens_shading_trig_cfg |
| lsc_trig_def | locator lsc_trig_cfg_def |
| msc[0..11] | mesh_shading_tbl[0..11] |
| msc_trig | mesh_shading_trig_cfg |
| msc_trig_def | locator msc_trig_cfg_def |
| otp_msc_golden | mesh_shading_tbl[0] (NULL disables the OTP MSC path) |
| linear | linearize_tbl |
| wdr_table | wdr_table (little-endian byte image, copied to an aligned u16 view) |
| wdr_front | unused by base.c; left NULL |
| anti_gamma | locator anti_gamma_table |
| rgb2yuv_base[0..5] | locator rgb2yuv_matrix (six 12-entry colour spaces) |
| color_matrix | colour_matrix_init |
| color_temp | locator isp_cm_color_temp, else built-in {2700, 4000, 6500} K |

- gamma_trig: the vendor object falls back to a compiled-in `gamma_trig_cfg_def` when the tuning trigger array is all zero. That table has no runtime source, so the tuning array serves both paths (base.c uses it directly whenever `gamma_trig_cfg[0] != 0`, the deployed case). This is the one unmapped default.
- color_temp: a NULL table makes config_color_matrix skip interpolation and zero the whole RGB2RGB matrix (green frame).

### Input binding (`bind_sdk`)

- Frame counters: SDK u64 -> clean u32 (low word carried).
- `isp_test_settings.*_en` <- `tuning.enables` flags (auto_exposure_en, auto_wb_en, auto_focus_en, flicker_detect_en, local_tone_en, dehaze_en, lens_shading_en, mesh_shading_en, gamma_en, wdr_merge_en, digital_gain_en, linearize_en, colour_matrix_en, saturation_en, wb_gain_en); isp_test_mode <- bench_mode; isp_color_temp <- fixed_cct_k.
- `isp_test_focus` has no SDK slot (AF stats read gate) and is bound to 0, matching vendor behaviour on platforms without an AF entity.
- `ae_entity_ctx.ae_param` points at the embedded clean `ae_param` copy; `freeisp_ae_set_params` translates it back onto the SDK `ae_param` (gain_favour <-> ae_gain_bias, analog/digital gain min/max <-> ae_analog/digital_gain_range[0/1]).
- 3A statistic windows (AE/AF/HIST/AWB `*_reg_win` <- `*_reg_window`: hor_num/ver_num <- horizontal/vertical_count, hor_start/ver_start <- horizontal/vertical_start, width, height) are always seeded because APPLY_SETTINGS always writes them back. Zeroed windows mis-place the 3A stats (an AWB width of 0 packs as `((0>>1)-1) = 0xffffffff`).
- The linear/lens/MSC tables, WDR table and RGB2YUV matrix/offset are seeded from the SDK targets for the same reason. An unseeded RGB2YUV zeroes registers 0x520..0x538 (flat green picture); an unseeded WDR table zeroes the WDR block once regeneration stops (it only regenerates when flag != 0).
- `af_result.temperature` <- AWB `colour_temp_output` (the shared temperature input).

### Write-back (`store_sdk`)

| Op | Written back |
| --- | --- |
| BAND_STEP | afs_cfg.inc_line |
| LENS_CENTER | lens_shading_cfg ct_x/ct_y/rs_val, disc_cfg ct_x/ct_y/rs_val |
| DIG_GAIN | sensor gain_offset, gain_offset_cfg.gain, linearize_table |
| GAMMA | gamma_cfg.gamma_tbl |
| WDR | wdr_cfg thresholds/ratio/slope/mv/out_sel/table, ae_mode, ae_result wdr thresholds, nor_cmd_mode, inverse_gamma |
| COLORMATRIX | rgb2rgb colour_matrix matrix/offset, dehaze_state |
| JUDGE | dynamic_stats accum, *_comp arrays and targets, mov/mov_old/mov_save, af_param->mov |
| STATS / STATS_SYNC | AE/AWB/AF/AFS/PLTM stats, dynamic accum, and every entity's stats handle pointed at the SDK stats block |
| APPLY_SETTINGS | rgb2yuv, 3A windows, ae_init gains, ae_setting (on change bit 8 with spot AE), afs flicker_mode, frame counters, pending_3a_changes |
| LENS_TABLE / MSC_TABLE | lens_table / mesh_shading_table |

- The entity stats handles (awb/ae/af/md/afs/gtm/pltm/rolloff) are what the framework reads (e.g. `isp_ae_run` gets `&ae_entity_ctx.ae_stats`); left NULL, the 3A cores run with no statistics and the image collapses to green with AE at minimum exposure.
- `config_blc` is declared by the SDK but not defined by the vendor object and never called; the shim exports a no-op. A real implementation must go into base.c first if it is ever called.

## module_cfg

`isp/shim/module_cfg/module_cfg_shim.c` translates the SDK `struct fwi_hw_module_cfg` (the vendor `isp_module_config`, 37040 bytes / 0x90b0 on the 32-bit target) into the clean `fwi_mod_config_t` (`include/module_cfg.h`) for `isp_hardware_update()` / `isp_map_addr()`, calls the clean core, and copies the mutated outputs back. The clean struct holds the same semantic fields re-laid-out: table pointers become embedded arrays and several sub-structs are decomposed differently.

### Header collisions

- The SDK and `module_cfg.h` share three ISP_* macros (ISP_DRC_TBL_SIZE, ISP_GAMMA_TBL_LENGTH, ISP_LENS_TBL_SIZE) with different meanings; the clean ISP_DRC_TBL_SIZE is a byte count, the SDK one an element count. The shim #undefs the SDK ones before including `module_cfg.h`.
- The clean record tags are `fwi_mod_*` / `fwi_reg_*`, distinct from the SDK `fwi_*` set, so no tag is renamed in the shim TU; isp_hardware_update / isp_map_addr are renamed to clean_* there and in the clean TU.

### SDK -> clean (`sdk_to_clean`)

Mode selectors (the clean struct keeps only what its writers consume):

| Clean `mode_cfg` | SDK |
| --- | --- |
| input_fmt, dg_mode, awb_mode, ae_mode, wdr_cmp_mode, rsc_mode | same names |
| cfa_mode | demosaic_mode |
| otf_dpc_mode | on_the_fly_defect_pixel_mode |
| hist_sel | hist_select |
| msc_mode | mesh_shading_mode |
| d3d_mode | denoise_3d_cfg.k3d_increase_mode (no SDK mode_cfg field) |

- SDK mode_cfg.wdr_mode / saturation_mode / hist_mode are not mode_cfg inputs; saturation_mode -> satu_cfg.mode and hist_cfg.hist_mode -> hist_cfg.mode instead.

Per-block renames (SDK -> clean):

| Block | Mapping |
| --- | --- |
| cfa_cfg | demosaic_cfg: dir_threshold -> dir_th, interp_mode, zig_zag |
| otf_cfg | on_the_fly_cfg: ratio[0..3] <- hot / cold / nbhd_diff / nearest_diff ratio; slope_threshold, cold_abs_threshold |
| cnr_cfg | chroma_denoise_cfg: c/y_threshold -> c_th/y_th; st_v_yth -> st_v_y; st_h_yth -> st_h_y |
| gain_offset_cfg | offset / gain / sensor_offset structs {r,gr,gb,b} -> arrays [0..3] (offsets sign-extended) |
| wb_gain_cfg | clip_val; wb_gain {r,gr,gb,b} -> wb_gain[0..3] |
| ctc_cfg | crosstalk_cfg: threshold_max/min/slope -> th_max/th_min/slope; dir_wt; dir_threshold -> dir_th |
| gca_cfg | green_ca_cfg: ct_h, ct_w; r.para0..2 <- r_para0..2; r.int_cns <- int_cns; b.para0..2 <- b_para0..2 |
| lca_cfg | lateral_ca_cfg: prefix dropped, *_threshold -> *_th, luminance -> lum; pf/gf saturation LUTs -> lca_pf/gf_satu_lut (ISP_LCA_SATU_BYTES) |
| sharp_cfg | *_strength -> *_stren; edge_scale_ratio -> edge_scale; high_frequency_scale_ratio -> hfrq_scale; edge_conv_para -> scale_ratio; high_frequency_conv_para -> conv_ratio; *_smoothing_ratio -> *_sm; *_ctrl -> over/under_val/area |
| sharp LUTs | sharp_val, sharp_edge_luminance, sharp_high_frequency_luminance, sharp_hsv -> byte images of ISP_LUT_TH_BYTES (0x42) each (only that much of the wider sharp_hsv[46] is used); sharp_s_map -> ISP_LUT_SHARP_SMAP_BYTES |
| bdnf_cfg | bayer_denoise_cfg: lf/bf/hf_ratio; lp_core[0..3] <- lpN_np_core_ratio; lp_side[0..2] <- lpN_np_side_ratio; lp_pcnt[0..3] <- lpN_pcnt_ratio; d2d_lp_lut[0..3] <- d2d_lpN_threshold |
| tdf_cfg (clean fwi_reg_d3d_cfg_t) | denoise_3d_cfg: rec_en, noise_clip_ratio, bright_diff_ratio, bright_diff_clip_ratio -> clip_ratio, luminance_diff_clip_ratio, st_2d_ratio, mv_ori_ratio, ltf_en, c_weight1..3, ltf_update_frame |
| d3d LUTs | temporal_denoise_threshold -> d3d_tdnf_th; ref_noise -> d3d_ref_noise_lut; k / k_delta -> d3d_k_lut / d3d_k_delta_lut (32 useful bytes of the 36-byte clean arrays: 6 banks, 5 x 6 + 2) |
| rgb2rgb_cfg / rgb2yuv | matrix[3][3] -> flat [9]; offset[3] |
| ae/af/awb/hist windows | *_reg_window: horizontal/vertical_count -> hor/ver_num; horizontal/vertical_start -> hor/ver_start; width; height |
| af_cfg | af_mode -> mode; af_en_cfg booleans -> en_bits (ISP_AF_*_EN); af_filter_cfg iir0_gN / fir0_gN -> iir0/fir0_coef[], iir0_sN -> iir0_s[], ldg/core/hlt/offset fields; af_square_lut |
| awb_cfg | awb_r/g/b_saturation_limit -> sat_r/g/b |
| lens_cfg | lens_shading_cfg ct_x/ct_y/rs_val; lens_src <- contiguous r/g/b planes starting at lens_r_table |
| pltm_cfg | frame_smoothing_en -> frm_sm_en; original_picture_ratio -> oripic_ratio; luminance_ratio -> lum_ratio; block_v/h_count -> block_v/h_num; rest same name |
| wdr_cfg | wdr_low/hi_threshold -> lo/hi_th; wdr_exposure_ratio -> exp_ratio; wdr_slope; wdr_mv_threshold/scale -> mv_th/mv_scale; wdr_output_select -> out_sel |
| msc_cfg | mesh_shading_blw/blh(_delta)_lut -> blw/blh(_dlt); msc_table <- mesh_shading_table pointer |
| satu_cfg | saturation_r/g/b -> satu_r/g/b; saturation_table (embedded) -> table |

Embedded source tables: colour_enhance_table -> cem_src, drc_table -> drc_src, pltm_table -> pltm_src, wdr_table -> wdr_src, gamma_cfg.gamma_tbl -> gamma_cfg.gamma_tbl.

### Table pointers

- The SDK table pointers (colour_enhance, drc, pltm, wdr, lens, gamma) gate both the clean copy and its table_update bit, so their presence is mirrored into the clean *_dst pointers (NULL = absent).
- Saturation: the clean core copies its embedded table (source) to `satu_src` (destination), so `satu_src` is bound to the SDK saturation_table target and the copy lands in the SDK buffer directly, as in the vendor routine.
- Linear: the vendor direction is linear_table -> fe_table; the clean linear_src is fed from the SDK linearize_table target and fe_table is written back.
- Destination arrays are pre-seeded from the current SDK targets, so a module that does not run leaves its target unchanged on write-back (the vendor only fills a target when the module executes).

### Clean -> SDK (`clean_to_sdk`)

- Written back: table_update (the clean update resets it to 0, like the vendor), the table pointer targets (fe, cem, drc, pltm, wdr, gamma), and the two embedded 3D-denoise outputs temporal_denoise_luminance_threshold / temporal_denoise_bri_threshold. Scalars are read-only across an update.
- lens_table is not written back: the vendor `isp_reg_prepare_lens` copies no table (mode + lsc config + the lens_table gate only), so the clean lens_src -> lens_table copy has no SDK-visible counterpart.

## ae

`isp/shim/ae/ae_shim.c` presents the clean AE core (`src/ae/ae_clean.c`) through the framework's `fwi_ae_core_ops_t` contract. The vendor entity embeds its live `ae_param_t` at offset 0 and returns that pointer from get_params(); the framework writes frame id / settings / sensor info into it in place and calls run without a set_params. The shim does the same: the SDK mirror is the entity's first member, and run() re-applies the whole mirror to the clean core before running, so the core always sees live values.

### Parameters (SDK `ae_param_t` -> clean `ae_params_t`)

Runtime (`ae_setting`):

| Clean | SDK | Note |
| --- | --- | --- |
| ev_bias | exposure_compensation | |
| fixed_exposure | exposure_absolute | us |
| sensor_gain | sensor_gain | Q4 |
| iso_sensitivity | iso_sensitivity | |
| aperture | iris_f_number | |
| exposure_mode | exposure_mode | |
| iso_control | iso_mode | |
| light_mode | light_mode | |
| metering_mode | exposure_metering_mode | |
| hdr_mode | ae_mode | |
| mains_detected | flicker_type | detected, 0/1/2 |
| scene | scene_mode | |
| flash_mode | flash_mode | |
| meter_roi | ae_coord | |
| exposure_locked | exposure_lock | |
| flash_open | flash_open | |
| capture_stage | take_pic_start_count | |
| target_comp | ae_target_comp (top level) | |
| exposure_cfg[14] | exposure_cfg[14] | |

Static (`ae_init`):

| Clean | SDK | Note |
| --- | --- | --- |
| table_source | define_ae_table | |
| max_lv | ae_max_level | |
| window_weight_seed[64] | ae_zone_weight[64] | default metering grids still come from the injected wght_* tables |
| hist_metering_en | ae_hist_mode_en | |
| kernel_index | ae_conv_data_index | |
| error_delay_frames | ae_delay_frame | |
| exp_delay_frames | exposure_delay_frame | |
| gain_delay_frames | gain_delay_frame | |
| ev_comp_step | exposure_comp_step | |
| touch_distance_index | ae_touch_distance_ind | |
| high_fps_handling_en | ae_handle_high_fps_en | |
| iso_to_gain_ratio | ae_iso2gain_ratio | |
| aperture_ladder[16] | ae_f_number_step[16] | |
| wdr_cfg[4] | wdr_cfg[4] | |
| total/analog/digital_gain_range[2] | ae_total/analog/digital_gain_range[2] | |
| scene_tables[16] | ae_tbl_scene[16] | byte copy |
| gain_split_ratio | gain_ratio | double |

- Sensor (`ae_sensor_info`), same names: pclk, hts, vts, frame_time, gain_min, gain_max, sensor_width, sensor_height, hflip, vflip.
- Test (`test_cfg`): ae_en -> test_enable; ae_forced -> test_forced; gain -> test_gain; exposure_line -> test_exp_line; luminance_forced -> lum_forced; test_exposure_time -> test_exptime; exposure_line_start/step/end -> exp_line_start/step/end; exposure_change_interval -> exp_change_interval; test_gain -> test_gain_en; gain_start/step/end; gain_change_interval; ae_check_delay_en -> delay_en; ae_delay_type -> delay_type.

### Parameter kind

| SDK `fwi_ae_param_type_e` | Clean kind |
| --- | --- |
| FWI_ISP_AE_INIT_DATA (0) | AE_PARAM_INIT |
| FWI_ISP_AE_UPDATE_AE_TABLE (1) | AE_PARAM_TABLES |
| FWI_ISP_AE_SET_EXPOSURE_INDEX (2) | AE_PARAM_SET_INDEX |
| FWI_ISP_AE_BUILD_TOUCH_WEIGHT (3) | AE_PARAM_TOUCH |

A non-INIT command first re-applies INIT from the mirror, since the vendor reads its embedded config live (same as the framework's INIT -> TABLES setup sequence).

### Statistics (SDK `fwi_ae_stats` -> clean `ae_stats_t`)

| Clean | SDK | Note |
| --- | --- | --- |
| win_avg[384] | average[384] | saturated to 8 bit |
| hist[256] | hist[256] | saturated to 8 bit |
| accum_r/g/b | accum_r/g/b | byte copy of the 16x24 arrays |
| win_pix_n | window_pixel_n | |

A NULL stats pointer is passed to the clean core as "no statistics" (it seeds its default result and returns -1).

### Result (clean `ae_result_t` -> SDK `fwi_ae_result_t`)

- status -> ae_status; setting / setting_last / setting_curr -> sensor_set ev_set / ev_set_last / ev_set_curr; idx_max / idx_expect -> ev_index_max / ev_index_expect.
- setting_short / setting_short_curr -> sensor_set_short, written only in WDR mode or on the no-statistics default path (the vendor leaves it untouched in linear mode).
- bright_pos / dark_pos -> bright/dark_pixel_value; gain_ratio_out -> ae_gain; target -> ae_target; avg_lum -> ae_average_luminance; weight_lum -> ae_weight_luminance; delta_idx -> ae_delta_exposure_index; lv_adj -> ev_level_adj; flash_ev_cumul -> ae_flash_ev_cumulative.
- wdr_ratio sensor/hw_ratio/tmp/last -> ae_wdr_ratio sensor/hardware/tmp/last; wdr_hi_th/wdr_low_th -> wdr_hi/low_threshold; hist_low/mid/hi; backlight; gain_ratio.
- Per-setting fields: exposure_time, analog/digital/total_gain, sensor_exp_line -> ev_sensor_exposure_line, f_number, fno2, lv -> ev_level, ev, ev_idx -> ev_index.
- `sensor_set` is an opaque 176-byte blob in the generated ABI; its layout is `fwi_sensor_settings_t`.
- With no statistics, the vendor applies its default result only when the framework result is still empty (ev_set_curr analog gain 0); otherwise it leaves the result alone. The shim does the same.
- The framework's config_wdr rewrites the result's WDR ratio slots between runs; the shim forwards a non-zero ae_wdr_ratio to the clean core (ae_apply_wdr_feedback) before each run.

### Intentionally unmapped

- ae_setting.flicker_mode (power line frequency) and ae_setting.wdr_output_select: no clean counterpart; only the detected flicker_type is used.
- ev_sensor_true_exp_line / ev_av / ev_tv / ev_sv and result ae_flash_ok / ae_flash_led / ae_wdr_delay: not produced by the clean core and left zero (spec ae.md section 4 omits them; the vendor never writes ev_av/tv/sv or the flash flags either).
- set_params() produces no SDK result; the next run() does.

### Write-back (`sync_mirror`)

The vendor core mutates its embedded mirror in place; the clean core does the same to its own block, so these fields are copied back after every set/run, keeping the stored `ae_param_t` identical to the vendor's:

- INIT: scene slots 0..2 and the aperture ladder, when the supplied arrays are unset (`ae_tbl_scene[0].max_exp == 0`, `ae_f_number_step[0] == 0`).
- Line-table build: analog/digital gain ranges (when either endpoint is zero) and the total gain range; on the auto path exposure_comp_step defaults to 4.
- Manual-ISO path: ae_setting.sensor_gain / iso_sensitivity; also ae_target_comp.

### Capture (`FREEISP_AE_DUMP`, `FREEISP_AE_TAIL`)

Inert unless `FREEISP_AE_DUMP=<path>` is set ("1" or empty selects `/tmp/freeisp_ae_dump.bin`). One record is appended per run(), holding exactly the inputs map_config()/map_stats() consume:

| Offset | Content | Size |
| --- | --- | --- |
| 0 | `ae_param_t` | 4944 |
| 4944 | `struct fwi_ae_stats` (zeros if NULL) | 7172 |

Record total: 12116 bytes.

The vendor metering grid reads past the statistics block (avg[640..791], 152 u32 beyond avg[384]+hist[256]), i.e. framework memory after the struct. `FREEISP_AE_TAIL=<nbytes>` (default 1024, 0 = off, max 65536) also dumps that many bytes following the stats struct to `<path>.tail`:

- Header: "AETL", u32 version = 1, u32 nbytes, u32 reserved (little endian).
- Then one nbytes record per run(), in the same order as the main dump; zeros when stats are NULL.
- The vendor itself over-reads 608 bytes there, so the default span is safe.

## awb

Shim: `isp/shim/awb/awb_shim.c`. Presents the clean AWB core (`src/awb/awb_clean.c`) through the `fwi_awb_core_ops_t` contract; design notes in `isp/shim/awb/DESIGN.md`.

### Entity layout

- `awb_shim_entity_t { awb_param_t config; awb_entity_t *clean; }`: the SDK mirror sits at offset 0 (the vendor entity does the same); get_params returns `&config`.
- The clean entity embeds its own `awb_params_t`; the shim reaches it via `clean_awb_get_params()` and re-maps the mirror on every set and every run, because the framework writes the mirror in place between calls.

### SDK awb_param_t -> clean awb_params_t

| Clean field | SDK source | Note |
| --- | --- | --- |
| mode | awb_ctrl.wb_mode | |
| manual_gain[4] | awb_ctrl.wb_gain_manual.{r,gr,gb,b}_gain | |
| lock | awb_ctrl.white_balance_lock | |
| win_region[4] | awb_ctrl.awb_coor.{x1,y1,x2,y2} | |
| interval | awb_ini.awb_interval | |
| speed | awb_ini.awb_speed | |
| temp_low | awb_ini.awb_color_temper_low | unused by clean core |
| temp_high | awb_ini.awb_color_temper_high | unused by clean core |
| base_temp | awb_ini.awb_base_temper | |
| green_dist | awb_ini.awb_green_zone_dist | |
| blue_dist | awb_ini.awb_blue_sky_dist | |
| light_num | awb_ini.awb_light_num | |
| ext_light_num | awb_ini.awb_ext_light_num | |
| skin_num | awb_ini.awb_skin_color_num | |
| special_num | awb_ini.awb_special_color_num | |
| light_info[320] | awb_ini.awb_light_info | |
| ext_light_info[320] | awb_ini.awb_ext_light_info | |
| skin_info[160] | awb_ini.awb_skin_color_info | |
| special_info[320] | awb_ini.awb_special_color_info | |
| preset_gain[22] | awb_ini.awb_preset_gain | |
| r_favor / b_favor | awb_ini.awb_rgain_favor / awb_ini.awb_bgain_favor | |
| ae_lv | awb_sensor_info.ae_lv | |
| ae_index / ae_index_max | awb_sensor_info.ae_tbl_idx / .ae_tbl_idx_max | |
| ae_done | awb_sensor_info.is_ae_done | |
| test_mode | test_cfg.isp_test_mode | unused by clean core |
| platform_id | isp_platform_id | unused by clean core |
| fixed_temp | test_cfg.isp_color_temp | |
| control_enable | test_cfg.awb_en | |
| frame_id | awb_frame_id | |

- Reference records in awb_ini are ten HW_S32 each, matching the clean record stride (compile-time checked).

### Statistics

- clean `win[i].avg[0..2]` <- `fwi_awb_stats_t` (vendor: `isp_awb_stats_s`) `.awb_average_r/g/b` (row-major 32x32, i = row*32 + col).
- clean `win[i].npix` <- `fwi_awb_stats_t.awb_sum_count`.
- SDK grid ISP_AWB_ROW * ISP_AWB_COL (32x32 at ISP_VERSION 521) equals clean AWB_NWIN = 1024 (compile-time checked).

### Result translation

- clean `gain_out.{r,gr,gb,b}` -> `wb_gain_output`; `color_temp_out` -> `color_temp_output`.
- Null stats: the clean run returns -1 and seeds the safe default (unity gains, 6500 K), copied out unchanged.
- Write-back is gated like the vendor: the clean result is seeded with the caller's current result, so early-outs (start-up frames, lock, interval gate, manual mode, outlier abort) leave gains and colour temperature untouched.
- set-params only rebuilds the reference hierarchy for the ISP_AWB_INI_DATA tag; other tags return -1 without touching the entity, as the vendor does.

### Runtime tables (`awb_shim_set_tables`)

- Opaque `awb_clean_tables_t`: trust table 96x256 u8, temporal smoothing-weight rows 48x64 u16, class labels 10x32 char, per-preset standard trust 10 s32, brightness x temperature trust matrix 10x10 s32, safe_gain pointer.
- No vendor table carries the no-statistics fallback gains (r, gr, gb, b); the built-in default is unity and `awb_shim_set_safe_gain()` overrides it.

### Debug capture record (`FREEISP_AWB_DUMP=<path>`, "1" = /tmp/freeisp_awb_dump.bin)

One fixed-size record appended per run (37592 bytes):

| Offset | Content | Size |
| --- | --- | --- |
| 0 | awb_param_t (no pointers) | 4824 |
| 4824 | fwi_awb_stats_t (zeros when no stats) | 32768 |

## afs

Shim: `isp/shim/afs/afs_shim.c`. Presents the clean AFS core (`src/afs/afs_clean.c`) through the `fwi_afs_core_ops_t` contract.

### SDK params/stats -> clean params/stats

| Clean field | SDK source | Note |
| --- | --- | --- |
| frame_index | afs_frame_id | |
| mode | flicker_mode | enum power_line_frequency 0..3 |
| seed_type | flicker_type_ini | |
| min_peak_ratio | flicker_ratio | |
| enable | test_cfg.afs_en | |
| gain_level | afs_sensor_info.ae_gain | vendor reads the AE gain word at sensor-info offset 108, not ae_tbl_idx (see include/freeisp/afs.h) |
| image_width | afs_sensor_info.sensor_width | |
| param_kind | type | unused by clean core |
| platform_id | isp_platform_id | unused by clean core |
| auto_flag_param | auto_afs_flag | unused by clean core |
| column_sum[128] | afs_stats->afs_sum[128] | |
| stats_width | afs_stats->pic_width | unused by clean core |
| stats_height | afs_stats->pic_height | unused by clean core |

### Result translation (clean -> SDK)

| clean detected_type | SDK enum |
| --- | --- |
| 0 | FLICKER_NO |
| 50 | FLICKER_50HZ |
| 60 | FLICKER_60HZ |

- detected_type -1 (frames < 3, unknown mode): the persistent SDK result is left untouched, as the vendor does.
- When a context is linked, the result is also copied to ae_settings.flicker_type (vendor AFS run stage does the same).
- Unmapped / pass-through: afs_test_config.isp_test_mode (no clean counterpart) and the parts of fwi_sensor_state_t other than sensor_width/ae_gain. The SDK result has a single field.

### Context corpus -> afs_param_t (`afs_shim_update_cfg`)

The vendor algorithm never sees the context (`fwi_isp_ctx`); the framework projects it onto afs_param_t.

| afs_param_t field | Context source | When |
| --- | --- | --- |
| flicker_ratio | isp_tunning_settings.flicker_ratio | context-update step (on config change) |
| flicker_type_ini | isp_tunning_settings.flicker_type | context-update step |
| test_cfg.isp_test_mode | isp_test_settings.isp_test_mode | context-update step |
| test_cfg.afs_en | isp_test_settings.afs_en | context-update step |
| isp_platform_id | module_cfg.isp_platform_id | per-frame param step |
| afs_frame_id | af_frame_cnt | per-frame param step |
| afs_sensor_info | sensor_info (whole descriptor) | per-frame param step |

- flicker_mode is never written by the vendor context-update step (checked in the v521/v316 and v833 sources); the caller supplies it through the get-params mirror.
- In the fwi ABI the context is `fwi_isp_ctx` and the sensor descriptor is `fwi_sensor_state_t`, whose field names differ from the vendor's `isp_sensor_info_t`; the shim copies field by field.

## iso

`isp/shim/iso_shim.c` presents the clean ISO core (`src/iso/iso_clean.c`) through the framework's `fwi_iso_cfg_core_ops_t` contract. The SDK `iso_param_t` mirror sits at entity offset 0 and get_params() returns it.

### Parameters (SDK `fwi_iso_param_t` -> clean `iso_params_t`)

Live fields (`map_live_params`), refreshed every run() because the framework mutates the stored block in place without calling set_params:

| Clean | SDK |
| --- | --- |
| frame_id | iso_frame_id |
| chroma_gray_th | colour_enhance_color2gray_threshold |
| chroma_gray_span_min / max | colour_enhance_color2gray_delta_min / max |
| cnr_on, sharp_on, sat_on, contrast_on, brightness_on | chroma_denoise_adjust, sharpness_adjust, saturation_adjust, contrast_adjust, brightness_adjust |
| cem_on, denoise_on, sensor_offset_on, black_level_on, dpc_on | colour_enhance_ratio_adjust, denoise_adjust, sensor_offset_adjust, black_level_adjust, defect_pixel_adjust |
| defog_on, pltm_dyn_on, tdnr_on | dehaze_value_adjust, pltm_dynamic_cfg_adjust, tdnr_adjust |
| ae_cfg_on, gtm_cfg_on, lca_cfg_on, af_cfg_on | ae_cfg_adjust, gtm_cfg_adjust, lateral_ca_cfg_adjust, af_cfg_adjust |
| dn_core_ratio[0..3] | denoise_lp0..3_np_core_ratio |

- The framework calls set_params *before* it raises the per-block adjust gates, and its per-frame path only refreshes the frame id and colour-to-gray thresholds; the vendor core reads the live mirror on every run. Without forwarding, the clean core would keep the all-clear snapshot, skip every gated block and leave e.g. module_cfg saturation at zero (the framework then drops the saturation programming and the image goes green).
- Rebuild inputs (`map_params`, from `tuning.by_iso`): gain_point[] <- gain_mapping_point[]; lum_point[] <- luminance_mapping_point[]; cfg[] <- dynamic_cfg[] as a byte copy. `iso_dyn_t` (union of int32_t[148]) and `fwi_dynamic_cfg` (all s32, same 19 members in the same order) have the same size, checked at compile time.

### Tuning corpus (SDK `tuning.modules` -> clean `iso_ctx_t`)

The vendor object reads these directly from the framework context at run time; the shapes differ, so the mapping is not a plain copy.

| Clean `iso_ctx_t` | SDK source | Shape change |
| --- | --- | --- |
| k3d_incre_curve[256] i32 | d3d_k3d_incre_curve[256] | u8 -> i32 |
| tdnf_diff[256] i32 | temporal_denoise_diff[256] | u8 -> i32 |
| sharp_edge_lum[33] u16 | sharp_edge_luminance[33] | identical |
| sharp_hfrq_lum[33] u16 | sharp_high_frequency_luminance[33] | identical |
| sharp_hsv[46] u16 | sharp_hsv[46] | identical |
| sharp_s_map[33] u8 | sharp_s_map[33] | identical |
| sharp_val[66] i32 | sharp_val[33] ++ sharp_luminance[33] | u16 -> i32, two halves |
| bdnf_th[33] i32 | bayer_denoise_threshold[33] | u16 -> i32 |
| tdnf_th[66] i32 | temporal_denoise_threshold[33] ++ temporal_denoise_ref_noise[33] | u16 -> i32, two halves |
| tdnf_k[32] i32 | temporal_denoise_k[32] | u8 -> i32 |
| cem_table_a[5888] u8 | colour_enhance_table[5888] | identical |
| cem_table_b[5888] u8 | colour_enhance_table1[5888] | identical |

- Two-half merges: the vendor's source pointer is the first array and it indexes `[0x21 + k]` (33 + k), which lands in the adjacent array of the same element type (it reads 66 contiguous u16). For tdnf_th the second half is the reference-noise table (spec iso.md 6.13).

### Context inputs (`map_ctx_inputs`)

- sensor total_gain / ae_tbl_index / ae_tbl_index_max -> total_gain / ae_pos / ae_pos_max; ae_ctl.ae_mode -> ae_mode.
- Enables: denoise_3d_en -> tdf_enable; local_contrast_en -> contrast_enable; wb_gain_en -> wb_enable; saturation_en -> sat_enable; local_tone_en -> pltm_enable.
- picture_ctl levels: contrast, saturation, sharpness, brightness, denoise, pltmwdr -> pltm_level, denoise_3d -> tdf_level, highlight, backlight.
- dynamic_stats: enable and the *_comp_target values; *_comp[0..3] -> comp.*.
- pltm_next_strength -> pltm_strength; AWB wb_gain_output r/gb/b -> wb_r/gb/b_gain; rgb2rgb and rgb2yuv matrix/offset; gamma_tbl[3072] -> gamma_table (i32).
- Shared state the clean core reads and updates across frames: module_enable_flag, table_update.

Trigger selectors (`tuning.by_iso.trigger_selectors[17]`, a raw u8 array in the ABI, no named fields):

| Index | Trigger | Clean field |
| --- | --- | --- |
| 0 | sharpen | trig_sharp |
| 1 | contrast | trig_contrast |
| 2 | denoise | trig_denoise |
| 3 | sensor offset | trig_sensor_offset |
| 4 | black level | trig_black_level |
| 5 | DPC | trig_dpc |
| 6 | dehaze | trig_defog |
| 7 | PLTM dynamic | trig_pltm_dynamic |
| 8 | brightness | trig_brightness |
| 9 | global contrast | not mapped (no consumer) |
| 10 | saturation | trig_saturation |
| 11 | CEM ratio | trig_cem_ratio |
| 12 | 3D denoise | trig_tdf |
| 13 | chroma denoise | trig_color_denoise |
| 14 | AE cfg | trig_ae_cfg |
| 15 | GTM cfg | trig_gtm_cfg |
| 16 | LCA cfg | trig_lca_cfg |

### Result write-back (`map_result_to_sdk`)

Each SDK block is written only when its own `*_adjust` flag is set, as the vendor gates its writes; a disabled block keeps the framework value. Clean-owned shared state (adjust.*, ae settings, dynamic_stats.*, sharpness_level, module_enable_flag, table_update) is copied from the clean context.

| Gate | Written |
| --- | --- |
| chroma_denoise_adjust | chroma_denoise_cfg c/y_threshold, st_v/h_yth |
| sharpness_adjust | sharp_cfg scalars and LUTs (sharp_val[0..32] / sharp_luminance <- sharp_val[33..65]), picture_ctl.sharpness_level |
| saturation_adjust | saturation_r/g/b, mode_cfg.saturation_mode, saturation_table[256], table_update |
| denoise_adjust | bayer_denoise_cfg ratios and d2d_lp0..3_threshold[33] |
| defect_pixel_adjust | on_the_fly_cfg ratios and thresholds |
| lateral_ca_cfg_adjust | lateral_ca_cfg |
| af_cfg_adjust | af_mode, af_en_cfg, af_filter_cfg, af_square_lut |
| colour_enhance_ratio_adjust | colour_enhance_table: ISO_CEM_BYTES (6656) copied although the table is 5888 bytes; the vendor's per-cell pass deliberately runs into the following lut_cfg member |
| sensor_offset_adjust | gain_offset_cfg.sensor_offset, sensor.gain_offset |
| black_level_adjust | gain_offset_cfg.offset |
| dehaze_value_adjust | adjust_ctl.dehaze_value |
| pltm_dynamic_cfg_adjust | ae_ctl.pltm_dynamic_cfg[4] |
| tdnr_adjust | denoise_3d_cfg scalars, k/k_delta, threshold/ref_noise (from tdnf_th halves), temporal_denoise_table, module_enable_flag |
| ae_cfg_adjust | ae_ctl.exposure_cfg[14] |
| gtm_cfg_adjust | ae_ctl.ae_hist_eq_cfg[9] |
| contrast_adjust | adjust_ctl.contrast, dynamic_stats enable and *_comp[4] |
| brightness_adjust | adjust_ctl.brightness |

- temporal_denoise_table is a framework-supplied buffer: the 256-byte increment curve followed by the 256-byte clamped diff curve, matching the vendor's two memcpys. The clean result stores each byte in a u16 slot, so the low byte is the data.

### Capture and trace

`FREEISP_ISO_DUMP=<path>` ("1" or empty selects `/tmp/freeisp_iso_dump.bin`), inert unless set. One record per run():

| Offset | Content | Size |
| --- | --- | --- |
| 0 | `iso_param_t` | 112 |
| 112 | framework ISP context (`struct fwi_isp_ctx`, zeros if NULL) | 273600 |

Record total: 273712 bytes. The ISO run reads its inputs from the whole framework context (tuning corpus, sensor/AE state, levels, AWB/PLTM results, module_cfg inputs), so the context is captured verbatim; its module_cfg table pointers are camera addresses that a replay must overwrite.

`FREEISP_ISO_TRACE=<path>` ("1" = stdout) prints one line per run() with the live saturation gate in the mirror, the dynamic record the clean core picked, the clean result and the module_cfg saturation values the framework will read, to localise a stale-gate / skipped-block divergence on the device.

## gtm

Shim: `isp/shim/gtm/gtm_shim.c`. Presents the clean GTM core (`src/gtm/gtm_clean.c`) through the `fwi_gtm_core_ops_t` contract; design notes in `isp/shim/gtm/DESIGN.md`.

### gtm_ini.gtm_cfg[] (GTM_HEQ_*) -> clean params

Indices are matched to the behaviour of the deployed object (see DESIGN.md).

| Index | SDK name | Clean field | Note |
| --- | --- | --- | --- |
| 0 | GTM_HEQ_GAIN | range_max | |
| 1 | GTM_HEQ_EQ_RATIO | eq_gain | |
| 2 | GTM_HEQ_EQ_SMOOTH | cover_bias | |
| 3 | GTM_HEQ_BLACK | black_level | |
| 4 | GTM_HEQ_WHITE | white_level | |
| 5 | GTM_HEQ_BLACK_ALPHA | white_slope | highlight ramp term |
| 6 | GTM_HEQ_WHITE_ALPHA | black_slope | shadow ramp term |
| 7 | GTM_HEQ_GAMMA_IND | pre_gamma_offset | |
| 8 | GTM_HEQ_GAMMA_PLUS | (unused) | |

- `gtm_ini.AutoAlphaEn`, `BrightPixellValue`, `DarkPixelValue` are not mapped: the deployed object clears AutoAlphaEn to 0 before reading either pixel value, and the clean auto-alpha ramp is hard-disabled, so only the AutoAlphaEn == 0 behaviour is reachable.

### Other scalars and tables (fwi_gtm_param_t names)

| Clean field | SDK source |
| --- | --- |
| mode | gtm_init.gtm_type |
| gamma_mode | gtm_init.gamma_type |
| frame_index | gtm_frame_id |
| enable | gtm_enable != 0 |
| hist_pixel_count | gtm_init.hist_pixel_count |
| dark_floor | gtm_init.dark_minval |
| bright_floor | gtm_init.bright_minval |
| brightness / contrast | brightness / contrast |
| bit_offset | gtm_bit_offset |
| wdr_en | wdr_en != 0 |
| peak_map[r][c] | gtm_init.plum_var[r][c] |
| gamma_lut | *gamma_tbl (zeros if NULL) |
| curve / curve_prev | *drc_table / *drc_table_last (set_params only; run leaves curve state alone) |

- NULL SDK table pointers are wired to the clean entity's own gamma_lut / curve / curve_prev storage; otherwise the clean curves are copied back to drc_table / drc_table_last after set and after each non-gated run.
- The clean parameter block embeds its own curve / curve_prev / gamma_lut storage, so no separate clean context is carried.

### Statistics and result

- SDK stats windows ISP_AE_ROW * ISP_AE_COL (16x24 = 384 at ISP_VERSION 521) equal clean GTM_NWIN = 384, so `average` is byte-copied to `win_avg`; ISP_HIST_NUM equals GTM_NCURVE and `hist` is copied to `hist_raw` (both compile-time checked).
- Null stats return -1.

| SDK result | Clean result |
| --- | --- |
| hist_max_val | peak_level |
| average_luminance | avg_lum |
| average_var | avg_var |
| hist_div | div_index |
| hratio_last | ratio_hold |
| hdr_req | hdr_flag |

- Gated frames (frame_index <= 2, or disabled) leave the result and the curve untouched, like the framework object.

### Debug capture record (`FREEISP_GTM_DUMP=<path>`, "1" = /tmp/freeisp_gtm_dump.bin)

One fixed-size record appended per run (14616 bytes); the three table pointers are captured inline, a NULL pointer as zeros.

| Offset | Content | Size |
| --- | --- | --- |
| 0 | gtm_param_t | 276 |
| 276 | fwi_gtm_stats_t (vendor: isp_gtm_stats_s) | 7172 |
| 7448 | HW_U16 gamma_tbl[3072] (ISP_GAMMA_TBL_LENGTH) | 6144 |
| 13592 | HW_U16 drc_table[256] (ISP_DRC_TBL_SIZE) | 512 |
| 14104 | HW_U16 drc_table_last[256] | 512 |

## pltm

Shim: `isp/shim/pltm/pltm_shim.c`. Presents the clean PLTM core (`src/pltm/pltm_clean.c`) through the `fwi_pltm_core_ops_t` contract.

### Entity layout

- `pltm_shim_entity_t { pltm_param_t config; pltm_entity_t *clean; }`: the SDK mirror sits at offset 0 (the vendor entity does the same); get_params returns `&config`.
- The clean entity embeds its own `pltm_clean_params_t` (config + sensor); the shim reaches it via `clean_pltm_get_params()` and re-maps the mirror on every set and every run, because the framework writes the mirror in place between calls.

### SDK pltm_param_t -> clean params (per the clean spec)

| Clean field | SDK source |
| --- | --- |
| config.mode | pltm_ini.pltm_cfg[ISP_PLTM_MODE] |
| config.oripic_ratio_cfg | pltm_ini.pltm_cfg[ISP_PLTM_ORIPIC_RATIO] |
| config.order_cfg | pltm_ini.pltm_cfg[ISP_PLTM_TR_ORDER] |
| config.last_order_ratio_cfg | pltm_ini.pltm_cfg[ISP_PLTM_LAST_ORDER_RATIO] |
| config.clip_cfg | pltm_ini.pltm_cfg[ISP_PLTM_POW_TBL] |
| config.gain_cfg | pltm_ini.pltm_cfg[ISP_PLTM_F_TBL] |
| config.block_rows | pltm_ini.pltm_cfg[ISP_PLTM_BLOCK_V_NUM] |
| config.block_cols | pltm_ini.pltm_cfg[ISP_PLTM_BLOCK_H_NUM] |
| config.contrast | pltm_ini.pltm_cfg[ISP_PLTM_CONTRAST] |
| config.tolerance | pltm_ini.pltm_cfg[ISP_PLTM_TOLERANCE] |
| config.speed | pltm_ini.pltm_cfg[ISP_PLTM_SPEED] |
| config.step | pltm_ini.pltm_cfg[ISP_PLTM_STEP] |
| config.interval | pltm_ini.pltm_cfg[ISP_PLTM_INTERVAL_FRAME] |
| config.enable | pltm_enable |
| config.frame_id | pltm_frame_id |
| config.auto_strength | pltm_ini.pltm_dynamic_cfg[..AUTO_STREN] |
| config.manual_strength | pltm_ini.pltm_dynamic_cfg[..MANUL_STREN] |
| config.ae_comp | pltm_ini.pltm_dynamic_cfg[..AE_COMP] |
| config.min_threshold | pltm_ini.pltm_dynamic_cfg[..MIN_TH] |
| config.bit_offset | wdr_bit_offset |
| config.source_curve | pltm_table (>= 0x300 u16 entries) |
| sensor.width / height | sensor_info.sensor_width / sensor_height |
| sensor.wdr_mode | sensor_info.wdr_mode |
| sensor.ae_settled | sensor_info.is_ae_done |
| sensor.backlight | sensor_info.backlight |
| stats.measured_min | stats->pltm_stats->min_after_pltm |
| stats.lst[768] | stats->pltm_stats->lst[768] |

### Unmapped (not consumed by the clean module)

- pltm_cfg[ISP_PLTM_LSS_SWITCH / LUM_RATIO / LP_HALO_RES / WHITE_LEVEL / SPATIAL_ASM / INTENS_ASYM]: the clean member exists but the spec marks it unreferenced; the framework reads these directly from the tuning blob in the vendor PLTM run stage.
- pltm_param_t.type / isp_platform_id / ae_enable: stored in the mirror only.
- Stats other than min_after_pltm / lst (avg_before/after, min_before, max_before/after): the clean spec reads only the measured minimum and the per-tile list.
- strength_bank / converge_bank are not in pltm_param_t: runtime-injected tables (`freeisp_get_tables()`), installed with `pltm_shim_set_tables()` before `pltm_init()`. Shapes: strength curve bank 4x256 int32, convergence step bank 32x128 uint8.

### Result translation (fwi names)

| SDK result | Clean result |
| --- | --- |
| pltm_last_order_ratio | last_order_ratio |
| pltm_tr_order | order |
| pltm_original_picture_ratio | oripic_ratio |
| pltm_cal_en | cal_en |
| pltm_frame_smoothing_en | frame_smooth_en |
| pltm_block_height / pltm_block_width | block_height / block_width |
| pltm_statistic_div | stat_scale |
| pltm_tbl[768] | tbl[768] |
| pltm_ae_comp | ae_comp |
| pltm_old_strength / pltm_next_strength | old_strength / next_strength |
| pltm_cal_strength | cal_strength |
| pltm_min_threshold | min_threshold |

- The SDK carries old/next strength in the result across frames (the vendor reads them from there); the clean core keeps them on the entity, so the shim seeds them from the result before each run.
- Result table and stats list are 768 u16 on both sides (compile-time checked).

### Debug capture record (`FREEISP_PLTM_DUMP=<path>`, "1" = /tmp/freeisp_pltm_dump.bin)

One fixed-size record appended per run (3344 bytes). pltm_cfg / pltm_dynamic_cfg are inline in the struct; the source curve pointer is captured inline (NULL as zeros).

| Offset | Content | Size |
| --- | --- | --- |
| 0 | pltm_param_t | 260 |
| 260 | fwi_pltm_stats_t (vendor: isp_pltm_stats_s) | 1548 |
| 1808 | HW_U16 pltm_table[0x300] (source curve) | 1536 |

## integration

Runtime table feed: `isp/shim/integration/freeisp_shim_tables.c`. No tuning bytes are compiled in; the locator (`freeisp/rmm_tables.h`: two anchors, offsets-only layout, fail-closed structural validators) slices the 36 vendor tables out of a stock `rmm` image, and the feed points each clean module's `*_clean_tables_t` at the located buffers and installs them through the shim setters. Table provenance: `isp/shim/DESIGN.md` and `isp/include/freeisp/tables_layout.h`.

### Vendor table -> clean table field

| Module | Clean field | Vendor table | Note |
| --- | --- | --- | --- |
| ae | log2 | Ae_Log2 | |
| ae | evtab | Ae_DeltaLvTbl | owned, writable copy |
| ae | conv | AeConverData | shared with GTM/PLTM |
| ae | touchprob | AeProbData | |
| ae | kernel | AeHistData | shared with GTM |
| ae | pregamma | AeGammaPre | shared with GTM |
| ae | blmask | AeBackLightWeight | |
| ae | wght_matrix / wght_avg / wght_center | Ae_LumWeight_win / _avg / _center | |
| ae | wght_over / wght_under | Ae_OverExp_LumWeight / Ae_UnderExp_LumWeight | |
| ae | net_in / net_bias / net_out | IW / b1 / LW | |
| ae | net_out_bias | (none: code immediates) | extracted by src/tables/ae_out_bias.c, cached in isp_cfg/ae_out_bias.txt; NULL disables the network (backlight 32) |
| ae | fno_ladder, fno_def | ae_fno_def | |
| ae | table_default | AeTblDef | whole-descriptor copy: vendor ae_table_info is 252 bytes and must stay byte-compatible with ae_desc_t |
| ae | auxprob | AwbProbData | shared with AWB |
| awb | trust | AwbProbData | |
| awb | speed_w | AwbSpeedData | |
| awb | class_label | AwbLightClassName | |
| awb | std_trust | AwbStdTempWeight | |
| awb | temp_bright | AwbTempLvWeightDef | |
| awb | safe_gain | (none, NULL) | see below |
| afs | sine / cosine | afs_sin / afs_cos | |
| iso | gain_index_table | TBL2GAIN | |
| iso | gain_point_default | iso_gain_point | |
| iso | lum_point_default | iso_lum_point | |
| iso | af_square_table | af_square_lut | |
| iso | af_iir_s | (none, NULL) | see below |
| gtm | guide_linear / guide_low / guide_high | gd_curve_linear / gd_curve_low / gd_curve_high | |
| gtm | pre_gamma | AeGammaPre | shared with AE |
| gtm | eq_kernel | AeHistData | shared with AE |
| gtm | converge | AeConverData | shared with AE/PLTM |
| pltm | strength_bank | pltm_stren_tbl_buf | |
| pltm | converge_bank | AeConverData | shared with AE/GTM |
| pltm | presets | PLTM preset text cache | NULL (neutral) when unresolved |
| base (weak hook) | base_shim_set_locator() args | anti_gamma_table, rgb2yuv_matrix, lsc_trig_cfg_def, msc_trig_cfg_def, isp_cm_color_temp | inert unless base_shim.o is linked |

- Shared vendor tables (AwbProbData, AeGammaPre, AeHistData, AeConverData) are single-sourced from the one located copy.
- Shape guards: ISO_CURVE_N 350 / ISO_BP_N 14; AWB 96 trust rows, 48x64 speed weights, 10 refs; GTM pre-gamma 11x256, EQ 20x31, converge 32x128; PLTM strength 4x256, converge 32x128; AE 384 windows, 256 hist bins, index max 1024, 10 segments.
- awb.safe_gain: the rmm image has no no-statistics fallback gains; the clean core treats NULL as "not supplied" and skips the fallback. `awb_shim_set_safe_gain()` only writes the shim's built-in table, so with external tables installed the caller must point its own table at the gains.
- iso.af_iir_s: the rmm image has no AF IIR feedback coefficients; NULL makes the clean core program zero feedback coefficients (iso_clean.c blk_af_cfg). No ISO setter exists for the field.
- PLTM presets are resolved separately from the table bundle (own text cache) and survive `freeisp_shim_tables_free()`.
- Bundle cache path defaults to the directory of yi-mediad's vendor-tuning cache (`/tmp/sd/unifi/isp_cfg`); the kill switch MEDIAD_NO_RMM_TUNING is applied by the caller and disables both the cache and the locator. An empty bundle path gives pure-locator behaviour (e.g. for auditing).

## framework context

SDK context names and the `fwi_isp_ctx_t` fields that hold them (`src/framework/framework_internal.h`):

| SDK name | Field |
| --- | --- |
| `isp_ini_cfg` | `ctx->tuning` (`fwi_tuning_image_t`) |
| `isp_test_settings` | `ctx->tuning.enables` |
| `isp_3a_settings` | `ctx->tuning.a3` |
| `isp_tunning_settings` | `ctx->tuning.modules` |
| `module_cfg` | `ctx->hw_cfg` (`fwi_hw_module_cfg_t`) |
| `sensor_info` | `ctx->sensor` |
| `stat` | `ctx->drv_stats_ref` |
| `stats_ctx` | `ctx->stats` |
| `ae_settings` / `awb_settings` / `af_settings` | `ctx->ae_ctl` / `ctx->awb_ctl` / `ctx->af_ctl` |
| `tune` | `ctx->picture_ctl` |
| `adjust` | `ctx->adjust_ctl` |
| `defog_ctx` | `ctx->dehaze_state` |
| `*_entity_ctx` | `ctx->*_entity` |
| `isp_3a_change_flags` | `ctx->pending_3a_changes` |
| `isp_ir_flag` | `ctx->ir_mode` |
| `isp_stat_buf` | `ctx->stats_buf` |
| `load_reg_base` | `ctx->reg_image` |
| `ctx_lock` | `ctx->lock` |
| `ops` | `ctx->callbacks` |

ISO module names use the ABI spellings: `cnr` -> `chroma_denoise`, `cem` -> `colour_enhance`, `dpc` -> `defect_pixel`, `defog` -> `dehaze`, `lca` -> `lateral_ca`.
