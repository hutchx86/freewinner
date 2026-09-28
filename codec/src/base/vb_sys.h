/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Internal seam of the encoder support library: every kernel and video-engine
 * call made by the allocator and the ION helpers goes through the table below,
 * so host tests can install fakes. Not exported from the shared library (the
 * version script keeps it local). */

#ifndef FREECODEC_VB_SYS_H
#define FREECODEC_VB_SYS_H

#include <stddef.h>
#include <sys/types.h>

#include "freecodec/ve_iface.h"

#define VB_ION_DEV_NODE "/dev/ion"

struct vb_sys {
    int   (*open)(const char *path, int flags);
    int   (*close)(int fd);
    int   (*ioctl)(int fd, unsigned long request, void *arg);
    void *(*mmap)(void *addr, size_t length, int prot, int flags, int fd,
                  off_t offset);
    int   (*munmap)(void *addr, size_t length);
    fc_ve_ops *(*get_ve_ops)(int type);
};

/* Current seam (never NULL). */
const struct vb_sys *vb_sys_get(void);

/* Install a seam (copied by value); NULL restores the libc/GetVeOpsS default.
 * Not synchronised: install before use (tests only). */
void vb_sys_set(const struct vb_sys *sys);

#endif /* FREECODEC_VB_SYS_H */
