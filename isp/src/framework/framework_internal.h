// SPDX-License-Identifier: AGPL-3.0-only
/* framework_internal.h - private framework declarations. SDK context names map to
 * fwi_isp_ctx_t fields as listed in docs/shim-mappings.md (framework context). */
#ifndef FRAMEWORK_INTERNAL_H
#define FRAMEWORK_INTERNAL_H

#include "framework_isp.h"
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* ISO ops naming reconciliation                                       */
/* The ISO vtable record is fwi_iso_cfg_core_ops_t, but the entity field and iso_init
 * use the opaque fwi_iso_core_ops_t; convert only here, at that ABI boundary. */
static inline fwi_iso_cfg_core_ops_t *fwi_iso_ops(fwi_iso_core_ops_t *ops)
{
    return (fwi_iso_cfg_core_ops_t *)ops;
}

/* constants whose numeric value is not stated in the spec (reported) */
#define ISP_CEM_MEM_SIZE    5888
#define ISP_PLTM_MEM_SIZE   1536
#define ISP_MSC_TBL_LENGTH  1452
#define AWB_SAT_DEF_LIM     255

/* library behaviour constants (pinned values) */
#define ISP_AF_DIR_TH       24    /* M.af_cfg.af_sap_lim */
#define ISP_CFA_DIR_TH      2047  /* M.demosaic_cfg.dir_threshold default */
#define ISP_CFA_INTERP_MODE 1     /* M.demosaic_cfg.interp_mode default */
#define ISP_CFA_ZIG_ZAG     5     /* M.demosaic_cfg.zig_zag default */
#define H3A_PIC_OFFSET      (-1000) /* the three *_coor windows */
#define H3A_PIC_SIZE        2000    /* the three *_coor windows */
#define ISP_FREQUENCY_AUTO  3   /* V4L2_CID_POWER_LINE_FREQUENCY_AUTO */
#define ISP_FREQUENCY_DISABLED 0

/* ------------------------------------------------------------------ */
/* event loop                                                          */
/* ------------------------------------------------------------------ */
enum isp_watch_kind { ISP_WATCH_READ = 1, ISP_WATCH_WRITE = 2,
                      ISP_WATCH_EXCEPT = 3 };

#define ISP_MAX_WATCH 16
struct fwi_event_loop {
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

void isp_loop_init(struct fwi_event_loop *l);
void isp_loop_start(struct fwi_event_loop *l);
void isp_loop_stop(struct fwi_event_loop *l);
int  isp_loop_watch(struct fwi_event_loop *l, int fd, int kind,
                    void (*cb)(void *priv), void *priv);
void isp_loop_unwatch(struct fwi_event_loop *l, int fd);
int  isp_loop_run(struct fwi_event_loop *l);

/* ------------------------------------------------------------------ */
/* tuning store                                                        */
/* ------------------------------------------------------------------ */
struct fwi_tuning_store {
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
/* Replay seam: with the real tiers linked and isp_set_load_reg wrapped, load a recorded
 * stats buffer into ctx->stats_buf and call this; it decodes via isp_handle_stats. */
void isp_frame_process(fwi_isp_ctx_t *ctx);
void isp_ae_feed(fwi_isp_ctx_t *ctx);
void isp_awb_feed(fwi_isp_ctx_t *ctx);
void isp_afs_feed(fwi_isp_ctx_t *ctx);
void isp_iso_feed(fwi_isp_ctx_t *ctx);
void isp_gtm_feed(fwi_isp_ctx_t *ctx);
void isp_pltm_feed(fwi_isp_ctx_t *ctx);
void isp_frame_process(fwi_isp_ctx_t *ctx);

struct fwi_event_loop *isp_loop_for(int id);
int  isp_event_start(fwi_isp_ctx_t *ctx);
void isp_event_stop(fwi_isp_ctx_t *ctx);

struct fwi_tuning_store *isp_tuning_store_get(int id);

/* control-event handler and attribute helpers (helper.c) */
void isp_handle_ctrl_event(fwi_isp_ctx_t *ctx, const struct v4l2_event *ev);
void isp_handle_frame_sync(fwi_isp_ctx_t *ctx, const uint8_t *data);

#endif /* FRAMEWORK_INTERNAL_H */
