// SPDX-License-Identifier: AGPL-3.0-only
/* framework_isp.h - ISP framework public declarations and imports, on top of
 * the generated fwi_* ABI set. Included by fisp_/fcap_/mediad. */
#ifndef FRAMEWORK_ISP_H
#define FRAMEWORK_ISP_H

#include "fwi_isp_api.h"
#include "framework_events.h"

/* attribute ids */
enum isp_ctrl_id {
    ISP_CTRL_MODULE_EN = 0,
    ISP_CTRL_DIGITAL_GAIN,
    ISP_CTRL_PLTMWDR_STR,
    ISP_CTRL_DN_STR,
    ISP_CTRL_3DN_STR,
    ISP_CTRL_HIGH_LIGHT,
    ISP_CTRL_BACK_LIGHT,
    ISP_CTRL_WB_MGAIN,
    ISP_CTRL_AGAIN_DGAIN,
    ISP_CTRL_COLOR_EFFECT,
    ISP_CTRL_AE_ROI,
    ISP_CTRL_AF_METERING,
    ISP_CTRL_COLOR_TEMP,
    ISP_CTRL_EV_IDX,
    ISP_CTRL_PLTM_HARDWARE_STR
};

/* ------------------------------------------------------------------ */
/* imports: implemented by mediad / the kept tiers                     */
/* ------------------------------------------------------------------ */
int parser_ini_info(fwi_tuning_image_t *param, char *sensor_name, int w, int h,
                    int fps, int wdr, int ir, int sync_mode, int isp_id);

/* 3A entry points (isp/shim/<module>/<module>_shim.c); each *_init returns the
 * module's fwi_*_core_ops_t vtable, used directly as ctx->*_entity.ops. */
void *ae_init(fwi_ae_core_ops_t **ae_core_ops);
void *iso_init(fwi_iso_cfg_core_ops_t **iso_core_ops);
void *pltm_init(fwi_pltm_core_ops_t **pltm_core_ops);
void *gtm_init(fwi_gtm_core_ops_t **gtm_core_ops);
void *afs_init(fwi_afs_core_ops_t **afs_core_ops);
void *awb_init(fwi_awb_core_ops_t **awb_core_ops);
void  ae_exit(void *e);
void  awb_exit(void *e);
void  afs_exit(void *e);
void  iso_exit(void *e);
void  gtm_exit(void *e);
void  pltm_exit(void *e);

void isp_handle_stats(fwi_isp_ctx_t *ctx, const void *buffer);
void isp_apply_settings(fwi_isp_ctx_t *ctx);
void __isp_stat_dynamic_judge(fwi_isp_ctx_t *ctx);
void isp_apply_colormatrix(fwi_isp_ctx_t *ctx);
void config_gamma(fwi_isp_ctx_t *ctx);
void config_dig_gain(fwi_isp_ctx_t *ctx, int gain);
void config_wdr(fwi_isp_ctx_t *ctx, int init);
void config_band_step(fwi_isp_ctx_t *ctx);
void config_lens_table(fwi_isp_ctx_t *ctx, int code);
void config_lens_center(fwi_isp_ctx_t *ctx);
void config_msc_table(fwi_isp_ctx_t *ctx, int code);

void isp_hardware_update(fwi_hw_module_cfg_t *cfg);
void isp_map_addr(fwi_hw_module_cfg_t *cfg, unsigned long base);

/* Optional consumer hook, called immediately before the per-frame
 * isp_hardware_update; the framework supplies a weak no-op default. */
void isp_control_hook(fwi_hw_module_cfg_t *cfg);

struct freeisp_reg_table {
    const uint32_t *default_reg;
};
struct freeisp_tables {
    const struct freeisp_reg_table *base;
};
const struct freeisp_tables *freeisp_get_tables(void);

#endif /* FRAMEWORK_ISP_H */
