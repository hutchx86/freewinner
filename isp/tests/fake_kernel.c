// SPDX-License-Identifier: AGPL-3.0-only
/* fake_kernel.c - host fake for the isp_uapi_sys seam */
#include "fake_kernel.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

fake_kernel fk;

static size_t req_size(unsigned long req)
{
    switch (req) {
    case MEDIA_IOC_DEVICE_INFO:    return sizeof(struct media_device_info);
    case MEDIA_IOC_ENUM_ENTITIES:  return sizeof(struct media_entity_desc);
    case MEDIA_IOC_ENUM_LINKS:     return sizeof(struct media_links_enum);
    case VIDIOC_VIN_ISP_H3A_CFG:   return sizeof(struct fwi_h3a_cfg);
    case VIDIOC_VIN_ISP_STAT_REQ:  return sizeof(struct fwi_stat_req);
    case VIDIOC_VIN_SENSOR_CFG_REQ:return sizeof(struct sensor_config);
    case VIDIOC_VIN_SENSOR_EXP_GAIN:return sizeof(struct sensor_exp_gain);
    case VIDIOC_VIN_SENSOR_SET_FPS:return sizeof(struct sensor_fps);
    case VIDIOC_VIN_SENSOR_GET_TEMP:return sizeof(struct sensor_temp);
    case VIDIOC_VIN_ACT_SET_CODE:  return sizeof(struct fwi_act_code);
    case VIDIOC_VIN_ACT_INIT:      return sizeof(struct fwi_act_init);
    case VIDIOC_VIN_ISP_LOAD_REG:  return sizeof(struct fwi_table_reg_map);
    case VIDIOC_S_INPUT:           return sizeof(struct v4l2_input);
    case VIDIOC_S_PARM:
    case VIDIOC_G_PARM:            return sizeof(struct v4l2_streamparm);
    case VIDIOC_S_FMT:
    case VIDIOC_G_FMT:             return sizeof(struct v4l2_format);
    case VIDIOC_REQBUFS:           return sizeof(struct v4l2_requestbuffers);
    case VIDIOC_QUERYBUF:
    case VIDIOC_QBUF:
    case VIDIOC_DQBUF:             return sizeof(struct v4l2_buffer);
    case VIDIOC_S_CTRL:
    case VIDIOC_G_CTRL:            return sizeof(struct v4l2_control);
    case VIDIOC_STREAMON:
    case VIDIOC_STREAMOFF:         return sizeof(int);
    case VIDIOC_SET_TOP_CLK:       return sizeof(struct fwi_top_clk);
    default:                       return 0;
    }
}

static struct fk_call *push_call(int fd, unsigned long req, void *arg, int ret)
{
    struct fk_call *c;
    size_t n;
    if (fk.ncall >= FK_MAX_CALL)
        return NULL;
    c = &fk.call[fk.ncall++];
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->req = req;
    c->ret = ret;
    n = req_size(req);
    if (arg != NULL && n > 0) {
        if (n > sizeof(c->data))
            n = sizeof(c->data);
        memcpy(c->data, arg, n);
        c->len = n;
    }
    return c;
}

void fk_record(struct fk_call *c) { (void)c; }

struct fk_call *fk_last(unsigned long req)
{
    int i;
    for (i = fk.ncall - 1; i >= 0; i--)
        if (fk.call[i].req == req)
            return &fk.call[i];
    return NULL;
}

int fk_count(unsigned long req)
{
    int i, n = 0;
    for (i = 0; i < fk.ncall; i++)
        if (fk.call[i].req == req)
            n++;
    return n;
}

void fk_fail_on(unsigned long req, int nth)
{
    fk.fail_req = req;
    fk.fail_nth = nth;
    fk.fail_seen = 0;
}

static struct fk_entity *ent_by_name(const char *name)
{
    int i;
    for (i = 0; i < fk.nent; i++)
        if (strcmp(fk.ent[i].name, name) == 0)
            return &fk.ent[i];
    return NULL;
}

static struct fk_entity *ent_by_node(const char *node)
{
    int i;
    for (i = 0; i < fk.nent; i++)
        if (strcmp(fk.ent[i].node, node) == 0)
            return &fk.ent[i];
    return NULL;
}

static struct fk_entity *ent_by_fd(int fd)
{
    int i;
    for (i = 0; i < fk.nent; i++)
        if (fk.ent[i].fd == fd)
            return &fk.ent[i];
    return NULL;
}

static struct fk_entity *ent_by_id(unsigned int id)
{
    int i;
    for (i = 0; i < fk.nent; i++)
        if (fk.ent[i].id == id)
            return &fk.ent[i];
    return NULL;
}

void fk_reset(void)
{
    memset(&fk, 0, sizeof(fk));
    fk.fd_media = 1;
    fk.fd_sensor = 10;
    fk.fd_isp = 11;
    fk.fd_h3a = 12;
    {
        int i;
        for (i = 0; i < HW_VIDEO_DEVICE_NUM; i++)
            fk.fd_video[i] = 20 + i;
    }
    fk.select_ready = 0;
    fk.h3a_buf_size = ISP_STAT_TOTAL_SIZE;
    fk.reqbufs_grant = -1;
    fk.soc_bits = 0;
}

void fk_topology_default(void)
{
    int i;

    fk.nent = 0;
    fk.nlnk = 0;

#define ADD_ENT(nm, ty, np, nl, maj, mino)                                 \
    do {                                                                   \
        struct fk_entity *e = &fk.ent[fk.nent];                            \
        memset(e, 0, sizeof(*e));                                          \
        e->id = (unsigned int)(fk.nent + 1);                               \
        strncpy(e->name, (nm), sizeof(e->name) - 1);                       \
        e->type = (ty);                                                    \
        e->pads = (np);                                                    \
        e->links = (nl);                                                   \
        e->major = (maj);                                                  \
        e->minor = (mino);                                                 \
        e->fd = -1;                                                        \
        fk.nent++;                                                         \
    } while (0)

    ADD_ENT("gc3003_mipi", ISP_MEDIA_ENT_T_V4L2_SUBDEV_SENSOR, 1, 1, 81, 0);
    snprintf(fk.ent[0].node, sizeof(fk.ent[0].node), "v4l-subdev0");
    fk.ent[0].pad_flags[0] = MEDIA_PAD_FL_SOURCE;
    ADD_ENT("sun6i_csi", ISP_MEDIA_ENT_T_V4L2_SUBDEV, 2, 2, 81, 1);
    snprintf(fk.ent[1].node, sizeof(fk.ent[1].node), "v4l-subdev1");
    fk.ent[1].pad_flags[0] = MEDIA_PAD_FL_SINK;
    fk.ent[1].pad_flags[1] = MEDIA_PAD_FL_SOURCE;
    ADD_ENT("sunxi_isp.0", ISP_MEDIA_ENT_T_V4L2_SUBDEV, 2, 0, 81, 2);
    snprintf(fk.ent[2].node, sizeof(fk.ent[2].node), "v4l-subdev2");
    fk.ent[2].pad_flags[0] = MEDIA_PAD_FL_SINK;
    fk.ent[2].pad_flags[1] = MEDIA_PAD_FL_SOURCE;
    ADD_ENT("sunxi_h3a.0", ISP_MEDIA_ENT_T_V4L2_SUBDEV, 0, 0, 81, 3);
    snprintf(fk.ent[3].node, sizeof(fk.ent[3].node), "v4l-subdev3");
    for (i = 0; i < HW_VIDEO_DEVICE_NUM; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "vin_video%d", i);
        ADD_ENT(nm, ISP_MEDIA_ENT_T_DEVNODE, 1, 0, 81, (unsigned)(10 + i));
        snprintf(fk.ent[fk.nent - 1].node,
                 sizeof(fk.ent[fk.nent - 1].node), "video%d", i);
        fk.ent[fk.nent - 1].pad_flags[0] = MEDIA_PAD_FL_SINK;
    }
#undef ADD_ENT

    /* fixed links: sensor -> csi -> isp */
    fk.lnk[0].src_ent = 1; fk.lnk[0].src_pad = 0;
    fk.lnk[0].dst_ent = 2; fk.lnk[0].dst_pad = 0;
    fk.lnk[0].flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
    fk.lnk[1].src_ent = 2; fk.lnk[1].src_pad = 1;
    fk.lnk[1].dst_ent = 3; fk.lnk[1].dst_pad = 0;
    fk.lnk[1].flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
    fk.nlnk = 2;
    fk.video_links_present = 0;
    fk.ent[2].links = 2; /* sensor->csi, csi->isp, plus none to video yet */
    ent_by_name("gc3003_mipi")->fd = fk.fd_sensor;
    ent_by_name("sunxi_isp.0")->fd = fk.fd_isp;
    ent_by_name("sunxi_h3a.0")->fd = fk.fd_h3a;
    for (i = 0; i < HW_VIDEO_DEVICE_NUM; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "vin_video%d", i);
        ent_by_name(nm)->fd = fk.fd_video[i];
    }
}

void fk_configure_video_links(void)
{
    int i;
    for (i = 0; i < HW_VIDEO_DEVICE_NUM; i++) {
        struct fk_link *l = &fk.lnk[fk.nlnk++];
        l->src_ent = 3;
        l->src_pad = 1;
        l->dst_ent = 5 + i;
        l->dst_pad = 0;
        l->flags = MEDIA_LNK_FL_ENABLED;
    }
    fk.video_links_present = 1;
    fk.ent[2].links = 2 + HW_VIDEO_DEVICE_NUM;
}

/* ------------------------------------------------------------------ */
static int fk_enum_entities(struct media_entity_desc *e)
{
    int i;
    unsigned int want = e->id;
    unsigned int next = (want & MEDIA_ENT_ID_FLAG_NEXT) != 0;
    unsigned int base = want & ~MEDIA_ENT_ID_FLAG_NEXT;
    struct fk_entity *hit = NULL;

    if (next) {
        unsigned int best = 0xffffffffu;
        for (i = 0; i < fk.nent; i++)
            if (fk.ent[i].id > base && fk.ent[i].id < best) {
                best = fk.ent[i].id;
                hit = &fk.ent[i];
            }
    } else {
        hit = ent_by_id(base);
    }
    if (hit == NULL)
        return -1;
    memset(e, 0, sizeof(*e));
    e->id = hit->id;
    memcpy(e->name, hit->name, sizeof(hit->name));
    e->type = hit->type;
    e->pads = (uint16_t)hit->pads;
    e->links = (uint16_t)hit->links;
    e->v4l.major = hit->major;
    e->v4l.minor = hit->minor;
    return 0;
}

static int fk_enum_links(struct media_links_enum *le)
{
    struct fk_entity *e = ent_by_id(le->entity);
    int i, j = 0, p;

    if (e == NULL)
        return -1;
    for (p = 0; p < (int)e->pads && le->pads; p++) {
        le->pads[p].entity = e->id;
        le->pads[p].index = (uint16_t)p;
        le->pads[p].flags = e->pad_flags[p];
    }
    for (i = 0; i < fk.nlnk && le->links; i++) {
        struct fk_link *l = &fk.lnk[i];
        if (l->src_ent != (int)e->id && l->dst_ent != (int)e->id)
            continue;
        if (fk.video_links_present == 0 && (l->dst_ent == 3 || l->src_ent == 3) &&
            (l->dst_ent >= 4 || l->src_ent >= 4))
            continue;
        if (j >= (int)e->links)
            break;
        le->links[j].source.entity = (uint32_t)l->src_ent;
        le->links[j].source.index = (uint16_t)l->src_pad;
        le->links[j].sink.entity = (uint32_t)l->dst_ent;
        le->links[j].sink.index = (uint16_t)l->dst_pad;
        le->links[j].flags = l->flags;
        j++;
    }
    return 0;
}

static int fk_ioctl(int fd, unsigned long req, void *arg)
{
    int ret = 0;

    if (req == fk.fail_req && fk.fail_nth > 0) {
        fk.fail_seen++;
        if (fk.fail_seen == fk.fail_nth) {
            push_call(fd, req, arg, -1);
            return -1;
        }
    }

    if (fd == 30) { /* /dev/sunxi_soc_info */
        if (req == 5 && arg != NULL)
            snprintf((char *)arg, 9, "%lx", fk.soc_bits);
        push_call(fd, req, arg, 0);
        return 0;
    }
    if (fd == fk.fd_media) {
        switch (req) {
        case MEDIA_IOC_DEVICE_INFO:
            memset(arg, 0, sizeof(struct media_device_info));
            strcpy(((struct media_device_info *)arg)->driver, "sunxi-media");
            break;
        case MEDIA_IOC_ENUM_ENTITIES:
            ret = fk_enum_entities(arg);
            break;
        case MEDIA_IOC_ENUM_LINKS:
            ret = fk_enum_links(arg);
            break;
        default:
            ret = -1;
            break;
        }
        push_call(fd, req, arg, ret);
        return ret;
    }
    if (fd == fk.fd_h3a) {
        if (req == VIDIOC_VIN_ISP_H3A_CFG && arg != NULL) {
            struct fwi_h3a_cfg *cfg = arg;
            cfg->buf_size = fk.h3a_buf_size;
        }
        push_call(fd, req, arg, 0);
        return 0;
    }
    if (fd == fk.fd_sensor) {
        switch (req) {
        case VIDIOC_VIN_SENSOR_CFG_REQ:
            *(struct sensor_config *)arg = fk.sensor_cfg;
            break;
        case VIDIOC_VIN_SENSOR_EXP_GAIN:
        case VIDIOC_VIN_SENSOR_SET_FPS:
        case VIDIOC_VIN_SENSOR_GET_TEMP:
        case VIDIOC_VIN_ACT_INIT:
        case VIDIOC_VIN_ACT_SET_CODE:
            break;
        default:
            ret = -1;
            break;
        }
        push_call(fd, req, arg, ret);
        return ret;
    }
    if (fd == fk.fd_isp) {
        if (req == VIDIOC_VIN_ISP_LOAD_REG && arg != NULL) {
            struct fwi_table_reg_map *r = arg;
            fk.load_addr = (unsigned)(unsigned long)r->addr;
            fk.load_size = r->size;
            fk.load_calls++;
        }
        push_call(fd, req, arg, 0);
        return 0;
    }
    if (ent_by_fd(fd) != NULL) { /* a video node */
        switch (req) {
        case VIDIOC_S_INPUT:
            break;
        case VIDIOC_S_PARM:
            fk.g_parm = *(struct v4l2_streamparm *)arg;
            break;
        case VIDIOC_G_PARM:
            *(struct v4l2_streamparm *)arg = fk.g_parm;
            break;
        case VIDIOC_S_FMT:
            fk.g_fmt = ((struct v4l2_format *)arg)->fmt.pix_mp;
            break;
        case VIDIOC_G_FMT:
            ((struct v4l2_format *)arg)->fmt.pix_mp = fk.g_fmt;
            break;
        case VIDIOC_REQBUFS: {
            struct v4l2_requestbuffers *r = arg;
            if (fk.reqbufs_grant >= 0)
                r->count = (unsigned)fk.reqbufs_grant;
            break;
        }
        case VIDIOC_QUERYBUF: {
            struct v4l2_buffer *b = arg;
            unsigned int j;
            for (j = 0; j < b->length; j++) {
                b->m.planes[j].length = 0x100000;
                b->m.planes[j].m.mem_offset = 0x1000 * (j + 1);
            }
            break;
        }
        case VIDIOC_QBUF:
            break;
        case VIDIOC_DQBUF: {
            struct v4l2_buffer *b = arg;
            unsigned int j;
            b->bytesused = 0x1234;
            b->timestamp = fk.dq_ts;
            b->reserved = fk.dq_reserved;
            b->reserved2 = fk.dq_reserved2;
            b->flags = 0;
            for (j = 0; j < b->length && b->m.planes; j++)
                b->m.planes[j].m.mem_offset = 0x1000 * (j + 1);
            break;
        }
        case VIDIOC_STREAMON:
        case VIDIOC_STREAMOFF:
            break;
        case VIDIOC_S_CTRL:
        case VIDIOC_G_CTRL:
            break;
        case VIDIOC_SET_TOP_CLK:
            break;
        default:
            ret = -1;
            break;
        }
        push_call(fd, req, arg, ret);
        return ret;
    }
    push_call(fd, req, arg, -1);
    return -1;
}

static int fk_open(const char *path, int flags, unsigned int mode)
{
    (void)flags;
    (void)mode;
    if (strcmp(path, "/dev/media0") == 0)
        return fk.fd_media;
    if (strcmp(path, "/dev/sunxi_soc_info") == 0)
        return 30;
    {
        const char *base = strrchr(path, '/');
        struct fk_entity *e = ent_by_node(base ? base + 1 : path);
        if (e != NULL)
            return e->fd;
    }
    return -1;
}

static int fk_close(int fd) { (void)fd; return 0; }

static void *fk_mmap(void *a, size_t l, int p, int f, int fd, long o)
{
    (void)a; (void)l; (void)p; (void)f; (void)fd; (void)o;
    return (void *)0x40000000;
}

static int fk_munmap(void *a, size_t l) { (void)a; (void)l; return 0; }

static int fk_select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
    (void)n;
    (void)w;
    (void)e;
    (void)t;
    if (fk.select_ready > 0) {
        if (r != NULL)
            FD_ZERO(r);
        return 1;
    }
    return 0;
}

static int fk_access(const char *p, int m) { (void)p; (void)m; return 0; }
static int fk_system(const char *c) { (void)c; return 0; }

static long fk_readlink(const char *path, char *buf, size_t n)
{
    unsigned int maj, min;
    int i;
    char tmp[128];
    if (sscanf(path, "/sys/dev/char/%u:%u", &maj, &min) != 2)
        return -1;
    for (i = 0; i < fk.nent; i++) {
        if (fk.ent[i].major == maj && fk.ent[i].minor == min) {
            int len = snprintf(tmp, sizeof(tmp), "/devices/soc/%s",
                               fk.ent[i].node);
            if ((size_t)len >= n)
                len = (int)n - 1;
            memcpy(buf, tmp, (size_t)len);
            return len;
        }
    }
    return -1;
}

static int fk_stat(const char *path, struct stat *st)
{
    const char *base = strrchr(path, '/');
    struct fk_entity *e = ent_by_node(base ? base + 1 : path);
    if (e == NULL)
        return -1;
    memset(st, 0, sizeof(*st));
    st->st_rdev = makedev(e->major, e->minor);
    return 0;
}

const struct fwi_uapi_sys fake_sys = {
    fk_open, fk_close, fk_ioctl, fk_mmap, fk_munmap,
    fk_select, fk_access, fk_system, fk_readlink, fk_stat,
};
