/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host-test seam for the capture runtime (package A).  See fcap_seam.h. */

#define _GNU_SOURCE

#include "fcap_seam.h"

#include "fisp_abi.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <time.h>

/* SoC "FT zone" ioctl on /dev/sunxi_soc_info (20-framework.md §4.5).  Unit F's
 * isp_dev_uapi.c now issues the raw ioctl number (5) rather than a named
 * macro from the public header; the seam mock keeps a private alias so this
 * file stays readable. */
#define CHECK_SOC_FT_ZONE 5

/* ------------------------------------------------------------------ */
/* Record log                                                          */
/* ------------------------------------------------------------------ */

#define SEAM_LOG_MAX 4096

static struct fcap_seam_rec g_log[SEAM_LOG_MAX];
static int g_log_n;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

int fcap_seam_log_len(void)
{
	int n;

	pthread_mutex_lock(&g_log_lock);
	n = g_log_n;
	pthread_mutex_unlock(&g_log_lock);
	return n;
}

void fcap_seam_clear_log(void)
{
	pthread_mutex_lock(&g_log_lock);
	g_log_n = 0;
	pthread_mutex_unlock(&g_log_lock);
}

const struct fcap_seam_rec *fcap_seam_log_at(int i)
{
	if (i < 0 || i >= g_log_n)
		return NULL;
	return &g_log[i];
}

int fcap_seam_count(unsigned long req)
{
	int i, n = 0;

	pthread_mutex_lock(&g_log_lock);
	for (i = 0; i < g_log_n; i++)
		if (g_log[i].req == req)
			n++;
	pthread_mutex_unlock(&g_log_lock);
	return n;
}

const struct fcap_seam_rec *fcap_seam_nth(unsigned long req, int n)
{
	int i, k = 0;

	for (i = 0; i < g_log_n; i++) {
		if (g_log[i].req == req && k++ == n)
			return &g_log[i];
	}
	return NULL;
}

const struct fcap_seam_rec *fcap_seam_last(unsigned long req)
{
	int i;

	for (i = g_log_n - 1; i >= 0; i--)
		if (g_log[i].req == req)
			return &g_log[i];
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Programmable device state                                           */
/* ------------------------------------------------------------------ */

unsigned int fcap_seam_gfmt_w;
unsigned int fcap_seam_gfmt_h;
unsigned int fcap_seam_gfmt_fourcc;
unsigned int fcap_seam_gfmt_nplanes;
unsigned int fcap_seam_parm_den;
unsigned int fcap_seam_capturemode;
int32_t      fcap_seam_ctrl_value;
unsigned int fcap_seam_sensor_fps;
unsigned int fcap_seam_plane_len[VIDEO_MAX_PLANES];
unsigned int fcap_seam_plane_off[VIDEO_MAX_PLANES];
unsigned int fcap_seam_plane_phy[VIDEO_MAX_PLANES];
const char  *fcap_seam_fail_open;

struct hw_isp_media_dev media_params;
struct hw_isp_device fcap_seam_isp;

int fcap_seam_isp_init_n;
int fcap_seam_isp_exit_n;
int fcap_seam_resolver_n;
struct isp_video_device *(*fcap_seam_resolver_ptr)(int);

/* ------------------------------------------------------------------ */
/* Canned DQBUF responses                                              */
/* ------------------------------------------------------------------ */

#define SEAM_FEED_MAX 64

struct seam_feed {
	unsigned int index;
	unsigned int frame_cnt;
	unsigned int exp_time;
	long         sec;
	long         usec;
};

static struct seam_feed g_feed[SEAM_FEED_MAX];
static int g_feed_head;
static int g_feed_n;
static pthread_mutex_t g_feed_lock = PTHREAD_MUTEX_INITIALIZER;

void fcap_seam_feed(unsigned int index, unsigned int frame_cnt,
		    unsigned int exp_time, long sec, long usec)
{
	pthread_mutex_lock(&g_feed_lock);
	if (g_feed_n < SEAM_FEED_MAX) {
		int tail = (g_feed_head + g_feed_n) % SEAM_FEED_MAX;

		g_feed[tail].index = index;
		g_feed[tail].frame_cnt = frame_cnt;
		g_feed[tail].exp_time = exp_time;
		g_feed[tail].sec = sec;
		g_feed[tail].usec = usec;
		g_feed_n++;
	}
	pthread_mutex_unlock(&g_feed_lock);
}

int fcap_seam_pending(void)
{
	int n;

	pthread_mutex_lock(&g_feed_lock);
	n = g_feed_n;
	pthread_mutex_unlock(&g_feed_lock);
	return n;
}

static int seam_feed_pop(struct seam_feed *out)
{
	int rc = -1;

	pthread_mutex_lock(&g_feed_lock);
	if (g_feed_n > 0) {
		*out = g_feed[g_feed_head];
		g_feed_head = (g_feed_head + 1) % SEAM_FEED_MAX;
		g_feed_n--;
		rc = 0;
	}
	pthread_mutex_unlock(&g_feed_lock);
	return rc;
}

/* ------------------------------------------------------------------ */
/* Clock seam                                                          */
/* ------------------------------------------------------------------ */

static int g_channel_expire;

static int seam_video_wait(struct isp_video_device *video, int timeout_ms)
{
	struct timespec ts;

	(void)video;
	(void)timeout_ms;
	if (fcap_seam_pending() > 0)
		return 0;
	ts.tv_sec = 0;
	ts.tv_nsec = 1000000L;
	nanosleep(&ts, NULL);
	return -1;
}

static int seam_channel_expire(struct fcap_channel *chan, int timeout_ms)
{
	(void)chan;
	(void)timeout_ms;
	return -1;
}

static void seam_delay(unsigned int ms)
{
	(void)ms;
}

static void seam_install_clock(void)
{
	struct fcap_clock_ops ops;

	ops.video_wait = seam_video_wait;
	ops.channel_wait = g_channel_expire ? seam_channel_expire : NULL;
	ops.delay_ms = seam_delay;
	fcap_set_clock(&ops);
}

void fcap_seam_set_channel_expire(int on)
{
	g_channel_expire = on;
	seam_install_clock();
}

/* ------------------------------------------------------------------ */
/* Mock syscall table                                                  */
/* ------------------------------------------------------------------ */

static char g_open_paths[64][80];
static int g_open_n;

static int mock_open(const char *path, int flags, unsigned int mode)
{
	(void)flags;
	(void)mode;
	if (g_open_n < 64) {
		strncpy(g_open_paths[g_open_n], path, sizeof(g_open_paths[0]) - 1);
		g_open_paths[g_open_n][sizeof(g_open_paths[0]) - 1] = '\0';
	}
	g_open_n++;
	if (fcap_seam_fail_open != NULL &&
	    strcmp(path, fcap_seam_fail_open) == 0)
		return -1;
	/* A distinct, positive fd per open; none of the assertions depend on a
	 * fixed number. */
	return 200 + (int)strlen(path) + (flags & 0x3);
}

int fcap_seam_open_n(void)
{
	return g_open_n;
}

const char *fcap_seam_open_path(int i)
{
	if (i < 0 || i >= g_open_n || i >= 64)
		return NULL;
	return g_open_paths[i];
}

static int mock_close(int fd)
{
	(void)fd;
	return 0;
}

static int mock_ioctl(int fd, unsigned long req, void *arg);

static void *mock_mmap(void *addr, size_t length, int prot, int flags,
		       int fd, long offset)
{
	static int n;

	(void)addr;
	(void)prot;
	(void)flags;
	(void)fd;
	(void)offset;
	n++;
	return (void *)(uintptr_t)(0x400000 + (size_t)n * 0x10000 + length);
}

static int mock_munmap(void *addr, size_t length)
{
	(void)addr;
	(void)length;
	return 0;
}

static int mock_select(int nfds, fd_set *r, fd_set *w, fd_set *e,
		       struct timeval *tv)
{
	(void)nfds;
	(void)r;
	(void)w;
	(void)e;
	(void)tv;
	return 1;
}

static int mock_access(const char *path, int mode)
{
	(void)path;
	(void)mode;
	return 0;
}

static int mock_system(const char *command)
{
	(void)command;
	return 0;
}

static long mock_readlink(const char *path, char *buf, size_t bufsiz)
{
	if (strcmp(path, "/sys/dev/char/81:0") == 0 && bufsiz > 18) {
		strcpy(buf, "../../dev/video0");
		return 17;
	}
	return -1;
}

static int mock_stat(const char *path, struct stat *st)
{
	if (strcmp(path, "/dev/video0") == 0) {
		memset(st, 0, sizeof *st);
		st->st_rdev = makedev(81, 0);
		return 0;
	}
	return -1;
}

static const struct isp_uapi_sys g_mock_sys = {
	mock_open, mock_close, mock_ioctl, mock_mmap, mock_munmap, mock_select,
	mock_access, mock_system, mock_readlink, mock_stat,
};

/* ------------------------------------------------------------------ */
/* Media topology                                                      */
/* ------------------------------------------------------------------ */

struct ent_script {
	uint32_t id;
	const char *name;
	uint32_t type;
	uint32_t major;
	uint32_t minor;
	uint16_t pads;
	uint16_t links;
	uint32_t pad_flags[2];
};

static const struct ent_script g_ents[] = {
	{ 1, "vin_video0", MEDIA_ENT_T_DEVNODE | 1, 81, 0, 1, 0,
	  { MEDIA_PAD_FL_SINK, 0 } },
	{ 2, "sunxi_isp.0", MEDIA_ENT_T_V4L2_SUBDEV | 1, 0, 0, 2, 2,
	  { MEDIA_PAD_FL_SINK, MEDIA_PAD_FL_SOURCE } },
	{ 3, "sunxi_csi.0", MEDIA_ENT_T_V4L2_SUBDEV | 2, 0, 0, 2, 2,
	  { MEDIA_PAD_FL_SINK, MEDIA_PAD_FL_SOURCE } },
	{ 4, "ov5640", MEDIA_ENT_T_V4L2_SUBDEV_SENSOR, 0, 0, 1, 1,
	  { MEDIA_PAD_FL_SOURCE, 0 } },
	{ 5, "sunxi_h3a.0", MEDIA_ENT_T_V4L2_SUBDEV | 3, 0, 0, 1, 0,
	  { MEDIA_PAD_FL_SINK, 0 } },
};

#define N_ENTS ((int)(sizeof(g_ents) / sizeof(g_ents[0])))

struct link_script {
	uint32_t src_ent, src_pad, sink_ent, sink_pad, flags;
};

static const struct link_script g_links[] = {
	{ 2, 1, 1, 0, 1 },
	{ 3, 1, 2, 0, 1 },
	{ 4, 0, 3, 0, 1 },
};

#define N_LINKS ((int)(sizeof(g_links) / sizeof(g_links[0])))

static int mock_ioctl(int fd, unsigned long req, void *arg)
{
	size_t n = _IOC_SIZE(req);

	if (req == CHECK_SOC_FT_ZONE)
		n = 9;
	if (n > 512)
		n = 512;

	pthread_mutex_lock(&g_log_lock);
	if (g_log_n < SEAM_LOG_MAX) {
		g_log[g_log_n].fd = fd;
		g_log[g_log_n].req = req;
		g_log[g_log_n].n = n;
		if (arg != NULL && n > 0)
			memcpy(g_log[g_log_n].arg, arg, n);
		g_log_n++;
	}
	pthread_mutex_unlock(&g_log_lock);

	switch (req) {
	case MEDIA_IOC_DEVICE_INFO:
		return 0;
	case MEDIA_IOC_ENUM_ENTITIES: {
		struct media_entity_desc *d = arg;
		int next = (d->id & MEDIA_ENT_ID_FLAG_NEXT) != 0;
		uint32_t id = d->id & ~MEDIA_ENT_ID_FLAG_NEXT;
		int ei = -1, j;

		for (j = 0; j < N_ENTS; j++) {
			if (next ? (g_ents[j].id > id) : (g_ents[j].id == id)) {
				ei = j;
				break;
			}
		}
		if (ei < 0)
			return -1;
		memset(d, 0, sizeof *d);
		d->id = g_ents[ei].id;
		strncpy(d->name, g_ents[ei].name, sizeof(d->name) - 1);
		d->type = g_ents[ei].type;
		d->pads = g_ents[ei].pads;
		d->links = g_ents[ei].links;
		d->v4l.major = g_ents[ei].major;
		d->v4l.minor = g_ents[ei].minor;
		return 0;
	}
	case MEDIA_IOC_ENUM_LINKS: {
		struct media_links_enum *en = arg;
		unsigned int j;
		int li, ei = -1, k = 0;

		for (j = 0; j < (unsigned int)N_ENTS; j++)
			if (g_ents[j].id == en->entity)
				ei = (int)j;
		if (ei < 0)
			return -1;
		if (en->pads != NULL)
			for (j = 0; j < g_ents[ei].pads; j++)
				en->pads[j].flags = g_ents[ei].pad_flags[j];
		for (li = 0; li < N_LINKS; li++) {
			if (g_links[li].src_ent != en->entity &&
			    g_links[li].sink_ent != en->entity)
				continue;
			en->links[k].source.entity = g_links[li].src_ent;
			en->links[k].source.index = g_links[li].src_pad;
			en->links[k].sink.entity = g_links[li].sink_ent;
			en->links[k].sink.index = g_links[li].sink_pad;
			en->links[k].flags = g_links[li].flags;
			k++;
		}
		return 0;
	}
	case VIDIOC_S_INPUT:
		return 0;
	case VIDIOC_S_PARM:
		return 0;
	case VIDIOC_G_PARM: {
		struct v4l2_streamparm *p = arg;

		memset(&p->parm, 0, sizeof p->parm);
		p->parm.capture.capturemode = fcap_seam_capturemode;
		p->parm.capture.timeperframe.numerator = 1;
		p->parm.capture.timeperframe.denominator = fcap_seam_parm_den;
		return 0;
	}
	case VIDIOC_S_FMT:
		return 0;
	case VIDIOC_G_FMT: {
		struct v4l2_format *f = arg;

		memset(&f->fmt, 0, sizeof f->fmt);
		f->fmt.pix_mp.width = fcap_seam_gfmt_w;
		f->fmt.pix_mp.height = fcap_seam_gfmt_h;
		f->fmt.pix_mp.pixelformat = fcap_seam_gfmt_fourcc;
		f->fmt.pix_mp.num_planes = (uint8_t)fcap_seam_gfmt_nplanes;
		return 0;
	}
	case VIDIOC_REQBUFS:
		return 0;
	case VIDIOC_QUERYBUF: {
		struct v4l2_buffer *b = arg;
		unsigned int j;

		if (b->m.planes != NULL) {
			for (j = 0; j < VIDEO_MAX_PLANES; j++) {
				b->m.planes[j].length = fcap_seam_plane_len[j];
				b->m.planes[j].m.mem_offset =
					fcap_seam_plane_off[j];
			}
		}
		return 0;
	}
	case VIDIOC_QBUF:
		return 0;
	case VIDIOC_DQBUF: {
		struct v4l2_buffer *b = arg;
		struct seam_feed f;
		unsigned int j;

		if (seam_feed_pop(&f) != 0)
			return -1;
		b->index = f.index;
		b->bytesused = 0;
		b->reserved = f.frame_cnt;
		b->reserved2 = f.exp_time;
		b->flags = 0;
		b->timestamp.tv_sec = f.sec;
		b->timestamp.tv_usec = f.usec;
		if (b->m.planes != NULL)
			for (j = 0; j < VIDEO_MAX_PLANES; j++)
				b->m.planes[j].m.mem_offset =
					fcap_seam_plane_phy[j];
		return 0;
	}
	case VIDIOC_STREAMON:
	case VIDIOC_STREAMOFF:
		return 0;
	case VIDIOC_G_CTRL: {
		struct v4l2_control *c = arg;

		c->value = fcap_seam_ctrl_value;
		return 0;
	}
	case VIDIOC_S_CTRL:
		return 0;
	case VIDIOC_VIN_SENSOR_CFG_REQ: {
		struct sensor_config *cfg = arg;

		memset(cfg, 0, sizeof *cfg);
		cfg->fps_fixed = fcap_seam_sensor_fps;
		return 0;
	}
	case VIDIOC_VIN_SENSOR_SET_FPS:
		return 0;
	case CHECK_SOC_FT_ZONE:
		return -1;
	default:
		return -1;
	}
}

/* ------------------------------------------------------------------ */
/* Package-B and framework externals                                   */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_ISP_Init(void)
{
	fcap_seam_isp_init_n++;
	return SUCCESS;
}

AW_S32 AW_MPI_ISP_Exit(void)
{
	fcap_seam_isp_exit_n++;
	return SUCCESS;
}

void fisp_set_video_resolver(struct isp_video_device *(*resolver)(int))
{
	fcap_seam_resolver_n++;
	fcap_seam_resolver_ptr = resolver;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */

void fcap_seam_reset(void)
{
	pthread_mutex_lock(&g_log_lock);
	g_log_n = 0;
	pthread_mutex_unlock(&g_log_lock);

	pthread_mutex_lock(&g_feed_lock);
	g_feed_head = 0;
	g_feed_n = 0;
	pthread_mutex_unlock(&g_feed_lock);

	fcap_seam_gfmt_w = 1920;
	fcap_seam_gfmt_h = 1080;
	fcap_seam_gfmt_fourcc = V4L2_PIX_FMT_LBC_2_5X;
	fcap_seam_gfmt_nplanes = 2;
	fcap_seam_parm_den = 20;
	fcap_seam_capturemode = 2;
	fcap_seam_ctrl_value = 0;
	fcap_seam_sensor_fps = 20;
	memset(fcap_seam_plane_len, 0, sizeof fcap_seam_plane_len);
	memset(fcap_seam_plane_off, 0, sizeof fcap_seam_plane_off);
	memset(fcap_seam_plane_phy, 0, sizeof fcap_seam_plane_phy);
	fcap_seam_fail_open = NULL;

	memset(&media_params, 0, sizeof media_params);
	memset(&fcap_seam_isp, 0, sizeof fcap_seam_isp);
	fcap_seam_isp.sensor.fd = 900;
	fcap_seam_isp.subdev.fd = 901;
	fcap_seam_isp.stat.fd = 902;
	media_params.isp_dev[0] = &fcap_seam_isp;

	fcap_seam_isp_init_n = 0;
	fcap_seam_isp_exit_n = 0;
	fcap_seam_resolver_n = 0;
	fcap_seam_resolver_ptr = NULL;
	g_open_n = 0;

	g_channel_expire = 0;
	isp_uapi_set_sys(&g_mock_sys);
	seam_install_clock();
}
