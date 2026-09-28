/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* fcap_seam.h - host-test seam for the capture runtime: a scripted syscall
 * table for isp_dev_uapi.c, the fcap_set_clock seam
 * and scripted fisp_/framework symbols, so no device is touched. */

#ifndef FCAP_SEAM_H
#define FCAP_SEAM_H

#include "fcap_priv.h"
#include "isp_dev_uapi.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fcap_seam_rec {
	int           fd;
	unsigned long req;
	size_t        n;
	unsigned char arg[512];
};

/* Reset the mock kernel, the media topology, the record log and media_params,
 * and install the gated clock. */
void fcap_seam_reset(void);

/* Clear only the ioctl record log (keeps the live session and the feed). */
void fcap_seam_clear_log(void);

int  fcap_seam_log_len(void);
const struct fcap_seam_rec *fcap_seam_log_at(int i);
int  fcap_seam_count(unsigned long req);
const struct fcap_seam_rec *fcap_seam_nth(unsigned long req, int n);
const struct fcap_seam_rec *fcap_seam_last(unsigned long req);

/* Programmable VIDIOC_* responses. */
extern unsigned int fcap_seam_gfmt_w;
extern unsigned int fcap_seam_gfmt_h;
extern unsigned int fcap_seam_gfmt_fourcc;
extern unsigned int fcap_seam_gfmt_nplanes;
extern unsigned int fcap_seam_parm_den;
extern unsigned int fcap_seam_capturemode;
extern int32_t      fcap_seam_ctrl_value;      /* VIDIOC_G_CTRL value */
extern unsigned int fcap_seam_sensor_fps;      /* sensor_config.fps_fixed */
extern unsigned int fcap_seam_plane_len[VIDEO_MAX_PLANES];
extern unsigned int fcap_seam_plane_off[VIDEO_MAX_PLANES];
extern unsigned int fcap_seam_plane_phy[VIDEO_MAX_PLANES];
extern const char  *fcap_seam_fail_open;       /* path whose open() fails */

/* Canned capture: the worker's buffer wait succeeds while responses remain. */
void fcap_seam_feed(unsigned int index, unsigned int frame_cnt,
		    unsigned int exp_time, long sec, long usec);
int  fcap_seam_pending(void);

/* Clock control: with `on` the per-channel frame wait reports expiry at once. */
void fcap_seam_set_channel_expire(int on);

/* ISP device bound to isp id 0 via media_params.isp_dev[0]. */
extern struct hw_isp_device fcap_seam_isp;

/* fisp_ call counters recorded by the seam. */
extern int fcap_seam_isp_init_n;
extern int fcap_seam_isp_exit_n;
extern int fcap_seam_resolver_n;
extern struct fwi_video_device *(*fcap_seam_resolver_ptr)(int);

/* Paths passed to the mock open(), in order. */
int fcap_seam_open_n(void);
const char *fcap_seam_open_path(int i);

#ifdef __cplusplus
}
#endif

#endif /* FCAP_SEAM_H */
