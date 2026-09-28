/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Recorder seam for the fisp_ host tests. See fisp_seam.h. */

#define _GNU_SOURCE

#include "fisp_seam.h"

#include "framework_isp.h"
#include "isp_dev_uapi.h"

#include <string.h>

#define SEAM_LOG_MAX 256

static struct fisp_seam_call g_log[SEAM_LOG_MAX];
static int g_log_n;

int fisp_seam_isp_init_rc;
int fisp_seam_isp_run_rc;
int fisp_seam_lv;
int fisp_seam_attr_set_rc;
int fisp_seam_attr_get_rc;
int fisp_seam_attr_get_value;
int fisp_seam_s_ctrl_rc;
int fisp_seam_g_ctrl_rc;
int fisp_seam_g_ctrl_value;

struct fwi_video_device fisp_seam_video0;

/* The clean framework's device-layer global; the default resolver scans it. The
 * tests install their own resolver, so the slots stay empty. */
struct hw_isp_media_dev media_params;

static void seam_log(int op, long a, long b, long c)
{
	if (g_log_n < SEAM_LOG_MAX) {
		g_log[g_log_n].op = op;
		g_log[g_log_n].a = a;
		g_log[g_log_n].b = b;
		g_log[g_log_n].c = c;
		g_log_n++;
	}
}

void fisp_seam_reset(void)
{
	g_log_n = 0;
	fisp_seam_isp_init_rc = 0;
	fisp_seam_isp_run_rc = 0;
	fisp_seam_lv = 42;
	fisp_seam_attr_set_rc = 0;
	fisp_seam_attr_get_rc = 0;
	fisp_seam_attr_get_value = 7;
	fisp_seam_s_ctrl_rc = 0;
	fisp_seam_g_ctrl_rc = 0;
	fisp_seam_g_ctrl_value = 7;
}

int fisp_seam_log_len(void)
{
	return g_log_n;
}

const struct fisp_seam_call *fisp_seam_log_at(int i)
{
	if (i < 0 || i >= g_log_n)
		return NULL;
	return &g_log[i];
}

int fisp_seam_count(int op)
{
	int i, n = 0;

	for (i = 0; i < g_log_n; i++)
		if (g_log[i].op == op)
			n++;
	return n;
}

int fisp_seam_has(int op)
{
	return fisp_seam_count(op) != 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

int media_dev_init(void)
{
	seam_log(FISP_OP_MEDIA_DEV_INIT, 0, 0, 0);
	return 0;
}

void media_dev_exit(void)
{
	seam_log(FISP_OP_MEDIA_DEV_EXIT, 0, 0, 0);
}

int isp_init(int dev_id)
{
	seam_log(FISP_OP_ISP_INIT, dev_id, 0, 0);
	return fisp_seam_isp_init_rc;
}

int isp_run(int dev_id)
{
	seam_log(FISP_OP_ISP_RUN, dev_id, 0, 0);
	return fisp_seam_isp_run_rc;
}

int isp_stop(int dev_id)
{
	seam_log(FISP_OP_ISP_STOP, dev_id, 0, 0);
	return 0;
}

int32_t isp_pthread_join(int dev_id)
{
	seam_log(FISP_OP_ISP_JOIN, dev_id, 0, 0);
	return 0;
}

int isp_exit(int dev_id)
{
	seam_log(FISP_OP_ISP_EXIT, dev_id, 0, 0);
	return 0;
}

int32_t isp_get_lv(int dev_id)
{
	seam_log(FISP_OP_ISP_GET_LV, dev_id, 0, 0);
	return fisp_seam_lv;
}

/* ------------------------------------------------------------------ */
/* ISP_CTRL_* attribute path                                           */
/* ------------------------------------------------------------------ */

int32_t isp_set_attr_cfg(int dev_id, uint32_t ctrl_id, void *value)
{
	seam_log(FISP_OP_ATTR_SET, dev_id, (long)ctrl_id,
		 value != NULL ? (long)*(int32_t *)value : 0);
	return fisp_seam_attr_set_rc;
}

int32_t isp_get_attr_cfg(int dev_id, uint32_t ctrl_id, void *value)
{
	seam_log(FISP_OP_ATTR_GET, dev_id, (long)ctrl_id, 0);
	if (value != NULL)
		*(int32_t *)value = fisp_seam_attr_get_value;
	return fisp_seam_attr_get_rc;
}

/* ------------------------------------------------------------------ */
/* Device layer                                                        */
/* ------------------------------------------------------------------ */

struct fwi_video_device *fisp_seam_resolver(int isp_dev)
{
	seam_log(FISP_OP_RESOLVE, isp_dev, 0, 0);
	if (isp_dev == 0)
		return &fisp_seam_video0;
	return NULL;
}

int video_set_control(struct fwi_video_device *video, int cmd, int value)
{
	seam_log(FISP_OP_S_CTRL, cmd, value, video == NULL ? 0 : 1);
	return fisp_seam_s_ctrl_rc;
}

int video_get_control(struct fwi_video_device *video, int cmd, int *value)
{
	seam_log(FISP_OP_G_CTRL, cmd, 0, video == NULL ? 0 : 1);
	if (value != NULL)
		*value = fisp_seam_g_ctrl_value;
	return fisp_seam_g_ctrl_rc;
}

int video_to_isp_id(struct fwi_video_device *video)
{
	if (video == NULL)
		return -1;
	return (int)video->isp_id;
}
