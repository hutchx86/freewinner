// SPDX-License-Identifier: AGPL-3.0-only
/* framework_internal.h - private framework declarations (unit F).
 *
 * Field mapping from 20-framework.md prose to the frozen fwi_* ABI:
 *   isp_ini_cfg          -> ctx->tuning   (fwi_tuning_image_t)
 *     isp_test_settings  -> ctx->tuning.enables
 *     isp_3a_settings    -> ctx->tuning.a3
 *     isp_tunning_settings -> ctx->tuning.modules
 *   module_cfg           -> ctx->hw_cfg   (fwi_hw_module_cfg_t)
 *   sensor_info          -> ctx->sensor
 *   stat                 -> ctx->drv_stats_ref
 *   stats_ctx            -> ctx->stats
 *   ae_settings          -> ctx->ae_ctl
 *   awb_settings         -> ctx->awb_ctl
 *   af_settings          -> ctx->af_ctl
 *   tune                 -> ctx->picture_ctl
 *   adjust               -> ctx->adjust_ctl
 *   defog_ctx            -> ctx->dehaze_state
 *   *_entity_ctx         -> ctx->*_entity
 *   isp_3a_change_flags  -> ctx->pending_3a_changes
 *   isp_ir_flag          -> ctx->ir_mode
 *   isp_stat_buf         -> ctx->stats_buf
 *   load_reg_base        -> ctx->reg_image
 *   ctx_lock             -> ctx->lock
 *   ops                  -> ctx->callbacks
 */
#ifndef FRAMEWORK_INTERNAL_H
#define FRAMEWORK_INTERNAL_H

#include "framework_isp.h"
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* ISO ops naming reconciliation (20 §12.2 / 22 §4)                    */
/* The ABI record for the ISO vtable is `fwi_iso_cfg_core_ops`         */
/* (fwi_iso_cfg_core_ops_t).  The frozen entity field and the iso_init */
/* prototype instead spell it `fwi_iso_core_ops_t`, which the frozen   */
/* set typedefs but never defines (opaque).  This implementation       */
/* standardises on the defined record type and converts only at that   */
/* frozen boundary, through this one accessor.                         */
static inline fwi_iso_cfg_core_ops_t *fwi_iso_ops(fwi_iso_core_ops_t *ops)
{
    return (fwi_iso_cfg_core_ops_t *)ops;
}

/* constants whose numeric value is not stated in the spec (reported) */
#define ISP_CEM_MEM_SIZE    5888
#define ISP_PLTM_MEM_SIZE   1536
#define ISP_MSC_TBL_LENGTH  1452
#define AWB_SAT_DEF_LIM     255

/* 20 §8.5 library behaviour constants (pinned values) */
#define ISP_AF_DIR_TH       24    /* M.af_cfg.af_sap_lim, §8.6 step 2 */
#define ISP_CFA_DIR_TH      2047  /* M.demosaic_cfg.dir_threshold default */
#define ISP_CFA_INTERP_MODE 1     /* M.demosaic_cfg.interp_mode default */
#define ISP_CFA_ZIG_ZAG     5     /* M.demosaic_cfg.zig_zag default */
#define H3A_PIC_OFFSET      (-1000) /* the three *_coor windows, §8.5 */
#define H3A_PIC_SIZE        2000    /* the three *_coor windows, §8.5 */
#define ISP_FREQUENCY_AUTO  3   /* V4L2_CID_POWER_LINE_FREQUENCY_AUTO */
#define ISP_FREQUENCY_DISABLED 0

/* ------------------------------------------------------------------ */
/* event loop (20 §5)                                                  */
/* ------------------------------------------------------------------ */
enum isp_watch_kind { ISP_WATCH_READ = 1, ISP_WATCH_WRITE = 2,
                      ISP_WATCH_EXCEPT = 3 };

#define ISP_MAX_WATCH 16
struct isp_event_loop {
    struct {
        int fd;
        int kind;
        void (*cb)(void *priv);
        void *priv;
    } watch[ISP_MAX_WATCH];
    int nwatch;
    int maxfd;
    int done;
    int started;
};

void isp_loop_init(struct isp_event_loop *l);
void isp_loop_start(struct isp_event_loop *l);
void isp_loop_stop(struct isp_event_loop *l);
int  isp_loop_watch(struct isp_event_loop *l, int fd, int kind,
                    void (*cb)(void *priv), void *priv);
void isp_loop_unwatch(struct isp_event_loop *l, int fd);
int  isp_loop_run(struct isp_event_loop *l);

/* ------------------------------------------------------------------ */
/* tuning store (20 §3, §10)                                           */
/* ------------------------------------------------------------------ */
struct isp_tuning_store {
    fwi_tuning_image_t params;
    pthread_mutex_t lock;
    struct hw_isp_device *dev;
    fwi_isp_ctx_t *ctx;
};

/* ------------------------------------------------------------------ */
/* internal entry points shared between the framework objects          */
/* ------------------------------------------------------------------ */
int  isp_config_init(fwi_isp_ctx_t *ctx);
int  isp_config_update(fwi_isp_ctx_t *ctx);
void isp_enable_mapping(fwi_isp_ctx_t *ctx);
void isp_library_defaults(fwi_isp_ctx_t *ctx);
void isp_module_config_derive(fwi_isp_ctx_t *ctx);
void isp_module_refresh(fwi_isp_ctx_t *ctx);
/* A-GOLD replay seam (23 §2/§5): a harness links the real kept 3A/base/
 * module-config/table tiers instead of tests/fake_tiers.c, wraps
 * isp_set_load_reg (-Wl,--wrap) to snapshot reg->size bytes before the ioctl,
 * and for each recorded frame loads stat_N into ctx->stats_buf then calls
 * isp_frame_process(ctx), which decodes it via isp_handle_stats (20 §7 step 3);
 * the wrapper output after the init call is
 * lrpre_000000 and after stat_N is lrpre_{N+1}.  The unassigned tail
 * 0xae40..0x1323f is masked in the comparison. */
void isp_frame_process(fwi_isp_ctx_t *ctx);
void isp_ae_feed(fwi_isp_ctx_t *ctx);
void isp_awb_feed(fwi_isp_ctx_t *ctx);
void isp_afs_feed(fwi_isp_ctx_t *ctx);
void isp_iso_feed(fwi_isp_ctx_t *ctx);
void isp_gtm_feed(fwi_isp_ctx_t *ctx);
void isp_pltm_feed(fwi_isp_ctx_t *ctx);
void isp_set_params_helper(fwi_isp_ctx_t *ctx, int module);
void isp_frame_process(fwi_isp_ctx_t *ctx);

struct isp_event_loop *isp_loop_for(int id);
int  isp_event_start(fwi_isp_ctx_t *ctx);
void isp_event_stop(fwi_isp_ctx_t *ctx);

struct isp_tuning_store *isp_tuning_store_get(int id);

/* control-event handler and attribute helpers (helper.c) */
void isp_handle_ctrl_event(fwi_isp_ctx_t *ctx, const struct v4l2_event *ev);
void isp_handle_frame_sync(fwi_isp_ctx_t *ctx, const uint8_t *data);

#endif /* FRAMEWORK_INTERNAL_H */
