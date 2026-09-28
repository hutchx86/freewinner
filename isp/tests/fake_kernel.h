// SPDX-License-Identifier: AGPL-3.0-only
/* fake_kernel.h - host fake for the isp_uapi_sys seam */
#ifndef FAKE_KERNEL_H
#define FAKE_KERNEL_H

#include "isp_dev_uapi.h"

#define FK_MAX_ENT  16
#define FK_MAX_LNK  32
#define FK_MAX_CALL 16384

struct fk_entity {
    unsigned int id;
    char name[32];
    unsigned int type;
    unsigned int pads;
    unsigned int links;
    unsigned int major, minor;
    char node[16];
    unsigned int pad_flags[4];
    int fd;
};

struct fk_link {
    int src_ent, src_pad, dst_ent, dst_pad;
    unsigned int flags;
};

struct fk_call {
    int fd;
    unsigned long req;
    unsigned char data[256];
    size_t len;
    int ret;
};

typedef struct fake_kernel {
    int fd_media;
    int fd_sensor, fd_isp, fd_h3a, fd_video[HW_VIDEO_DEVICE_NUM];
    struct fk_entity ent[FK_MAX_ENT];
    int nent;
    struct fk_link lnk[FK_MAX_LNK];
    int nlnk;
    int video_links_present;
    struct fk_call call[FK_MAX_CALL];
    int ncall;
    unsigned long fail_req;
    int fail_nth;
    int fail_seen;
    unsigned int h3a_buf_size;
    struct sensor_config sensor_cfg;
    unsigned int load_addr;
    unsigned int load_size;
    int load_calls;
    int select_ready;
    unsigned long soc_bits;
    int soc_present;
    int reqbufs_grant;
    struct v4l2_streamparm g_parm;
    struct v4l2_pix_format_mplane g_fmt;
    struct timeval dq_ts;
    unsigned int dq_reserved, dq_reserved2;
} fake_kernel;

extern fake_kernel fk;
extern const struct fwi_uapi_sys fake_sys;

void fk_reset(void);
void fk_topology_default(void);
void fk_configure_video_links(void);
struct fk_call *fk_last(unsigned long req);
int fk_count(unsigned long req);
void fk_fail_on(unsigned long req, int nth);
void fk_record(struct fk_call *c);

#endif
