/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* fcap.c - capture runtime (fcap_): the 3 AW_MPI_SYS_* and 16 AW_MPI_VI_* entry
 * points between the media daemon and the clean device layer. Device access goes only
 * through isp_dev_uapi.h; AW_MPI_ISP_* belongs to the ISP runtime (fisp_). Deliberate
 * omissions are listed in NOT-IMPLEMENTED.md. */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fisp_abi.h"      /* fisp_: AW_MPI_ISP_Init/Exit, resolver seam */
#include "fcap_priv.h"

#include "isp_dev_uapi.h"

#define FCAP_LOG(...) fprintf(stderr, "fcap: " __VA_ARGS__)

/* The framework's shared ISP media device (the one ISP_Run opened), used only by
 * SetVippShutterTime; capture keeps its own /dev/media0 handle for the video nodes. */
extern struct hw_isp_media_dev media_params;

/* MOD_ID_VIU: the media-utils ABI fixes fwm_mod_id_e's width but not its member
 * list; nothing reads the value, so a documented constant is used. */
#define FCAP_MOD_ID_VIU 16

/* Bounds on the worker's shutdown wait while a channel survives DisableVipp:
 * 100 ticks of 10 ms. */
#define FCAP_SHUTDOWN_TICKS 100

/* ------------------------------------------------------------------ */
/* Process singleton                                                   */
/* ------------------------------------------------------------------ */

enum fcap_sys_state {
	FCAP_SYS_INVALID = 0,
	FCAP_SYS_CONFIGURED,
	FCAP_SYS_STARTED
};

static struct {
	int                       state;
	MPP_SYS_CONF_S            conf;
	struct hw_isp_media_dev  *md;
} g_sys;

static struct fcap_vipp *g_vipp[VI_VIPP_NUM_MAX];

static int fcap_valid_dev(int dev)
{
	return dev >= 0 && dev < VI_VIPP_NUM_MAX;
}

static int fcap_valid_chn(int chn)
{
	return chn >= 0 && chn < VI_VIRCHN_NUM_MAX;
}

/* ------------------------------------------------------------------ */
/* Replaceable clock / wait seam                                       */
/* ------------------------------------------------------------------ */

static int fcap_default_video_wait(struct fwi_video_device *video, int timeout_ms)
{
	return video_wait_buffer(video, timeout_ms) == 0 ? 0 : -1;
}

static int fcap_default_channel_wait(struct fcap_channel *chan, int timeout_ms)
{
	if (chan == NULL)
		return -1;
	if (timeout_ms < 0) {
		cdx_sem_down(&chan->frame_sem);
		return 0;
	}
	if (timeout_ms == 0)
		return -1;
	return cdx_sem_down_timedwait(&chan->frame_sem,
				      (unsigned int)timeout_ms) == 0 ? 0 : -1;
}

static void fcap_default_delay_ms(unsigned int ms)
{
	struct timespec ts;

	ts.tv_sec = (time_t)(ms / 1000u);
	ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
	nanosleep(&ts, NULL);
}

struct fcap_clock_ops fcap_clock = {
	fcap_default_video_wait,
	fcap_default_channel_wait,
	fcap_default_delay_ms
};

void fcap_set_clock(const struct fcap_clock_ops *ops)
{
	fcap_clock.video_wait =
		(ops != NULL && ops->video_wait != NULL) ? ops->video_wait
							 : fcap_default_video_wait;
	fcap_clock.channel_wait =
		(ops != NULL && ops->channel_wait != NULL) ? ops->channel_wait
							   : fcap_default_channel_wait;
	fcap_clock.delay_ms =
		(ops != NULL && ops->delay_ms != NULL) ? ops->delay_ms
						       : fcap_default_delay_ms;
}

static int fcap_wait_video(struct fwi_video_device *video, int timeout_ms)
{
	return fcap_clock.video_wait ? fcap_clock.video_wait(video, timeout_ms)
				     : fcap_default_video_wait(video, timeout_ms);
}

static int fcap_wait_channel(struct fcap_channel *chan, int timeout_ms)
{
	return fcap_clock.channel_wait ? fcap_clock.channel_wait(chan, timeout_ms)
				       : fcap_default_channel_wait(chan, timeout_ms);
}

static void fcap_delay_ms(unsigned int ms)
{
	if (fcap_clock.delay_ms)
		fcap_clock.delay_ms(ms);
}

/* ------------------------------------------------------------------ */
/* Component facade                                                    */
/* ------------------------------------------------------------------ */

static void fcap_comp_set_state(struct fcap_component *c, int state)
{
	pthread_mutex_lock(&c->lock);
	c->state = state;
	pthread_mutex_unlock(&c->lock);
}

static int fcap_comp_state(struct fcap_component *c)
{
	int state;

	pthread_mutex_lock(&c->lock);
	state = c->state;
	pthread_mutex_unlock(&c->lock);
	return state;
}

static void *fcap_comp_worker(void *arg)
{
	struct fcap_component *c = arg;

	for (;;) {
		message_t msg;
		int stop = 0;

		(void)TMessage_WaitQueueNotEmpty(&c->cmdq, 0);
		if (get_message(&c->cmdq, &msg) != SUCCESS)
			continue;
		if (msg.mpData != NULL)
			free(msg.mpData);

		switch (msg.command) {
		case FCAP_CMD_EXIT:
			stop = 1;
			break;
		case FCAP_CMD_SET_STATE:
			fcap_comp_set_state(c, msg.para0);
			break;
		default:
			break;
		}
		if (stop)
			break;
		cdx_sem_up(&c->done);
	}
	return NULL;
}

static int fcap_comp_create(struct fcap_component *c, struct fcap_channel *chan,
			    int dev, int chn)
{
	if (message_create(&c->cmdq) != SUCCESS)
		return FAILURE;
	if (cdx_sem_init(&c->done, 0) != 0) {
		message_destroy(&c->cmdq);
		return FAILURE;
	}
	if (pthread_mutex_init(&c->lock, NULL) != 0) {
		cdx_sem_deinit(&c->done);
		message_destroy(&c->cmdq);
		return FAILURE;
	}

	c->chan = chan;
	c->chn.mModId = (fwm_mod_id_e)FCAP_MOD_ID_VIU;
	c->chn.mDevId = dev;
	c->chn.mChnId = chn;
	c->state = FCAP_ST_LOADED;

	if (pthread_create(&c->thread, NULL, fcap_comp_worker, c) != 0) {
		pthread_mutex_destroy(&c->lock);
		cdx_sem_deinit(&c->done);
		message_destroy(&c->cmdq);
		return FAILURE;
	}
	c->thread_on = 1;
	return SUCCESS;
}

static int fcap_comp_post_state(struct fcap_component *c, int state)
{
	message_t msg;

	memset(&msg, 0, sizeof msg);
	msg.command = FCAP_CMD_SET_STATE;
	msg.para0 = state;
	if (put_message(&c->cmdq, &msg) != SUCCESS)
		return FAILURE;
	cdx_sem_down(&c->done);
	return SUCCESS;
}

static void fcap_comp_destroy(struct fcap_component *c)
{
	message_t msg;

	if (!c->thread_on)
		return;

	memset(&msg, 0, sizeof msg);
	msg.command = FCAP_CMD_EXIT;
	(void)put_message(&c->cmdq, &msg);
	pthread_join(c->thread, NULL);
	c->thread_on = 0;

	cdx_sem_deinit(&c->done);
	pthread_mutex_destroy(&c->lock);
	message_destroy(&c->cmdq);
}

/* ------------------------------------------------------------------ */
/* Vipp helpers                                                        */
/* ------------------------------------------------------------------ */

static struct fcap_channel *fcap_find_chan(struct fcap_vipp *v, int chn)
{
	if (v == NULL || !fcap_valid_chn(chn))
		return NULL;
	return v->chans[chn];
}

/* Copy a caller VI_ATTR_S into the clean layer's format record.
 * The two guard fields default to 0; `index` is the vipp index. */
static void fcap_attr_to_fmt(const VI_ATTR_S *attr, struct video_fmt *fmt,
			     int index)
{
	memset(fmt, 0, sizeof *fmt);
	fmt->type = attr->type;
	fmt->memtype = attr->memtype;
	fmt->format = attr->format;
	fmt->nbufs = attr->nbufs;
	fmt->nplanes = attr->nplanes;
	fmt->fps = attr->fps;
	fmt->capturemode = attr->capturemode;
	fmt->use_current_win = attr->use_current_win;
	fmt->wdr_mode = (attr->wdr_mode <= 2u) ? attr->wdr_mode : 0u;
	fmt->drop_frame_num =
		((int)attr->drop_frame_num >= 0) ? attr->drop_frame_num : 0u;
	fmt->index = index;
}

/* Normalise a caller attribute exactly as the vendor path does before it
 * is stored and re-applied. */
static void fcap_attr_normalise(const VI_ATTR_S *in, VI_ATTR_S *out)
{
	*out = *in;
	if (out->wdr_mode > 2u)
		out->wdr_mode = 0u;
	if ((int)out->drop_frame_num < 0)
		out->drop_frame_num = 0u;
}

static struct hw_isp_device *fcap_isp_for(struct fcap_vipp *v)
{
	int id;

	if (v == NULL || v->video == NULL)
		return NULL;
	id = video_to_isp_id(v->video);
	if (id < 0 || id >= (int)HW_ISP_DEVICE_NUM)
		return NULL;
	return media_params.isp_dev[id];
}

/* ------------------------------------------------------------------ */
/* Capture worker                                                      */
/* ------------------------------------------------------------------ */

/* Build the frame descriptor handed to the channels from a dequeued buffer;
 * `mStride` carries the plane mapped length. */
static VIDEO_FRAME_INFO_S fcap_build_frame(struct fcap_vipp *v,
					   struct video_buffer *vb)
{
	VIDEO_FRAME_INFO_S fi;
	struct buffers_pool *pool = v->video->pool;
	unsigned int nplanes = v->video->nplanes;
	unsigned int i;

	memset(&fi, 0, sizeof fi);
	fi.VFrame.mWidth = v->video->format.width;
	fi.VFrame.mHeight = v->video->format.height;
	fi.VFrame.mField = (fwm_video_field_e)(int)v->attr.format.field;
	fi.VFrame.mPixelFormat =
		map_V4L2_PIX_FMT_to_PIXEL_FORMAT_E(
			(int)v->video->format.pixelformat);
	fi.VFrame.mpts = (uint64_t)vb->timestamp.tv_sec * 1000000ULL +
			 (uint64_t)vb->timestamp.tv_usec;
	fi.VFrame.mFramecnt = vb->frame_cnt;
	fi.VFrame.mExposureTime = vb->exp_time / 1000u;
	fi.mId = vb->index;

	if (nplanes > 3)
		nplanes = 3;
	for (i = 0; i < nplanes; i++) {
		/* mPhyAddr comes from the dequeue call; the mapped address and
		 * length live in the pool record. */
		fi.VFrame.mPhyAddr[i] = vb->planes[i].mem_phy;
		if (pool != NULL && vb->index < pool->nbufs &&
		    pool->buffers[vb->index].planes != NULL) {
			fi.VFrame.mpVirAddr[i] =
				pool->buffers[vb->index].planes[i].mem;
			fi.VFrame.mStride[i] =
				pool->buffers[vb->index].planes[i].size;
		}
	}
	return fi;
}

/* Long-exposure channel bookkeeping: the first frame whose PTS interval exceeds the
 * threshold is the long frame and resets the mode to AUTO. Only partly used by the daemon. */
static void fcap_le_check(struct fcap_vipp *v, struct fcap_channel *c,
			  const VIDEO_FRAME_INFO_S *fi)
{
	if (!c->le.active)
		return;

	if (c->le.have_last_pts) {
		int fps = c->le.fps > 0 ? c->le.fps : 1;
		int fc = c->le.frame_count > 0 ? c->le.frame_count : 1;
		int64_t threshold_ms = (int64_t)(1000 / fps) * fc + 500;
		int64_t delta = (int64_t)fi->VFrame.mpts - c->le.last_pts;

		if (delta > threshold_ms * 1000) {
			/* The long frame: reset the mode to AUTO. */
			c->le.active = 0;
			pthread_mutex_lock(&v->le_lock);
			v->long_exposure = 0;
			pthread_mutex_unlock(&v->le_lock);
		}
	}
	c->le.last_pts = fi->VFrame.mpts;
	c->le.have_last_pts = 1;
	c->le.frame_count++;
}

static void fcap_capture_shutdown(struct fcap_vipp *v)
{
	unsigned int i;

	/* Wait for the channel list to drain, bounded: DisableVipp proceeds with
	 * a surviving virchn. */
	for (i = 0; i < FCAP_SHUTDOWN_TICKS; i++) {
		int k, any = 0;

		pthread_mutex_lock(&v->lock);
		for (k = 0; k < VI_VIRCHN_NUM_MAX; k++)
			if (v->chans[k] != NULL)
				any = 1;
		pthread_mutex_unlock(&v->lock);
		if (!any)
			break;
		fcap_delay_ms(10);
	}

	/* Release every frame whose occupancy is still non-zero. */
	for (i = 0; i < FCAP_OCC_SLOTS; i++) {
		int release = 0;

		pthread_mutex_lock(&v->lock);
		if (v->occ[i].inuse != 0) {
			v->occ[i].inuse = 0;
			release = 1;
		}
		pthread_mutex_unlock(&v->lock);
		if (release && v->video != NULL)
			(void)video_queue_buffer(v->video, i);
	}
}

static void *fcap_capture_entry(void *arg)
{
	struct fcap_vipp *v = arg;

	while (v->enabled) {
		struct video_buffer vb;
		struct video_plane planes[VIDEO_MAX_PLANES];
		VIDEO_FRAME_INFO_S fi;
		unsigned int idx, i;
		int holders = 0;

		if (fcap_wait_video(v->video, 2000) != 0) {
			/* buffer-wait miss: count and continue (no error state) */
			continue;
		}

		memset(&vb, 0, sizeof vb);
		memset(planes, 0, sizeof planes);
		vb.nplanes = (v->video != NULL) ? v->video->nplanes : 0;
		vb.planes = planes;
		if (video_dequeue_buffer(v->video, &vb) != 0)
			continue;

		if (v->drop_remaining > 0) {
			v->drop_remaining--;
			(void)video_queue_buffer(v->video, vb.index);
			continue;
		}

		idx = vb.index;
		if (idx >= FCAP_OCC_SLOTS) {
			(void)video_queue_buffer(v->video, idx);
			continue;
		}

		pthread_mutex_lock(&v->lock);
		if (v->occ[idx].inuse != 0) {
			/* The buffer has not come back from a previous frame. */
			pthread_mutex_unlock(&v->lock);
			continue;
		}
		fi = fcap_build_frame(v, &vb);
		v->occ[idx].frame = fi;

		for (i = 0; i < VI_VIRCHN_NUM_MAX; i++) {
			struct fcap_channel *c = v->chans[i];

			if (c == NULL)
				continue;
			fcap_le_check(v, c, &fi);
			if (VideoBufMgrPushFrame(c->mgr, &fi) == SUCCESS) {
				v->occ[idx].inuse++;
				holders++;
				cdx_sem_up(&c->frame_sem);
			}
		}
		pthread_mutex_unlock(&v->lock);

		if (holders == 0) {
			/* No channel took it: release the capture-side reference. */
			(void)video_queue_buffer(v->video, idx);
		}
	}

	fcap_capture_shutdown(v);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* SYS                                                                 */
/* ------------------------------------------------------------------ */

static int fcap_media_open(void)
{
	if (g_sys.md != NULL)
		return SUCCESS;
	g_sys.md = isp_md_open(MEDIA_DEVICE_PATH);
	return g_sys.md != NULL ? SUCCESS : FAILURE;
}

static void fcap_media_close(void)
{
	if (g_sys.md != NULL) {
		isp_md_close(g_sys.md);
		g_sys.md = NULL;
	}
}

/* ISP id -> VI video device for the fisp_ control path (companion seam). */
static struct fwi_video_device *fcap_resolve_video(int isp_dev)
{
	unsigned int i;

	if (g_sys.md == NULL || isp_dev < 0)
		return NULL;
	for (i = 0; i < HW_VIDEO_DEVICE_NUM; i++) {
		struct fwi_video_device *video = g_sys.md->video_dev[i];

		if (video != NULL && video_to_isp_id(video) == isp_dev)
			return video;
	}
	return NULL;
}

AW_S32 AW_MPI_SYS_SetConf(const MPP_SYS_CONF_S *pSysConf)
{
	if (pSysConf == NULL)
		return ERR_SYS_ILLEGAL_PARAM;
	if (g_sys.state == FCAP_SYS_STARTED)
		return ERR_SYS_NOT_PERM;

	g_sys.conf = *pSysConf;
	if (g_sys.conf.mkfcTmpDir[0] == '\0') {
		memset(g_sys.conf.mkfcTmpDir, 0, sizeof g_sys.conf.mkfcTmpDir);
		strncpy(g_sys.conf.mkfcTmpDir, "/tmp",
			sizeof g_sys.conf.mkfcTmpDir - 1);
	}
	g_sys.state = FCAP_SYS_CONFIGURED;
	return SUCCESS;
}

AW_S32 AW_MPI_SYS_Init(void)
{
	if (g_sys.state == FCAP_SYS_INVALID)
		return ERR_SYS_NOTREADY;
	if (g_sys.state == FCAP_SYS_STARTED)
		return SUCCESS;

	if (fcap_media_open() != SUCCESS)
		return FAILURE;

	/* fisp_'s ISP init; its result is not checked. */
	(void)AW_MPI_ISP_Init();

	/* fcap_'s own infrastructure: bind the ISP control path to the video
	 * devices this package opens. */
	fisp_set_video_resolver(fcap_resolve_video);

	g_sys.state = FCAP_SYS_STARTED;
	return SUCCESS;
}

AW_S32 AW_MPI_SYS_Exit(void)
{
	unsigned int i;

	if (g_sys.state != FCAP_SYS_STARTED)
		return SUCCESS;

	/* Tear down any vipp the caller left behind, then the fisp_ ISP and
	 * finally the media device. */
	for (i = 0; i < VI_VIPP_NUM_MAX; i++)
		if (g_vipp[i] != NULL)
			(void)AW_MPI_VI_DestoryVipp((VI_DEV)i);

	fisp_set_video_resolver(NULL);
	(void)AW_MPI_ISP_Exit();
	fcap_media_close();
	g_sys.state = FCAP_SYS_CONFIGURED;
	return SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Vipp lifecycle                                                      */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_VI_CreateVipp(VI_DEV ViDev)
{
	struct fcap_vipp *v;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;

	v = g_vipp[ViDev];
	if (v != NULL)
		return SUCCESS;   /* idempotent */

	v = calloc(1, sizeof *v);
	if (v == NULL)
		return ERR_VI_NOMEM;

	v->dev = ViDev;
	pthread_mutex_init(&v->lock, NULL);
	pthread_mutex_init(&v->le_lock, NULL);

	if (isp_video_open(g_sys.md, (unsigned int)ViDev) != 0) {
		pthread_mutex_destroy(&v->le_lock);
		pthread_mutex_destroy(&v->lock);
		free(v);
		return FAILURE;
	}
	v->video = g_sys.md->video_dev[ViDev];
	if (v->video == NULL) {
		isp_video_close(g_sys.md, (unsigned int)ViDev);
		pthread_mutex_destroy(&v->le_lock);
		pthread_mutex_destroy(&v->lock);
		free(v);
		return FAILURE;
	}

	if (v->top_clk_pending) {
		(void)video_set_top_clk(v->video, v->top_clk);
		v->top_clk_pending = 0;
	}

	g_vipp[ViDev] = v;
	return SUCCESS;
}

/* Close the video node and free the manager. */
static void fcap_vipp_teardown(struct fcap_vipp *v)
{
	if (v == NULL)
		return;

	if (v->enabled) {
		v->enabled = 0;
		if (v->worker_on) {
			pthread_join(v->worker, NULL);
			v->worker_on = 0;
		}
		if (v->pool != NULL) {
			(void)video_stream_off(v->video);
			(void)video_free_buffers(v->video);
		}
	}
	if (v->video != NULL) {
		if (v->video->pool != NULL)
			buffers_pool_delete(v->video);
		isp_video_close(g_sys.md, (unsigned int)v->dev);
		v->video = NULL;
	}
	v->pool = NULL;
	pthread_mutex_destroy(&v->le_lock);
	pthread_mutex_destroy(&v->lock);
	g_vipp[v->dev] = NULL;
	free(v);
}

AW_S32 AW_MPI_VI_DestoryVipp(VI_DEV ViDev)
{
	struct fcap_vipp *v;
	unsigned int i;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;
	v = g_vipp[ViDev];
	if (v == NULL)
		return ERR_VI_UNEXIST;

	for (i = 0; i < VI_VIRCHN_NUM_MAX; i++) {
		if (v->chans[i] != NULL) {
			FCAP_LOG("DestoryVipp(%d): virchn %u still present\n",
				 ViDev, i);
			/* Drive it through a legal transition before destroying it,
			 * so the component worker is always joined. */
			(void)AW_MPI_VI_DisableVirChn(ViDev, (VI_CHN)i);
			(void)AW_MPI_VI_DestoryVirChn(ViDev, (VI_CHN)i);
		}
	}
	fcap_vipp_teardown(v);
	return SUCCESS;
}

AW_S32 AW_MPI_VI_SetVippAttr(VI_DEV ViDev, VI_ATTR_S *pstAttr)
{
	struct fcap_vipp *v;
	struct video_fmt fmt;
	VI_ATTR_S norm;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;
	v = g_vipp[ViDev];
	if (v == NULL || v->video == NULL)
		return ERR_VI_UNEXIST;
	if (pstAttr == NULL)
		return ERR_VI_NULL_PTR;

	fcap_attr_normalise(pstAttr, &norm);
	fcap_attr_to_fmt(&norm, &fmt, ViDev);

	if (video_set_fmt(v->video, &fmt) != 0) {
		/* The one driver error the vendor path maps: the
		 * soc-ft high-resolution 30 fps clamp. */
		if (norm.format.width >= 3840u && norm.format.height >= 2160u &&
		    norm.fps > 25u)
			return ERR_VI_EIS_EFUSE_ERR;
		return FAILURE;
	}

	v->attr = norm;
	v->have_attr = 1;
	return SUCCESS;
}

AW_S32 AW_MPI_VI_GetVippAttr(VI_DEV ViDev, VI_ATTR_S *pstAttr)
{
	struct fcap_vipp *v;
	struct video_fmt fmt;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;
	v = g_vipp[ViDev];
	if (v == NULL || v->video == NULL)
		return ERR_VI_UNEXIST;
	if (pstAttr == NULL)
		return ERR_VI_NULL_PTR;

	if (video_get_fmt(v->video, &fmt) != 0)
		return FAILURE;

	pstAttr->type = fmt.type;
	pstAttr->memtype = fmt.memtype;
	pstAttr->format = fmt.format;
	pstAttr->nbufs = fmt.nbufs;
	pstAttr->nplanes = fmt.nplanes;
	pstAttr->fps = fmt.fps;
	pstAttr->capturemode = fmt.capturemode;
	pstAttr->use_current_win = fmt.use_current_win;
	pstAttr->wdr_mode = fmt.wdr_mode;
	pstAttr->drop_frame_num = fmt.drop_frame_num;
	return SUCCESS;
}

AW_S32 AW_MPI_VI_EnableVipp(VI_DEV ViDev)
{
	struct fcap_vipp *v;
	struct buffers_pool *pool;
	struct video_fmt fmt;
	unsigned int i;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;
	v = g_vipp[ViDev];
	if (v == NULL || v->video == NULL)
		return ERR_VI_UNEXIST;
	if (v->enabled)
		return SUCCESS;

	pool = buffers_pool_new(v->video);
	if (pool == NULL)
		return FAILURE;
	v->pool = pool;

	if (video_req_buffers(v->video, pool) != 0)
		goto fail;

	/* Re-read the format: the device call that issues G_FMT and refreshes
	 * the cached negotiated record. */
	if (v->have_attr) {
		fcap_attr_to_fmt(&v->attr, &fmt, ViDev);
		if (video_set_fmt(v->video, &fmt) != 0)
			goto fail;
	}

	for (i = 0; i < pool->nbufs; i++) {
		if (video_queue_buffer(v->video, i) != 0)
			goto fail;
	}

	if (video_stream_on(v->video) != 0)
		goto fail;

	v->drop_remaining = (int)v->attr.drop_frame_num;
	v->enabled = 1;
	if (pthread_create(&v->worker, NULL, fcap_capture_entry, v) != 0) {
		v->enabled = 0;
		goto fail;
	}
	v->worker_on = 1;
	return SUCCESS;

fail:
	v->enabled = 0;
	(void)video_stream_off(v->video);
	(void)video_free_buffers(v->video);
	if (v->video->pool != NULL)
		buffers_pool_delete(v->video);
	v->pool = NULL;
	return FAILURE;
}

AW_S32 AW_MPI_VI_DisableVipp(VI_DEV ViDev)
{
	struct fcap_vipp *v;
	AW_S32 rc = SUCCESS;
	int i;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;
	v = g_vipp[ViDev];
	if (v == NULL || v->video == NULL)
		return ERR_VI_UNEXIST;
	if (!v->enabled)
		return SUCCESS;

	fcap_delay_ms(5);

	for (i = 0; i < VI_VIRCHN_NUM_MAX; i++)
		if (v->chans[i] != NULL)
			FCAP_LOG("DisableVipp(%d): virchn %d still present\n",
				 ViDev, i);

	v->enabled = 0;
	if (v->worker_on) {
		pthread_join(v->worker, NULL);
		v->worker_on = 0;
	}

	if (video_stream_off(v->video) != 0)
		rc = FAILURE;
	if (video_free_buffers(v->video) != 0)
		rc = FAILURE;
	if (v->video->pool != NULL)
		buffers_pool_delete(v->video);
	v->pool = NULL;
	return rc;
}

/* ------------------------------------------------------------------ */
/* Orientation                                                         */
/* ------------------------------------------------------------------ */

static AW_S32 fcap_set_orientation(VI_DEV ViDev, int cid, int Value)
{
	struct fcap_vipp *v;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;
	v = g_vipp[ViDev];
	if (v == NULL || v->video == NULL)
		return ERR_VI_UNEXIST;

	if (Value != 0 && Value != 1)
		return SUCCESS;   /* silently accepted, no write */

	return video_set_control(v->video, cid, Value) == 0 ? SUCCESS
							    : FAILURE;
}

AW_S32 AW_MPI_VI_SetVippMirror(VI_DEV ViDev, int Value)
{
	return fcap_set_orientation(ViDev, V4L2_CID_HFLIP, Value);
}

AW_S32 AW_MPI_VI_SetVippFlip(VI_DEV ViDev, int Value)
{
	return fcap_set_orientation(ViDev, V4L2_CID_VFLIP, Value);
}

/* ------------------------------------------------------------------ */
/* Shutter time                                                        */
/* ------------------------------------------------------------------ */

/* Vipp-level application; caller holds no lock. */
static AW_S32 fcap_shutter_apply(struct fcap_vipp *v,
				 const VI_SHUTTIME_CFG_S *t)
{
	struct hw_isp_device *isp = fcap_isp_for(v);
	struct sensor_config cfg;
	struct sensor_fps sfps;
	int mode = (int)t->eShutterMode;
	int fps;
	AW_S32 rc = SUCCESS;

	pthread_mutex_lock(&v->le_lock);

	if (t->iTime == 0 && mode != VI_SHUTTIME_MODE_AUTO) {
		rc = FAILURE;
		goto out;
	}
	if (isp == NULL) {
		FCAP_LOG("shutter: no ISP device for this vipp\n");
		rc = FAILURE;
		goto out;
	}

	if (v->long_exposure && mode == VI_SHUTTIME_MODE_NIGHT_VIEW) {
		rc = ERR_VI_BUSY;
		goto out;
	}

	memset(&cfg, 0, sizeof cfg);
	if (isp_sensor_get_configs(isp, &cfg) != 0) {
		rc = FAILURE;
		goto out;
	}
	fps = (int)cfg.fps_fixed;

	if (mode == VI_SHUTTIME_MODE_AUTO) {
		if (video_set_control(v->video, V4L2_CID_EXPOSURE_AUTO, 0) != 0) {
			rc = FAILURE;
			goto out;
		}
		if (video_set_control(v->video, V4L2_CID_AUTOGAIN, 1) != 0) {
			rc = FAILURE;
			goto out;
		}
		memset(&sfps, 0, sizeof sfps);
		sfps.fps = fps;
		if (isp_sensor_set_fps(isp, &sfps) != 0) {
			rc = FAILURE;
			goto out;
		}
		v->long_exposure = 0;
	} else if (mode == VI_SHUTTIME_MODE_PREVIEW) {
		if (fps <= t->iTime) {
			int gain = 0, exposure = 0, newexp, newgain;

			if (video_get_control(v->video, V4L2_CID_GAIN,
					      &gain) != 0 ||
			    video_get_control(v->video,
					      V4L2_CID_EXPOSURE_ABSOLUTE,
					      &exposure) != 0) {
				rc = FAILURE;
				goto out;
			}
			if (video_set_control(v->video, V4L2_CID_EXPOSURE_AUTO,
					      1) != 0 ||
			    video_set_control(v->video, V4L2_CID_AUTOGAIN,
					      0) != 0) {
				rc = FAILURE;
				goto out;
			}
			newexp = 1000000 / t->iTime;
			newgain = (newexp != 0)
					  ? gain * exposure / newexp
					  : gain;
			if (newgain < 16)
				newgain = 16;
			newgain *= 3;
			if (video_set_control(v->video,
					      V4L2_CID_EXPOSURE_ABSOLUTE,
					      newexp) != 0 ||
			    video_set_control(v->video, V4L2_CID_GAIN,
					      newgain) != 0) {
				rc = FAILURE;
				goto out;
			}
		}
	} else if (mode == VI_SHUTTIME_MODE_NIGHT_VIEW) {
		if (fps > t->iTime) {
			int gain = 0, exposure = 0, newexp, newgain;

			if (video_get_control(v->video, V4L2_CID_GAIN,
					      &gain) != 0 ||
			    video_get_control(v->video,
					      V4L2_CID_EXPOSURE_ABSOLUTE,
					      &exposure) != 0) {
				rc = FAILURE;
				goto out;
			}
			if (video_set_control(v->video, V4L2_CID_EXPOSURE_AUTO,
					      1) != 0 ||
			    video_set_control(v->video, V4L2_CID_AUTOGAIN,
					      0) != 0) {
				rc = FAILURE;
				goto out;
			}
			newexp = (t->iTime > 0) ? 1000000 / t->iTime
						: 1000000 * (-t->iTime);
			newgain = (newexp != 0)
					  ? gain * exposure / newexp
					  : gain;
			if (newgain < 16)
				newgain = 16;
			newgain *= 3;
			if (video_set_control(v->video,
					      V4L2_CID_EXPOSURE_ABSOLUTE,
					      newexp) != 0 ||
			    video_set_control(v->video, V4L2_CID_GAIN,
					      newgain) != 0) {
				rc = FAILURE;
				goto out;
			}
			memset(&sfps, 0, sizeof sfps);
			sfps.fps = t->iTime;
			if (isp_sensor_set_fps(isp, &sfps) != 0) {
				rc = FAILURE;
				goto out;
			}
			v->long_exposure = 1;
		}
	} else {
		rc = FAILURE;
	}

out:
	pthread_mutex_unlock(&v->le_lock);
	return rc;
}

/* Channel-half bookkeeping for modes other than PREVIEW. */
static void fcap_shutter_record_chan(struct fcap_vipp *v, int chn,
				     const VI_SHUTTIME_CFG_S *t, int fps)
{
	struct fcap_channel *c;

	if ((int)t->eShutterMode == VI_SHUTTIME_MODE_PREVIEW)
		return;

	pthread_mutex_lock(&v->lock);
	c = v->chans[chn];
	if (c != NULL) {
		int st = c->comp.state;

		if (st == FCAP_ST_IDLE || st == FCAP_ST_EXECUTING ||
		    st == FCAP_ST_PAUSE) {
			pthread_mutex_lock(&c->lock);
			c->le.reset_mode = (int)t->eResetMode;
			c->le.fps = fps;
			c->le.frame_count = 1;
			if ((int)t->eShutterMode == VI_SHUTTIME_MODE_AUTO) {
				c->le.active = 0;
			} else {
				if (!c->le.active) {
					/* Entering long exposure: return any
					 * queued frames to the capture side. */
					VIDEO_FRAME_INFO_S *f;

					while ((f = VideoBufMgrGetAllValidUsingFrame(
							c->mgr)) != NULL) {
						unsigned int idx = f->mId;

						if (idx < FCAP_OCC_SLOTS &&
						    v->occ[idx].inuse > 0) {
							v->occ[idx].inuse--;
							if (v->occ[idx].inuse == 0)
								(void)video_queue_buffer(
									v->video,
									idx);
						}
					}
				}
				c->le.active = 1;
				c->le.have_last_pts = 0;
			}
			pthread_mutex_unlock(&c->lock);
		}
	}
	pthread_mutex_unlock(&v->lock);
}

AW_S32 AW_MPI_VI_SetVippShutterTime(VI_DEV ViDev, VI_SHUTTIME_CFG_S *pTime)
{
	struct fcap_vipp *v;
	struct hw_isp_device *isp;
	struct sensor_config cfg;
	int fps = 0;
	AW_S32 rc;

	if (!fcap_valid_dev(ViDev))
		return ERR_VI_INVALID_DEVID;
	v = g_vipp[ViDev];
	if (v == NULL)
		return ERR_VI_UNEXIST;
	if (pTime == NULL)
		return ERR_VI_NULL_PTR;

	rc = fcap_shutter_apply(v, pTime);

	isp = fcap_isp_for(v);
	if (isp != NULL && isp_sensor_get_configs(isp, &cfg) == 0)
		fps = (int)cfg.fps_fixed;
	fcap_shutter_record_chan(v, 0, pTime, fps);

	return rc;
}

/* ------------------------------------------------------------------ */
/* Virtual channels                                                    */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_VI_CreateVirChn(VI_DEV ViDev, VI_CHN ViCh, void *pAttr)
{
	struct fcap_vipp *v;
	struct fcap_channel *c;
	AW_S32 rc;

	(void)pAttr;   /* ignored */

	if (!fcap_valid_dev(ViDev) || !fcap_valid_chn(ViCh))
		return ERR_VI_INVALID_CHNID;
	v = g_vipp[ViDev];
	if (v == NULL || !v->enabled)
		return ERR_VI_UNEXIST;
	if (v->chans[ViCh] != NULL)
		return ERR_VI_EXIST;

	c = calloc(1, sizeof *c);
	if (c == NULL)
		return ERR_VI_NOMEM;
	c->dev = ViDev;
	c->chn = ViCh;
	c->key = (ViDev << 16) | ViCh;
	c->vipp = v;
	pthread_mutex_init(&c->lock, NULL);
	c->mgr = VideoBufMgrCreate(VI_FIFO_LEVEL, 0);
	if (c->mgr == NULL) {
		pthread_mutex_destroy(&c->lock);
		free(c);
		return ERR_VI_NOMEM;
	}
	if (cdx_sem_init(&c->frame_sem, 0) != 0) {
		VideoBufMgrDestroy(c->mgr);
		pthread_mutex_destroy(&c->lock);
		free(c);
		return FAILURE;
	}

	/* Step 2-4: create the component, install the identity and hand it the
	 * vipp's current attribute. */
	if (fcap_comp_create(&c->comp, c, ViDev, ViCh) != SUCCESS) {
		cdx_sem_deinit(&c->frame_sem);
		VideoBufMgrDestroy(c->mgr);
		pthread_mutex_destroy(&c->lock);
		free(c);
		return FAILURE;
	}
	if (v->have_attr) {
		c->attr = v->attr;
		c->have_attr = 1;
	}

	/* Step 5: drive Loaded -> Idle and wait. */
	rc = fcap_comp_post_state(&c->comp, FCAP_ST_IDLE);
	if (rc != SUCCESS) {
		fcap_comp_destroy(&c->comp);
		cdx_sem_deinit(&c->frame_sem);
		VideoBufMgrDestroy(c->mgr);
		pthread_mutex_destroy(&c->lock);
		free(c);
		return rc;
	}

	/* Step 6: publish to the vipp's channel list. */
	v->chans[ViCh] = c;
	return rc;
}

AW_S32 AW_MPI_VI_DestoryVirChn(VI_DEV ViDev, VI_CHN ViCh)
{
	struct fcap_vipp *v;
	struct fcap_channel *c;
	int st;

	if (!fcap_valid_dev(ViDev) || !fcap_valid_chn(ViCh))
		return ERR_VI_INVALID_CHNID;
	v = g_vipp[ViDev];
	if (v == NULL)
		return ERR_VI_UNEXIST;
	c = v->chans[ViCh];
	if (c == NULL)
		return ERR_VI_UNEXIST;

	st = fcap_comp_state(&c->comp);
	if (st == FCAP_ST_IDLE) {
		if (fcap_comp_post_state(&c->comp, FCAP_ST_LOADED) != SUCCESS)
			return FAILURE;
	} else if (st != FCAP_ST_LOADED && st != FCAP_ST_INVALID) {
		return ERR_VI_BUSY;
	}

	v->chans[ViCh] = NULL;
	fcap_comp_destroy(&c->comp);
	cdx_sem_deinit(&c->frame_sem);
	VideoBufMgrDestroy(c->mgr);
	pthread_mutex_destroy(&c->lock);
	free(c);
	return SUCCESS;
}

AW_S32 AW_MPI_VI_EnableVirChn(VI_DEV ViDev, VI_CHN ViCh)
{
	struct fcap_vipp *v;
	struct fcap_channel *c;
	int st;

	if (!fcap_valid_dev(ViDev) || !fcap_valid_chn(ViCh))
		return ERR_VI_INVALID_CHNID;
	v = g_vipp[ViDev];
	if (v == NULL)
		return ERR_VI_UNEXIST;
	c = v->chans[ViCh];
	if (c == NULL)
		return ERR_VI_UNEXIST;

	st = fcap_comp_state(&c->comp);
	if (st == FCAP_ST_IDLE) {
		if (fcap_comp_post_state(&c->comp, FCAP_ST_EXECUTING) != SUCCESS)
			return FAILURE;
	}
	return SUCCESS;
}

AW_S32 AW_MPI_VI_DisableVirChn(VI_DEV ViDev, VI_CHN ViCh)
{
	struct fcap_vipp *v;
	struct fcap_channel *c;
	int st;

	if (!fcap_valid_dev(ViDev) || !fcap_valid_chn(ViCh))
		return ERR_VI_INVALID_CHNID;
	v = g_vipp[ViDev];
	if (v == NULL)
		return ERR_VI_UNEXIST;
	c = v->chans[ViCh];
	if (c == NULL)
		return ERR_VI_UNEXIST;

	st = fcap_comp_state(&c->comp);
	if (st == FCAP_ST_EXECUTING || st == FCAP_ST_PAUSE) {
		if (fcap_comp_post_state(&c->comp, FCAP_ST_IDLE) != SUCCESS)
			return FAILURE;
	} else if (st != FCAP_ST_IDLE) {
		FCAP_LOG("DisableVirChn(%d,%d): state %d is not a run state\n",
			 ViDev, ViCh, st);
	}
	return SUCCESS;
}

AW_S32 AW_MPI_VI_SetVirChnAttr(VI_DEV ViDev, VI_CHN ViCh, void *pAttr)
{
	(void)pAttr;   /* the caller passes NULL; the body is a no-op */

	if (!fcap_valid_dev(ViDev) || !fcap_valid_chn(ViCh))
		return ERR_VI_INVALID_CHNID;
	return SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Frame capture                                                       */
/* ------------------------------------------------------------------ */

AW_S32 AW_MPI_VI_GetFrame(VI_DEV ViDev, VI_CHN ViCh,
			  VIDEO_FRAME_INFO_S *pstFrameInfo, AW_S32 s32MilliSec)
{
	struct fcap_vipp *v;
	struct fcap_channel *c;
	VIDEO_FRAME_INFO_S *f;
	int st;

	if (!fcap_valid_dev(ViDev) || !fcap_valid_chn(ViCh))
		return ERR_VI_INVALID_CHNID;
	if (pstFrameInfo == NULL)
		return ERR_VI_NULL_PTR;

	v = g_vipp[ViDev];
	c = fcap_find_chan(v, ViCh);
	if (c == NULL)
		return ERR_VI_UNEXIST;

	st = fcap_comp_state(&c->comp);
	if (st != FCAP_ST_EXECUTING && st != FCAP_ST_IDLE)
		return ERR_VI_NOT_PERM;

	f = VideoBufMgrGetValidFrame(c->mgr);
	if (f == NULL) {
		if (s32MilliSec == 0)
			return FAILURE;
		if (fcap_wait_channel(c, (int)s32MilliSec) != 0)
			return FAILURE;
		f = VideoBufMgrGetValidFrame(c->mgr);
		if (f == NULL)
			return FAILURE;
	}

	*pstFrameInfo = *f;
	pstFrameInfo->VFrame.mOffsetTop = 0;
	pstFrameInfo->VFrame.mOffsetBottom = (short)f->VFrame.mHeight;
	pstFrameInfo->VFrame.mOffsetLeft = 0;
	pstFrameInfo->VFrame.mOffsetRight = (short)f->VFrame.mWidth;
	return SUCCESS;
}

AW_S32 AW_MPI_VI_ReleaseFrame(VI_DEV ViDev, VI_CHN ViCh,
			      VIDEO_FRAME_INFO_S *pstFrameInfo)
{
	struct fcap_vipp *v;
	struct fcap_channel *c;
	unsigned int idx;
	AW_S32 rc = SUCCESS;
	int st;

	if (!fcap_valid_dev(ViDev) || !fcap_valid_chn(ViCh))
		return ERR_VI_INVALID_CHNID;
	if (pstFrameInfo == NULL)
		return ERR_VI_NULL_PTR;

	v = g_vipp[ViDev];
	c = fcap_find_chan(v, ViCh);
	if (c == NULL)
		return ERR_VI_UNEXIST;

	st = fcap_comp_state(&c->comp);
	if (st != FCAP_ST_EXECUTING && st != FCAP_ST_IDLE)
		return ERR_VI_NOT_PERM;

	/* Return the driver buffer once every holder has released it. */
	idx = pstFrameInfo->mId;
	if (idx < FCAP_OCC_SLOTS && v->video != NULL) {
		pthread_mutex_lock(&v->lock);
		if (v->occ[idx].inuse > 0) {
			v->occ[idx].inuse--;
			if (v->occ[idx].inuse == 0) {
				if (video_queue_buffer(v->video, idx) != 0)
					rc = FAILURE;
			}
		} else {
			FCAP_LOG("ReleaseFrame(%d,%d): index %u not held\n",
				 ViDev, ViCh, idx);
			rc = FAILURE;
		}
		pthread_mutex_unlock(&v->lock);
	} else if (idx >= FCAP_OCC_SLOTS) {
		rc = FAILURE;
	}

	if (VideoBufMgrReleaseFrame(c->mgr, pstFrameInfo) != SUCCESS)
		rc = FAILURE;

	return rc;
}
