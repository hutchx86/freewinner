// SPDX-License-Identifier: AGPL-3.0-only
/* fwi_isp_api.h - hand-written public entry points of the ISP framework and
 * utilities; types come from the generated fwi_isp_abi.h. Not generated:
 * tools/gen_abi.py only writes it when absent. */
#ifndef FWI_ISP_API_H
#define FWI_ISP_API_H

#include "media_utils_abi.h"
#include "isp_dev_uapi.h"

/* fwi_rolloff_core_ops uses fwi_rolloff_stats_t, which the generator never
 * declares; forward-declare it (pointer use only) before including it. */
typedef struct fwi_rolloff_stats fwi_rolloff_stats_t;

#include "fwi_isp_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* framework, context side                                            */
/* ------------------------------------------------------------------ */
extern fwi_isp_ctx_t isp_ctx[1];

int    media_dev_init(void);
void   media_dev_exit(void);
int    isp_init(int dev_id);
int    isp_run(int dev_id);
int    isp_stop(int dev_id);
int32_t isp_pthread_join(int dev_id);
int    isp_exit(int dev_id);

int32_t isp_get_attr_cfg(int dev_id, uint32_t ctrl_id, void *value);
int32_t isp_set_attr_cfg(int dev_id, uint32_t ctrl_id, void *value);
int32_t isp_get_lv(int dev_id);

int32_t isp_set_cfg(int dev_id, uint8_t group_id, uint32_t cfg_ids,
                    void *cfg_data);
int32_t isp_get_cfg(int dev_id, uint8_t group_id, uint32_t cfg_ids,
                    void *cfg_data);
int    isp_update(int dev_id);
int    isp_ctx_config_init(fwi_isp_ctx_t *ctx);
int    isp_ctx_config_update(fwi_isp_ctx_t *ctx);

void   isp_ae_set_params_helper(fwi_ae_entity_t *ae_ctx,
                                fwi_ae_param_type_e cmd);

int    isp_reset(int dev_id, int mode_flag);
int    isp_set_sync(int mode);

/* ------------------------------------------------------------------ */
/* framework, device layer                                            */
/* ------------------------------------------------------------------ */
extern struct hw_isp_media_dev media_params;

/* device-layer functions are declared in isp_dev_uapi.h (included above). */

/* syscall seam */
void isp_uapi_set_sys(const struct fwi_uapi_sys *sys);
const struct fwi_uapi_sys *isp_uapi_get_sys(void);
extern const struct fwi_uapi_sys *isp_uapi_sys;

/* ------------------------------------------------------------------ */
/* utilities                                                          */
/* ------------------------------------------------------------------ */
int  cdx_sem_init(cdx_sem_t *s, unsigned int val);
void cdx_sem_deinit(cdx_sem_t *s);
void cdx_sem_down(cdx_sem_t *s);
int  cdx_sem_down_timedwait(cdx_sem_t *s, unsigned int timeout_ms);
void cdx_sem_up(cdx_sem_t *s);

int  message_create(message_queue_t *q);
void message_destroy(message_queue_t *q);
int  put_message(message_queue_t *q, message_t *msg);
int  putMessageWithData(message_queue_t *q, message_t *msg);
int  get_message(message_queue_t *q, message_t *msg);
int  TMessage_WaitQueueNotEmpty(message_queue_t *q, unsigned int timeout_ms);
int  pthread_cond_wait_timeout(pthread_cond_t *c, pthread_mutex_t *m,
                               unsigned int ms);

VideoBufferManager *VideoBufMgrCreate(int frmNum, int frmSize);
void VideoBufMgrDestroy(VideoBufferManager *mgr);

/* The V4L2 <-> fwm_pixel_format_e mappers are declared in media_utils_abi.h. */

void  media_utils_log_error(const char *reason, unsigned int detail);
void *media_utils_pool_alloc(size_t size);
void  media_utils_pool_free(void *ptr);

/* isp_set_cfg/isp_get_cfg group and id values. */
#define HW_ISP_CFG_TEST          1
#define HW_ISP_CFG_3A            2
#define HW_ISP_CFG_TUNING        3
#define HW_ISP_CFG_TUNING_TABLES 4
#define HW_ISP_CFG_DYNAMIC       5

#define HW_ISP_CFG_TEST_ENABLE      0x20
#define HW_ISP_CFG_TUNING_FLICKER   0x02
#define HW_ISP_CFG_TUNING_CCM_LOW   0x80
#define HW_ISP_CFG_TUNING_CCM_MID   0x100
#define HW_ISP_CFG_TUNING_CCM_HIGH  0x200
#define HW_ISP_CFG_TUNING_PLTM      0x400
#define HW_ISP_CFG_TUNING_SHARP     0x80
/* gamma is sent under group 4 (TUNING_TABLES), id 0x02 */
#define HW_ISP_CFG_TUNING_GAMMA     0x02

/* Register-table (LCA, sharp, CCM trigger) length. */
#define ISP_REG_TBL_LENGTH  33

#ifdef __cplusplus
}
#endif
#endif /* FWI_ISP_API_H */
