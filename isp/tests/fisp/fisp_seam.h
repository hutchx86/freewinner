/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Recorder seam for the fisp_ host tests (SPEC 7.1). Every framework-tier and
 * device-layer symbol fisp_ consumes is defined here as a scriptable recorder,
 * so the tests can assert the exact call, control id, value and order without a
 * device, the framework tier or a camera. */

#ifndef FREEISP_FISP_SEAM_H
#define FREEISP_FISP_SEAM_H

#include "fisp_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

enum fisp_seam_op {
	FISP_OP_MEDIA_DEV_INIT = 1,
	FISP_OP_MEDIA_DEV_EXIT,
	FISP_OP_ISP_INIT,
	FISP_OP_ISP_RUN,
	FISP_OP_ISP_STOP,
	FISP_OP_ISP_JOIN,
	FISP_OP_ISP_EXIT,
	FISP_OP_ISP_GET_LV,
	FISP_OP_ATTR_SET,
	FISP_OP_ATTR_GET,
	FISP_OP_S_CTRL,
	FISP_OP_G_CTRL,
	FISP_OP_RESOLVE,
};

struct fisp_seam_call {
	int  op;
	long a;         /* op-dependent: dev / ctrl id / value */
	long b;
	long c;
};

/* Scripted results; reset to the defaults below by fisp_seam_reset(). */
extern int fisp_seam_isp_init_rc;       /* default 0 */
extern int fisp_seam_isp_run_rc;        /* default 0 */
extern int fisp_seam_lv;                /* default 42 */
extern int fisp_seam_attr_set_rc;       /* default 0 */
extern int fisp_seam_attr_get_rc;       /* default 0 */
extern int fisp_seam_attr_get_value;    /* default 7 */
extern int fisp_seam_s_ctrl_rc;         /* default 0 */
extern int fisp_seam_g_ctrl_rc;         /* default 0 */
extern int fisp_seam_g_ctrl_value;      /* default 7 */

/* Sentinel VI device handed out for ISP id 0; NULL for id 1. */
extern struct isp_video_device fisp_seam_video0;
struct isp_video_device *fisp_seam_resolver(int isp_dev);

void fisp_seam_reset(void);

int  fisp_seam_log_len(void);
const struct fisp_seam_call *fisp_seam_log_at(int i);
int  fisp_seam_count(int op);
int  fisp_seam_has(int op);

#ifdef __cplusplus
}
#endif

#endif /* FREEISP_FISP_SEAM_H */
