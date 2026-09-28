// SPDX-License-Identifier: AGPL-3.0-only
/* events.c - event loop, subscriptions and the subdev/stats handlers */
#include "framework_internal.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

static struct fwi_event_loop loops[HW_ISP_DEVICE_NUM];

struct fwi_event_loop *isp_loop_for(int id)
{
    return &loops[id];
}

void isp_loop_init(struct fwi_event_loop *l)
{
    memset(l, 0, sizeof(*l));
    l->maxfd = -1;
}

void isp_loop_start(struct fwi_event_loop *l)
{
    l->done = 0;
    l->started = 1;
}

void isp_loop_stop(struct fwi_event_loop *l)
{
    l->done = 1;
}

int isp_loop_watch(struct fwi_event_loop *l, int fd, int kind,
                   void (*cb)(void *priv), void *priv)
{
    int i;
    for (i = 0; i < l->nwatch; i++)
        if (l->watch[i].fd == fd) {
            l->watch[i].kind = kind;
            l->watch[i].cb = cb;
            l->watch[i].priv = priv;
            return 0;
        }
    if (l->nwatch >= ISP_MAX_WATCH)
        return -1;
    l->watch[l->nwatch].fd = fd;
    l->watch[l->nwatch].kind = kind;
    l->watch[l->nwatch].cb = cb;
    l->watch[l->nwatch].priv = priv;
    l->nwatch++;
    if (fd > l->maxfd)
        l->maxfd = fd;
    return 0;
}

void isp_loop_unwatch(struct fwi_event_loop *l, int fd)
{
    int i, j;
    for (i = 0; i < l->nwatch; i++) {
        if (l->watch[i].fd == fd) {
            for (j = i; j < l->nwatch - 1; j++)
                l->watch[j] = l->watch[j + 1];
            l->nwatch--;
            break;
        }
    }
    l->maxfd = -1;
    for (i = 0; i < l->nwatch; i++)
        if (l->watch[i].fd > l->maxfd)
            l->maxfd = l->watch[i].fd;
}

int isp_loop_run(struct fwi_event_loop *l)
{
    while (!l->done) {
        fd_set r, w, e;
        struct timeval tv;
        int i, ret;
        int ready[ISP_MAX_WATCH];
        int nready = 0;

        FD_ZERO(&r);
        FD_ZERO(&w);
        FD_ZERO(&e);
        for (i = 0; i < l->nwatch; i++) {
            if (l->watch[i].kind == ISP_WATCH_READ)
                FD_SET(l->watch[i].fd, &r);
            else if (l->watch[i].kind == ISP_WATCH_WRITE)
                FD_SET(l->watch[i].fd, &w);
            else
                FD_SET(l->watch[i].fd, &e);
        }
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        ret = isp_uapi_sys->select(l->maxfd + 1, &r, &w, &e, &tv);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (ret == 0)
            break; /* 3 s of silence is fatal */
        for (i = 0; i < l->nwatch; i++) {
            if (l->watch[i].kind == ISP_WATCH_READ)
                ready[nready] = FD_ISSET(l->watch[i].fd, &r);
            else if (l->watch[i].kind == ISP_WATCH_WRITE)
                ready[nready] = FD_ISSET(l->watch[i].fd, &w);
            else
                ready[nready] = FD_ISSET(l->watch[i].fd, &e);
            nready++;
        }
        for (i = 0; i < nready && !l->done; i++)
            if (ready[i] && l->watch[i].cb)
                l->watch[i].cb(l->watch[i].priv);
    }
    return !l->done;
}

/* ------------------------------------------------------------------ */
/* subscriptions                                                       */
/* ------------------------------------------------------------------ */
static const int ctrl_ids[] = {
    0x00980900, 0x00980901, 0x00980902, 0x00980903, 0x0098090c, 0x00980911,
    0x00980912, 0x00980913, 0x00980918, 0x00980919, 0x0098091a, 0x0098091b,
    0x0098091d, 0x0098091f, 0x00980920, 0x00980921, 0x00980925, 0x00980926,
    0x009a0901, 0x009a0902, 0x009a0903, 0x009a090a, 0x009a090b, 0x009a090c,
    0x009a0913, 0x009a0914, 0x009a0915, 0x009a0916, 0x009a0917, 0x009a0918,
    0x009a0919, 0x009a091a, 0x009a091b, 0x009a091c, 0x009a091d, 0x009a091f,
    0x0098195f, 0x00981960, 0x00981961, 0x00981962, 0x00981963, 0x00981964,
    0x00981965, 0x00981966,
};

static void subdev_handler(void *priv);
static void stats_handler(void *priv);

int isp_event_start(fwi_isp_ctx_t *ctx)
{
    struct hw_isp_device *dev = media_params.isp_dev[ctx->isp_index];
    struct fwi_event_loop *l = isp_loop_for(ctx->isp_index);
    struct v4l2_event_subscription sub;
    unsigned int en = 1;
    unsigned int i;

    if (dev == NULL)
        return -1;

    memset(&sub, 0, sizeof(sub));
    sub.type = V4L2_EVENT_FRAME_SYNC;
    sub.id = 0;
    if (isp_uapi_sys->ioctl(dev->subdev.fd, VIDIOC_SUBSCRIBE_EVENT, &sub) != 0)
        fprintf(stderr, "isp: subscribe frame_sync failed\n");
    sub.type = V4L2_EVENT_VIN_ISP_OFF;
    if (isp_uapi_sys->ioctl(dev->subdev.fd, VIDIOC_SUBSCRIBE_EVENT, &sub) != 0)
        fprintf(stderr, "isp: subscribe isp_off failed\n");
    sub.type = V4L2_EVENT_CTRL;
    for (i = 0; i < sizeof(ctrl_ids) / sizeof(ctrl_ids[0]); i++) {
        sub.id = (uint32_t)ctrl_ids[i];
        if (isp_uapi_sys->ioctl(dev->subdev.fd, VIDIOC_SUBSCRIBE_EVENT,
                                &sub) != 0)
            fprintf(stderr, "isp: subscribe ctrl %#x failed\n", ctrl_ids[i]);
    }
    isp_loop_watch(l, dev->subdev.fd, ISP_WATCH_EXCEPT, subdev_handler, ctx);

    memset(&sub, 0, sizeof(sub));
    sub.type = V4L2_EVENT_VIN_H3A;
    sub.id = 0;
    if (isp_uapi_sys->ioctl(dev->stat.fd, VIDIOC_SUBSCRIBE_EVENT, &sub) != 0)
        fprintf(stderr, "isp: subscribe h3a failed\n");
    if (isp_uapi_sys->ioctl(dev->stat.fd, VIDIOC_VIN_ISP_STAT_EN, &en) != 0)
        fprintf(stderr, "isp: stat_en failed\n");
    isp_loop_watch(l, dev->stat.fd, ISP_WATCH_EXCEPT, stats_handler, ctx);
    return 0;
}

void isp_event_stop(fwi_isp_ctx_t *ctx)
{
    struct hw_isp_device *dev = media_params.isp_dev[ctx->isp_index];
    struct fwi_event_loop *l = isp_loop_for(ctx->isp_index);
    struct v4l2_event_subscription sub;
    unsigned int en = 0;

    if (dev == NULL)
        return;
    isp_loop_unwatch(l, dev->subdev.fd);
    memset(&sub, 0, sizeof(sub));
    sub.type = V4L2_EVENT_ALL;
    isp_uapi_sys->ioctl(dev->subdev.fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
    isp_loop_unwatch(l, dev->stat.fd);
    isp_uapi_sys->ioctl(dev->stat.fd, VIDIOC_VIN_ISP_STAT_EN, &en);
    isp_uapi_sys->ioctl(dev->stat.fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
}

static void subdev_handler(void *priv)
{
    fwi_isp_ctx_t *ctx = priv;
    struct hw_isp_device *dev = media_params.isp_dev[ctx->isp_index];
    struct fwi_event_loop *l = isp_loop_for(ctx->isp_index);
    struct v4l2_event ev;

    if (dev == NULL)
        return;
    memset(&ev, 0, sizeof(ev));
    if (isp_uapi_sys->ioctl(dev->subdev.fd, VIDIOC_DQEVENT, &ev) != 0) {
        fprintf(stderr, "isp: dqevent subdev failed\n");
        return;
    }
    switch (ev.type) {
    case V4L2_EVENT_CTRL:
        isp_handle_ctrl_event(ctx, &ev);
        break;
    case V4L2_EVENT_FRAME_SYNC:
        dev->load_type = ev.u.data[0];
        isp_handle_frame_sync(ctx, ev.u.data);
        break;
    case V4L2_EVENT_VIN_ISP_OFF:
        isp_loop_stop(l);
        break;
    default:
        fprintf(stderr, "isp: unhandled event %#x\n", ev.type);
        break;
    }
}

static void stats_handler(void *priv)
{
    fwi_isp_ctx_t *ctx = priv;
    struct hw_isp_device *dev = media_params.isp_dev[ctx->isp_index];
    struct v4l2_event ev;
    struct fwi_stat_req req;
    struct fwi_stat_event *se;

    if (dev == NULL)
        return;
    memset(&ev, 0, sizeof(ev));
    if (isp_uapi_sys->ioctl(dev->stat.fd, VIDIOC_DQEVENT, &ev) != 0) {
        fprintf(stderr, "isp: dqevent stat failed\n");
        return;
    }
    se = (struct fwi_stat_event *)ev.u.data;
    if (se->buf_err != 0) {
        fprintf(stderr, "isp: stats buffer error\n");
        return;
    }
    /* Stats request: zero it and set only buf/buf_size. frame_number and config_counter stay
     * zero; filling them from the dequeued event made no difference on camera. */
    memset(&req, 0, sizeof(req));
    req.buf = dev->stats_buf;
    req.buf_size = dev->stats_size;
    if (isp_uapi_sys->ioctl(dev->stat.fd, VIDIOC_VIN_ISP_STAT_REQ, &req) != 0) {
        fprintf(stderr, "isp: stat_req failed\n");
        return;
    }
    ctx->stats_buf = dev->stats_buf;
    isp_frame_process(ctx);
}
