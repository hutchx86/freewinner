// SPDX-License-Identifier: AGPL-3.0-only
/* isp_dev_uapi.c - project device layer.
 * Compiled as the long-enum translation unit.
 */
#include "isp_dev_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/sysmacros.h>

/* ------------------------------------------------------------------ */
/* default libc syscall table                                          */
/* ------------------------------------------------------------------ */
static int d_open(const char *p, int f, unsigned int m)
{
    return open(p, f, m);
}
static int d_close(int fd)
{
    return close(fd);
}
static int d_ioctl(int fd, unsigned long req, void *arg)
{
    return ioctl(fd, req, arg);
}
static void *d_mmap(void *a, size_t l, int p, int f, int fd, long o)
{
    return mmap(a, l, p, f, fd, o);
}
static int d_munmap(void *a, size_t l)
{
    return munmap(a, l);
}
static int d_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
    return select(n, r, w, e, t);
}
static int d_access(const char *p, int m)
{
    return access(p, m);
}
static int d_system(const char *c)
{
    return system(c);
}
static long d_readlink(const char *p, char *b, size_t s)
{
    return readlink(p, b, s);
}
static int d_stat(const char *p, struct stat *st)
{
    return stat(p, st);
}

static const struct fwi_uapi_sys default_sys = {
    d_open, d_close, d_ioctl, d_mmap, d_munmap,
    d_select, d_access, d_system, d_readlink, d_stat,
};

const struct fwi_uapi_sys *isp_uapi_sys = &default_sys;

void isp_uapi_set_sys(const struct fwi_uapi_sys *sys)
{
    isp_uapi_sys = sys ? sys : &default_sys;
}

const struct fwi_uapi_sys *isp_uapi_get_sys(void)
{
    return isp_uapi_sys;
}

#define SYS isp_uapi_sys

/* ------------------------------------------------------------------ */
/* media graph                                                         */
/* ------------------------------------------------------------------ */
static struct media_entity *find_entity_by_id(struct media_device *md,
                                              unsigned int id)
{
    unsigned int i;
    for (i = 0; i < md->num_entities; i++)
        if (md->entities[i].info.id == id)
            return &md->entities[i];
    return NULL;
}

static struct media_entity *find_entity_by_name(struct media_device *md,
                                                const char *name)
{
    unsigned int i;
    for (i = 0; i < md->num_entities; i++)
        if (strcmp(md->entities[i].info.name, name) == 0)
            return &md->entities[i];
    return NULL;
}

static struct media_pad *find_pad(struct media_entity *e, unsigned int index)
{
    if (e == NULL || index >= e->num_pads)
        return NULL;
    return &e->pads[index];
}

static void free_graph(struct media_device *md)
{
    unsigned int i;
    if (md->entities == NULL)
        return;
    for (i = 0; i < md->num_entities; i++) {
        free(md->entities[i].pads);
        free(md->entities[i].links);
        md->entities[i].pads = NULL;
        md->entities[i].links = NULL;
        md->entities[i].num_pads = 0;
        md->entities[i].num_links = 0;
    }
}

static void resolve_devname(struct media_entity *e)
{
    char link[128];
    char target[256];
    char path[64];
    struct stat st;
    unsigned int t;
    const char *base;
    long n;

    e->devname[0] = '\0';
    t = e->info.type & 0x00ff0000u;
    if (t != ISP_MEDIA_ENT_T_DEVNODE && t != ISP_MEDIA_ENT_T_V4L2_SUBDEV)
        return;
    snprintf(link, sizeof(link), "/sys/dev/char/%u:%u",
             (unsigned)e->info.v4l.major, (unsigned)e->info.v4l.minor);
    n = SYS->readlink(link, target, sizeof(target) - 1);
    if (n <= 0)
        return;
    target[n] = '\0';
    base = strrchr(target, '/');
    base = base ? base + 1 : target;
    if (base[0] == '\0' || strlen(base) >= sizeof(e->devname))
        return;
    snprintf(path, sizeof(path), "/dev/%s", base);
    if (SYS->stat_(path, &st) != 0)
        return;
    if (major(st.st_rdev) != (unsigned)e->info.v4l.major ||
        minor(st.st_rdev) != (unsigned)e->info.v4l.minor)
        return;
    strncpy(e->devname, path, sizeof(e->devname) - 1);
}

static struct media_link *add_link(struct media_entity *e,
                                   struct media_pad *src,
                                   struct media_pad *sink,
                                   unsigned int flags)
{
    unsigned int i;
    for (i = 0; i < e->num_links; i++) {
        if (e->links[i].source == src && e->links[i].sink == sink) {
            e->links[i].flags |= flags;
            return &e->links[i];
        }
    }
    if (e->num_links >= (unsigned int)(e->info.pads + e->info.links) + 4u)
        return NULL;
    e->links[e->num_links].source = src;
    e->links[e->num_links].sink = sink;
    e->links[e->num_links].flags = flags;
    return &e->links[e->num_links++];
}

static int build_graph(struct media_device *md)
{
    unsigned int i, j;
    int ok = 1;

    free_graph(md);
    for (i = 0; i < md->num_entities; i++) {
        struct media_entity *e = &md->entities[i];
        e->num_pads = e->info.pads;
        e->num_links = 0;
        if (e->num_pads) {
            e->pads = calloc(e->num_pads, sizeof(*e->pads));
            if (e->pads == NULL)
                return -1;
        }
        for (j = 0; j < e->num_pads; j++) {
            e->pads[j].index = j;
            e->pads[j].flags = 0;
            e->pads[j].entity = e;
        }
        e->links = calloc(e->info.pads + e->info.links + 4,
                          sizeof(*e->links));
        if (e->links == NULL)
            return -1;
    }
    for (i = 0; i < md->num_entities; i++) {
        struct media_entity *e = &md->entities[i];
        struct media_pad_desc *pads;
        struct media_link_desc *links;
        struct media_links_enum le;

        if (e->info.links == 0)
            continue;
        pads = calloc(e->info.pads ? e->info.pads : 1, sizeof(*pads));
        links = calloc(e->info.links, sizeof(*links));
        if (pads == NULL || links == NULL) {
            free(pads);
            free(links);
            return -1;
        }
        memset(&le, 0, sizeof(le));
        le.entity = e->info.id;
        le.pads = pads;
        le.links = links;
        if (SYS->ioctl(md->fd, MEDIA_IOC_ENUM_LINKS, &le) == 0) {
            for (j = 0; j < e->info.pads && j < e->num_pads; j++)
                e->pads[j].flags = pads[j].flags;
            for (j = 0; j < e->info.links; j++) {
                struct media_entity *s =
                    find_entity_by_id(md, links[j].source.entity);
                struct media_entity *d =
                    find_entity_by_id(md, links[j].sink.entity);
                struct media_pad *sp = find_pad(s, links[j].source.index);
                struct media_pad *dp = find_pad(d, links[j].sink.index);
                if (sp == NULL || dp == NULL)
                    continue;
                ok &= add_link(sp->entity, sp, dp, links[j].flags) != NULL;
                ok &= add_link(dp->entity, sp, dp, links[j].flags) != NULL;
            }
        }
        free(pads);
        free(links);
    }
    return ok ? 0 : -1;
}

struct media_device *media_open(const char *path, int verbose)
{
    struct media_device *md;
    struct media_entity_desc e;
    unsigned int id = 0;

    (void)verbose;
    md = calloc(1, sizeof(*md));
    if (md == NULL)
        return NULL;
    md->fd = SYS->open(path, O_RDWR, 0);
    if (md->fd < 0)
        goto err;
    SYS->ioctl(md->fd, MEDIA_IOC_DEVICE_INFO, &md->info);

    for (;;) {
        struct media_entity *n;
        memset(&e, 0, sizeof(e));
        e.id = id | MEDIA_ENT_ID_FLAG_NEXT;
        if (SYS->ioctl(md->fd, MEDIA_IOC_ENUM_ENTITIES, &e) < 0)
            break;
        e.id &= ~MEDIA_ENT_ID_FLAG_NEXT;
        id = e.id;
        n = realloc(md->entities, (md->num_entities + 1) * sizeof(*n));
        if (n == NULL)
            goto err;
        md->entities = n;
        memset(&md->entities[md->num_entities], 0, sizeof(*n));
        md->entities[md->num_entities].info = e;
        md->entities[md->num_entities].fd = -1;
        resolve_devname(&md->entities[md->num_entities]);
        md->num_entities++;
    }
    if (build_graph(md) != 0)
        goto err;
    return md;
err:
    media_close(md);
    return NULL;
}

void media_close(struct media_device *md)
{
    if (md == NULL)
        return;
    free_graph(md);
    free(md->entities);
    if (md->fd >= 0)
        SYS->close(md->fd);
    free(md);
}

int media_refresh_links(struct media_device *md)
{
    unsigned int i;
    if (md == NULL)
        return -1;
    for (i = 0; i < md->num_entities; i++) {
        struct media_entity_desc e;
        memset(&e, 0, sizeof(e));
        e.id = md->entities[i].info.id;
        if (SYS->ioctl(md->fd, MEDIA_IOC_ENUM_ENTITIES, &e) == 0) {
            unsigned int keep_fd = md->entities[i].fd;
            char keep_dev[sizeof(md->entities[i].devname)];
            memcpy(keep_dev, md->entities[i].devname, sizeof(keep_dev));
            md->entities[i].info = e;
            md->entities[i].fd = keep_fd;
            memcpy(md->entities[i].devname, keep_dev, sizeof(keep_dev));
        }
    }
    return build_graph(md);
}

/* remote pad of a pad: first enabled link on the pad's entity touching it */
static struct media_pad *remote_of(struct media_pad *pad)
{
    struct media_entity *e = pad->entity;
    unsigned int i;
    for (i = 0; i < e->num_links; i++) {
        struct media_link *l = &e->links[i];
        if (!(l->flags & MEDIA_LNK_FL_ENABLED))
            continue;
        if (l->source == pad)
            return l->sink;
        if (l->sink == pad)
            return l->source;
    }
    return NULL;
}

struct media_entity *media_pipeline_head(struct media_device *md,
                                         struct media_entity *entity)
{
    struct media_entity *e = entity;
    int guard = 0;

    (void)md;
    while (e != NULL && guard++ < 64) {
        struct media_pad *p;
        if (e->num_pads == 0)
            return NULL;
        p = &e->pads[0];
        if (p->flags & MEDIA_PAD_FL_SOURCE)
            return (e->info.type == ISP_MEDIA_ENT_T_V4L2_SUBDEV_SENSOR)
                       ? e : NULL;
        p = remote_of(p);
        if (p == NULL)
            return NULL;
        e = p->entity;
    }
    return NULL;
}

int isp_entity_to_isp_id(struct media_device *md, struct media_entity *entity)
{
    struct media_entity *e = entity;
    int guard = 0;

    (void)md;
    while (e != NULL && guard++ < 64) {
        struct media_pad *p;
        if (strncmp(e->info.name, "sunxi_isp.", 10) == 0)
            return atoi(e->info.name + 10);
        if (e->num_pads == 0)
            return -1;
        p = &e->pads[0];
        if (p->flags & MEDIA_PAD_FL_SOURCE)
            return -1;
        p = remote_of(p);
        if (p == NULL)
            return -1;
        e = p->entity;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* ISP device open / close                                             */
/* ------------------------------------------------------------------ */
int isp_dev_open(struct hw_isp_media_dev *md, int id)
{
    struct hw_isp_device *dev;
    struct media_entity *sensor, *subdev, *stat;
    struct fwi_h3a_cfg cfg;
    char name[32];

    if (md == NULL || md->mdev == NULL || id != 0)
        return -1;
    snprintf(name, sizeof(name), "sunxi_isp.%d", id);
    subdev = find_entity_by_name(md->mdev, name);
    snprintf(name, sizeof(name), "sunxi_h3a.%d", id);
    stat = find_entity_by_name(md->mdev, name);
    sensor = subdev ? media_pipeline_head(md->mdev, subdev) : NULL;
    if (subdev == NULL || stat == NULL || sensor == NULL)
        return -1;

    dev = calloc(1, sizeof(*dev));
    if (dev == NULL)
        return -1;
    dev->sensor = *sensor;
    dev->subdev = *subdev;
    dev->stat = *stat;
    dev->sensor.fd = -1;
    dev->subdev.fd = -1;
    dev->stat.fd = -1;
    dev->load_type = 0;

    dev->stat.fd = SYS->open(dev->stat.devname, O_RDWR | O_NONBLOCK, 0);
    if (dev->stat.fd < 0)
        goto err;

    memset(&cfg, 0, sizeof(cfg));
    cfg.buf_size = ISP_STAT_TOTAL_SIZE;
    cfg.config_counter = 0;
    if (SYS->ioctl(dev->stat.fd, VIDIOC_VIN_ISP_H3A_CFG, &cfg) != 0)
        goto err;
    dev->stats_size = cfg.buf_size;
    dev->stats_buf = calloc(1, dev->stats_size ? dev->stats_size : 1);
    if (dev->stats_buf == NULL)
        goto err;

    dev->subdev.fd = SYS->open(dev->subdev.devname, O_RDWR | O_NONBLOCK, 0);
    if (dev->subdev.fd < 0)
        goto err;
    dev->sensor.fd = SYS->open(dev->sensor.devname, O_RDWR | O_NONBLOCK, 0);
    if (dev->sensor.fd < 0)
        goto err;

    md->isp_dev[id] = dev;
    return 0;
err:
    if (dev->sensor.fd >= 0)
        SYS->close(dev->sensor.fd);
    if (dev->subdev.fd >= 0)
        SYS->close(dev->subdev.fd);
    if (dev->stat.fd >= 0)
        SYS->close(dev->stat.fd);
    free(dev->stats_buf);
    free(dev);
    return -1;
}

void isp_dev_close(struct hw_isp_media_dev *md, int id)
{
    struct hw_isp_device *dev;
    if (md == NULL || id != 0 || md->isp_dev[id] == NULL)
        return;
    dev = md->isp_dev[id];
    free(dev->stats_buf);
    if (dev->stat.fd >= 0)
        SYS->close(dev->stat.fd);
    if (dev->subdev.fd >= 0)
        SYS->close(dev->subdev.fd);
    if (dev->sensor.fd >= 0)
        SYS->close(dev->sensor.fd);
    free(dev);
    md->isp_dev[id] = NULL;
}

/* ------------------------------------------------------------------ */
/* media-device handle and video nodes                                 */
/* ------------------------------------------------------------------ */
struct hw_isp_media_dev *isp_md_open(const char *devname)
{
    struct hw_isp_media_dev *md = calloc(1, sizeof(*md));
    if (md == NULL)
        return NULL;
    if (SYS->access(devname, F_OK) != 0)
        SYS->system("mknod /dev/media0 c 253 0");
    md->mdev = media_open(devname, 0);
    if (md->mdev == NULL) {
        free(md);
        return NULL;
    }
    return md;
}

void isp_md_close(struct hw_isp_media_dev *md)
{
    int id;
    if (md == NULL)
        return;
    for (id = 0; id < HW_ISP_DEVICE_NUM; id++)
        isp_dev_close(md, id);
    for (id = 0; id < HW_VIDEO_DEVICE_NUM; id++)
        isp_video_close(md, id);
    media_close(md->mdev);
    free(md);
}

int isp_video_open(struct hw_isp_media_dev *md, unsigned int id)
{
    struct fwi_video_device *video;
    struct media_entity *entity;
    char name[32];

    if (md == NULL || md->mdev == NULL || id >= HW_VIDEO_DEVICE_NUM)
        return -1;
    if (md->video_dev[id] != NULL)
        return 0;
    media_refresh_links(md->mdev);
    snprintf(name, sizeof(name), "vin_video%u", id);
    entity = find_entity_by_name(md->mdev, name);
    if (entity == NULL)
        return -1;
    video = calloc(1, sizeof(*video));
    if (video == NULL)
        return -1;
    video->id = id;
    video->entity = entity;
    video->isp_id = isp_entity_to_isp_id(md->mdev, entity);
    if (video->isp_id < 0 && HW_ISP_DEVICE_NUM == 1)
        video->isp_id = 0;
    if (video->isp_id < 0) {
        free(video);
        return -1;
    }
    if (entity->fd < 0) {
        entity->fd = SYS->open(entity->devname,
                               O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
        if (entity->fd < 0) {
            free(video);
            return -1;
        }
    }
    md->video_dev[id] = video;
    return 0;
}

void isp_video_close(struct hw_isp_media_dev *md, unsigned int id)
{
    struct fwi_video_device *video;
    if (md == NULL || id >= HW_VIDEO_DEVICE_NUM || md->video_dev[id] == NULL)
        return;
    video = md->video_dev[id];
    if (video->pool != NULL)
        buffers_pool_delete(video);
    if (video->entity != NULL && video->entity->fd >= 0) {
        SYS->close(video->entity->fd);
        video->entity->fd = -1;
    }
    free(video);
    md->video_dev[id] = NULL;
}

/* ------------------------------------------------------------------ */
/* video operations                                                    */
/* ------------------------------------------------------------------ */
int video_to_isp_id(struct fwi_video_device *video)
{
    return video ? video->isp_id : -1;
}

static int soc_check(struct fwi_video_device *video)
{
    int fd;
    unsigned char buf[9];
    unsigned long v;
    uint32_t w, h, fps;

    fd = SYS->open("/dev/sunxi_soc_info", O_RDONLY, 0);
    if (fd < 0)
        return 0;
    memset(buf, 0, sizeof(buf));
    if (SYS->ioctl(fd, 5, buf) != 0) {
        SYS->close(fd);
        return 0;
    }
    SYS->close(fd);
    v = strtoul((char *)buf, NULL, 16);
    if (!(v & 0x20u))
        return 0;
    w = video->format.width;
    h = video->format.height;
    {
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = video->type;
        if (SYS->ioctl(video->entity->fd, VIDIOC_G_PARM, &parm) != 0)
            return 0;
        fps = parm.parm.capture.timeperframe.denominator;
    }
    if (w >= 3840 && h >= 2160 && fps > 25)
        return -1;
    return 0;
}

/* video_fmt and fwi_video_device hold the same 32-bit scalars in different orders, so
 * copy them through an explicit offset map; the `format` struct is copied by the callers. */
struct fmt_scalar_map {
    size_t dst;
    size_t src;
};

static void fmt_copy_scalars(void *dst, const void *src,
                             const struct fmt_scalar_map *map, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++)
        memcpy((char *)dst + map[i].dst, (const char *)src + map[i].src,
               sizeof(uint32_t));
}

int video_set_fmt(struct fwi_video_device *video, struct video_fmt *vfmt)
{
    struct v4l2_input input;
    struct v4l2_streamparm parm;
    struct v4l2_format fmt;
    int fd;

    if (video == NULL || vfmt == NULL)
        return -1;
    fd = video->entity->fd;
    memset(&input, 0, sizeof(input));
    input.index = vfmt->index;
    if (SYS->ioctl(fd, VIDIOC_S_INPUT, &input) != 0)
        return -1;

    memset(&parm, 0, sizeof(parm));
    parm.type = vfmt->type;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = vfmt->fps;
    parm.parm.capture.capturemode = vfmt->capturemode;
    parm.parm.capture.reserved[0] = vfmt->use_current_win;
    parm.parm.capture.reserved[1] = vfmt->wdr_mode;
    parm.parm.capture.reserved[2] = 0;
    parm.parm.capture.reserved[3] = 0;
    if (SYS->ioctl(fd, VIDIOC_S_PARM, &parm) != 0)
        return -1;

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = vfmt->type;
    fmt.fmt.pix_mp = vfmt->format;
    if (SYS->ioctl(fd, VIDIOC_S_FMT, &fmt) != 0)
        return -1;

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = vfmt->type;
    if (SYS->ioctl(fd, VIDIOC_G_FMT, &fmt) != 0)
        return -1;
    video->format = fmt.fmt.pix_mp;
    video->nplanes = fmt.fmt.pix_mp.num_planes;

    memset(&parm, 0, sizeof(parm));
    parm.type = vfmt->type;
    if (SYS->ioctl(fd, VIDIOC_G_PARM, &parm) != 0)
        return -1;
    vfmt->capturemode = parm.parm.capture.capturemode;

    {
        static const struct fmt_scalar_map map[] = {
            { offsetof(struct fwi_video_device, type),
              offsetof(struct video_fmt, type) },
            { offsetof(struct fwi_video_device, memtype),
              offsetof(struct video_fmt, memtype) },
            { offsetof(struct fwi_video_device, capturemode),
              offsetof(struct video_fmt, capturemode) },
            { offsetof(struct fwi_video_device, use_current_win),
              offsetof(struct video_fmt, use_current_win) },
            { offsetof(struct fwi_video_device, wdr_mode),
              offsetof(struct video_fmt, wdr_mode) },
            { offsetof(struct fwi_video_device, nbufs),
              offsetof(struct video_fmt, nbufs) },
            { offsetof(struct fwi_video_device, fps),
              offsetof(struct video_fmt, fps) },
            { offsetof(struct fwi_video_device, drop_frame_num),
              offsetof(struct video_fmt, drop_frame_num) },
        };
        fmt_copy_scalars(video, vfmt, map, sizeof(map) / sizeof(map[0]));
    }

    if (soc_check(video) != 0)
        return -1;
    return 0;
}

int video_get_fmt(struct fwi_video_device *video, struct video_fmt *vfmt)
{
    if (video == NULL || vfmt == NULL)
        return -1;
    {
        static const struct fmt_scalar_map map[] = {
            { offsetof(struct video_fmt, type),
              offsetof(struct fwi_video_device, type) },
            { offsetof(struct video_fmt, memtype),
              offsetof(struct fwi_video_device, memtype) },
            { offsetof(struct video_fmt, nbufs),
              offsetof(struct fwi_video_device, nbufs) },
            { offsetof(struct video_fmt, nplanes),
              offsetof(struct fwi_video_device, nplanes) },
            { offsetof(struct video_fmt, capturemode),
              offsetof(struct fwi_video_device, capturemode) },
            { offsetof(struct video_fmt, use_current_win),
              offsetof(struct fwi_video_device, use_current_win) },
            { offsetof(struct video_fmt, wdr_mode),
              offsetof(struct fwi_video_device, wdr_mode) },
            { offsetof(struct video_fmt, fps),
              offsetof(struct fwi_video_device, fps) },
            { offsetof(struct video_fmt, drop_frame_num),
              offsetof(struct fwi_video_device, drop_frame_num) },
        };
        fmt_copy_scalars(vfmt, video, map, sizeof(map) / sizeof(map[0]));
    }
    vfmt->format = video->format;
    return 0;
}

struct buffers_pool *buffers_pool_new(struct fwi_video_device *video)
{
    struct buffers_pool *pool;
    unsigned int i, j;

    if (video == NULL)
        return NULL;
    pool = calloc(1, sizeof(*pool));
    if (pool == NULL)
        return NULL;
    pool->nbufs = video->nbufs;
    if (pool->nbufs) {
        pool->buffers = calloc(pool->nbufs, sizeof(*pool->buffers));
        if (pool->buffers == NULL)
            goto err;
    }
    for (i = 0; i < pool->nbufs; i++) {
        pool->buffers[i].index = i;
        pool->buffers[i].nplanes = video->nplanes;
        if (video->nplanes) {
            pool->buffers[i].planes =
                calloc(video->nplanes, sizeof(*pool->buffers[i].planes));
            if (pool->buffers[i].planes == NULL)
                goto err;
        }
        for (j = 0; j < video->nplanes; j++)
            pool->buffers[i].planes[j].dma_fd = -1;
    }
    video->pool = pool;
    return pool;
err:
    for (i = 0; i < pool->nbufs; i++)
        free(pool->buffers[i].planes);
    free(pool->buffers);
    free(pool);
    return NULL;
}

void buffers_pool_delete(struct fwi_video_device *video)
{
    struct buffers_pool *pool;
    unsigned int i;

    if (video == NULL || video->pool == NULL)
        return;
    pool = video->pool;
    for (i = 0; i < pool->nbufs; i++)
        free(pool->buffers[i].planes);
    free(pool->buffers);
    free(pool);
    video->pool = NULL;
}

int video_req_buffers(struct fwi_video_device *video, struct buffers_pool *pool)
{
    struct v4l2_requestbuffers req;
    unsigned int i, j;

    if (video == NULL || pool == NULL)
        return -1;
    memset(&req, 0, sizeof(req));
    req.count = pool->nbufs;
    req.type = video->type;
    req.memory = video->memtype;
    if (SYS->ioctl(video->entity->fd, VIDIOC_REQBUFS, &req) != 0)
        return -1;
    if (req.count > pool->nbufs)
        return -1;
    pool->nbufs = req.count;

    for (i = 0; i < pool->nbufs; i++) {
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        struct v4l2_buffer buf;
        memset(planes, 0, sizeof(planes));
        memset(&buf, 0, sizeof(buf));
        buf.type = video->type;
        buf.memory = video->memtype;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = video->nplanes;
        if (SYS->ioctl(video->entity->fd, VIDIOC_QUERYBUF, &buf) != 0)
            return -1;
        pool->buffers[i].index = i;
        pool->buffers[i].nplanes = video->nplanes;
        for (j = 0; j < video->nplanes; j++) {
            pool->buffers[i].planes[j].size = planes[j].length;
            pool->buffers[i].planes[j].mem_phy =
                (unsigned int)planes[j].m.mem_offset;
            if (video->memtype == V4L2_MEMORY_MMAP) {
                void *m = SYS->mmap(NULL, planes[j].length,
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    video->entity->fd,
                                    (long)planes[j].m.mem_offset);
                if (m == MAP_FAILED)
                    return -1;
                pool->buffers[i].planes[j].mem = m;
            }
        }
    }
    return 0;
}

int video_free_buffers(struct fwi_video_device *video)
{
    struct buffers_pool *pool;
    struct v4l2_requestbuffers req;
    unsigned int i, j;

    if (video == NULL || video->pool == NULL)
        return -1;
    pool = video->pool;
    for (i = 0; i < pool->nbufs; i++)
        for (j = 0; j < pool->buffers[i].nplanes; j++)
            if (pool->buffers[i].planes[j].mem != NULL)
                SYS->munmap(pool->buffers[i].planes[j].mem,
                            pool->buffers[i].planes[j].size);
    memset(&req, 0, sizeof(req));
    req.type = video->type;
    req.memory = video->memtype;
    req.count = 0;
    if (SYS->ioctl(video->entity->fd, VIDIOC_REQBUFS, &req) != 0)
        return -1;
    video->nbufs = 0;
    video->nplanes = 0;
    return 0;
}

int video_wait_buffer(struct fwi_video_device *video, int timeout_ms)
{
    fd_set r;
    struct timeval tv, *ptv = NULL;
    int ret;

    if (video == NULL)
        return -1;
    FD_ZERO(&r);
    FD_SET(video->entity->fd, &r);
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }
    ret = SYS->select(video->entity->fd + 1, &r, NULL, NULL, ptv);
    return ret > 0 ? 0 : -1;
}

int video_dequeue_buffer(struct fwi_video_device *video,
                         struct video_buffer *buffer)
{
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf;
    unsigned int i;

    if (video == NULL || buffer == NULL)
        return -1;
    memset(planes, 0, sizeof(planes));
    memset(&buf, 0, sizeof(buf));
    buf.type = video->type;
    buf.memory = video->memtype;
    buf.m.planes = planes;
    buf.length = video->nplanes;
    if (SYS->ioctl(video->entity->fd, VIDIOC_DQBUF, &buf) != 0)
        return -1;
    buffer->index = buf.index;
    buffer->bytesused = buf.bytesused;
    buffer->timestamp = buf.timestamp;
    buffer->frame_cnt = buf.reserved;
    buffer->exp_time = buf.reserved2;
    buffer->error = (buf.flags & V4L2_BUF_FLAG_ERROR) != 0;
    buffer->nplanes = video->nplanes;
    if (buffer->planes != NULL) {
        for (i = 0; i < video->nplanes; i++)
            buffer->planes[i].mem_phy =
                (unsigned int)planes[i].m.mem_offset;
    }
    return 0;
}

int video_queue_buffer(struct fwi_video_device *video, unsigned int buf_id)
{
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf;
    struct buffers_pool *pool;
    unsigned int i;

    if (video == NULL || video->pool == NULL)
        return -1;
    pool = video->pool;
    if (buf_id >= pool->nbufs)
        return -1;
    memset(planes, 0, sizeof(planes));
    for (i = 0; i < video->nplanes; i++) {
        if (video->memtype == V4L2_MEMORY_USERPTR)
            planes[i].m.userptr =
                (unsigned long)pool->buffers[buf_id].planes[i].mem;
        else if (video->memtype == V4L2_MEMORY_DMABUF)
            planes[i].m.fd = pool->buffers[buf_id].planes[i].dma_fd;
    }
    memset(&buf, 0, sizeof(buf));
    buf.type = video->type;
    buf.memory = video->memtype;
    buf.index = buf_id;
    buf.m.planes = planes;
    buf.length = video->nplanes;
    if (video->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
        buf.bytesused = pool->buffers[buf_id].bytesused;
    if (SYS->ioctl(video->entity->fd, VIDIOC_QBUF, &buf) != 0)
        return -1;
    return 0;
}

int video_stream_on(struct fwi_video_device *video)
{
    int type;
    if (video == NULL)
        return -1;
    type = video->type;
    if (SYS->ioctl(video->entity->fd, VIDIOC_STREAMON, &type) != 0)
        return -errno;
    return 0;
}

int video_stream_off(struct fwi_video_device *video)
{
    int type;
    if (video == NULL)
        return -1;
    type = video->type;
    if (SYS->ioctl(video->entity->fd, VIDIOC_STREAMOFF, &type) != 0)
        return -errno;
    return 0;
}

int video_set_control(struct fwi_video_device *video, int cid, int value)
{
    struct v4l2_control ctrl;
    if (video == NULL)
        return -1;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = cid;
    ctrl.value = value;
    if (SYS->ioctl(video->entity->fd, VIDIOC_S_CTRL, &ctrl) != 0)
        return -1;
    return 0;
}

int video_get_control(struct fwi_video_device *video, int cid, int *value)
{
    struct v4l2_control ctrl;
    if (video == NULL || value == NULL)
        return -1;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = cid;
    if (SYS->ioctl(video->entity->fd, VIDIOC_G_CTRL, &ctrl) != 0)
        return -1;
    *value = ctrl.value;
    return 0;
}

int video_set_top_clk(struct fwi_video_device *video, unsigned int rate)
{
    struct fwi_top_clk clk;
    if (video == NULL)
        return -1;
    clk.clk_rate = rate;
    if (SYS->ioctl(video->entity->fd, VIDIOC_SET_TOP_CLK, &clk) != 0)
        return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* sensor and load-register ioctls                                     */
/* ------------------------------------------------------------------ */
int isp_sensor_get_configs(struct hw_isp_device *isp, struct sensor_config *cfg)
{
    if (isp == NULL || cfg == NULL)
        return -1;
    return SYS->ioctl(isp->sensor.fd, VIDIOC_VIN_SENSOR_CFG_REQ, cfg);
}

int isp_sensor_set_fps(struct hw_isp_device *isp, struct sensor_fps *fps)
{
    if (isp == NULL || fps == NULL)
        return -1;
    return SYS->ioctl(isp->sensor.fd, VIDIOC_VIN_SENSOR_SET_FPS, fps);
}

int isp_sensor_get_temp(struct hw_isp_device *isp, struct sensor_temp *temp)
{
    if (isp == NULL || temp == NULL)
        return -1;
    return SYS->ioctl(isp->sensor.fd, VIDIOC_VIN_SENSOR_GET_TEMP, temp);
}

int isp_set_load_reg(struct hw_isp_device *isp, struct fwi_table_reg_map *reg)
{
    if (isp == NULL || reg == NULL)
        return -1;
    if (isp->load_type != 0)
        reg->size = ISP_LOAD_DRAM_SIZE;
    return SYS->ioctl(isp->subdev.fd, VIDIOC_VIN_ISP_LOAD_REG, reg);
}
