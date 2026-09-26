/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/*
 * fcap_priv.h - internal objects and seams of the capture runtime (package A,
 * `fcap_`).  Not part of the exported ABI: the four objects of SPEC 4.1
 * (process singleton, vipp object, channel object, capture worker) and the
 * replaceable clock seam of SPEC 7.
 *
 * The clock seam is the SPEC 7.2 hook: the capture worker's buffer wait and the
 * per-channel frame wait are driven through `fcap_clock` so a host test needs
 * never sleep the production 2000 ms.  A NULL field falls back to the built-in
 * implementation (the clean device layer's `video_wait_buffer` and the clean
 * `cdx_sem_*`), so a test may override just the wait it cares about.
 */

#ifndef FREEWINNER_FCAP_PRIV_H
#define FREEWINNER_FCAP_PRIV_H

#include <pthread.h>

#include "fcap_abi.h"
#include "utils/frame_pool.h"
#include "utils/msgqueue.h"
#include "utils/semaphore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Component states (SPEC 3.3): Loaded -> Idle -> Executing <-> Pause. */
enum fcap_state {
	FCAP_ST_INVALID = 0,
	FCAP_ST_LOADED,
	FCAP_ST_IDLE,
	FCAP_ST_EXECUTING,
	FCAP_ST_PAUSE
};

/* Internal command ids for the component command queue. */
enum fcap_cmd {
	FCAP_CMD_SET_STATE = 1,
	FCAP_CMD_EXIT
};

struct fcap_vipp;
struct fcap_channel;

/* Minimal one-channel-per-vipp component facade (SPEC 4.1/4.3). */
struct fcap_component {
	message_queue_t      cmdq;
	cdx_sem_t            done;
	pthread_t            thread;
	int                  thread_on;
	pthread_mutex_t      lock;        /* guards `state` */
	int                  state;
	MPP_CHN_S            chn;         /* {MOD_ID_VIU, ViDev, ViCh} */
	struct fcap_channel *chan;
};

/* Per-channel long-exposure bookkeeping (SPEC 3.2.7, channel half). */
struct fcap_le {
	int     active;
	int     fps;           /* sensor fps captured at entry */
	int     frame_count;   /* tracked frame count */
	int     reset_mode;    /* VI_SHUTTIME_RESET_E */
	int64_t last_pts;
	int     have_last_pts;
};

/* Channel object (SPEC 4.1 item 3). */
struct fcap_channel {
	int                  dev;
	int                  chn;
	int                  key;          /* (ViDev<<16)|ViCh */
	struct fcap_vipp    *vipp;
	struct fcap_component comp;
	VideoBufferManager  *mgr;          /* frame FIFO, depth VI_FIFO_LEVEL */
	cdx_sem_t            frame_sem;    /* capture worker -> GetFrame */
	pthread_mutex_t      lock;
	VI_ATTR_S            attr;         /* vipp attribute handed in at create */
	int                  have_attr;
	struct fcap_le       le;
};

/* Vipp object (SPEC 4.1 item 2). */
struct fcap_vipp {
	int                       dev;
	struct isp_video_device  *video;
	VI_ATTR_S                 attr;
	int                       have_attr;
	int                       enabled;
	pthread_mutex_t           lock;
	struct fcap_channel      *chans[VI_VIRCHN_NUM_MAX];
	pthread_t                 worker;
	int                       worker_on;
	int                       drop_remaining;
	struct buffers_pool      *pool;
	pthread_mutex_t           le_lock;
	int                       long_exposure;
	unsigned int              top_clk;
	int                       top_clk_pending;

	/* Occupancy table indexed by driver buffer index (SPEC 4.3). */
	struct {
		int                inuse;
		VIDEO_FRAME_INFO_S frame;
	} occ[FCAP_OCC_SLOTS];
};

/* ------------------------------------------------------------------ */
/* Replaceable clock / wait seam (SPEC 7)                              */
/* ------------------------------------------------------------------ */

struct fcap_clock_ops {
	/* Wait up to timeout_ms for a captured buffer: 0 ready, -1 timeout.
	 * Default: clean `video_wait_buffer`. */
	int  (*video_wait)(struct isp_video_device *video, int timeout_ms);
	/* Wait for a channel frame: 0 signalled, -1 timeout/expired; a negative
	 * timeout means block indefinitely.  Default: the channel frame
	 * semaphore. */
	int  (*channel_wait)(struct fcap_channel *chan, int timeout_ms);
	/* Millisecond delay.  Default: nanosleep. */
	void (*delay_ms)(unsigned int ms);
};

extern struct fcap_clock_ops fcap_clock;

/* A NULL argument (or a NULL field) restores the built-in implementation. */
void fcap_set_clock(const struct fcap_clock_ops *ops);

#ifdef __cplusplus
}
#endif

#endif /* FREEWINNER_FCAP_PRIV_H */
