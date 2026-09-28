/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Default (libc + GetVeOpsS) implementation of the internal seam. */

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "vb_sys.h"

static int sys_open(const char *path, int flags)
{
    return open(path, flags);
}

static int sys_close(int fd)
{
    return close(fd);
}

/* glibc and musl disagree on the request type of ioctl(); pin it here. */
static int sys_ioctl(int fd, unsigned long request, void *arg)
{
    return ioctl(fd, request, arg);
}

static void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
                      off_t offset)
{
    return mmap(addr, length, prot, flags, fd, offset);
}

static int sys_munmap(void *addr, size_t length)
{
    return munmap(addr, length);
}

/* Provided by the executable that loads the library. */
static fc_ve_ops *sys_get_ve_ops(int type)
{
    return GetVeOpsS(type);
}

static const struct vb_sys g_default = {
    .open       = sys_open,
    .close      = sys_close,
    .ioctl      = sys_ioctl,
    .mmap       = sys_mmap,
    .munmap     = sys_munmap,
    .get_ve_ops = sys_get_ve_ops,
};

static struct vb_sys g_sys = {
    .open       = sys_open,
    .close      = sys_close,
    .ioctl      = sys_ioctl,
    .mmap       = sys_mmap,
    .munmap     = sys_munmap,
    .get_ve_ops = sys_get_ve_ops,
};

const struct vb_sys *vb_sys_get(void)
{
    return &g_sys;
}

void vb_sys_set(const struct vb_sys *sys)
{
    g_sys = sys ? *sys : g_default;
}
