/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* ION helpers on a caller-owned /dev/ion descriptor (SPEC §3). IOMMU mode only.
 * Kernel calls go through the vb_sys seam. */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freecodec/venc_base_abi.h"
#include "vb_sys.h"

#define LOG(...) fprintf(stderr, "venc_base: " __VA_ARGS__)

int CdcIonOpen(void)
{
    int fd = vb_sys_get()->open(VB_ION_DEV_NODE, O_RDWR);

    if (fd < 0)
        LOG("cannot open " VB_ION_DEV_NODE "\n");
    return fd;
}

int CdcIonClose(int fd)
{
    if (vb_sys_get()->close(fd) < 0) {
        int e = errno;
        LOG("ION close(%d) failed\n", fd);
        return e > 0 ? -e : -1;
    }
    return 0;
}

int CdcIonGetMemType(void)
{
    return VB_MEM_IOMMU;
}

unsigned long CdcIonGetPhyAdr(int fd, uintptr_t handle)
{
    (void)fd;
    (void)handle;
    return 1;   /* IOMMU: no physical address; non-zero means "present" */
}

int CdcIonGetFd(int fd, uintptr_t handle)
{
    struct vb_ion_fd_data d;

    memset(&d, 0, sizeof(d));
    d.handle = (vb_ion_handle)handle;
    d.fd = -1;
    if (vb_sys_get()->ioctl(fd, VB_ION_IOC_MAP, &d) < 0 || d.fd < 0) {
        LOG("ION map (share) failed\n");
        return -1;
    }
    return d.fd;
}

int CdcIonImport(int fd, int share_fd, vb_ion_handle *handle)
{
    struct vb_ion_fd_data d;

    memset(&d, 0, sizeof(d));
    d.fd = share_fd;
    if (vb_sys_get()->ioctl(fd, VB_ION_IOC_IMPORT, &d) < 0) {
        LOG("ION import of fd %d failed\n", share_fd);
        return -1;
    }
    if (handle)
        *handle = d.handle;
    return 0;
}

int CdcIonFree(int fd, vb_ion_handle handle)
{
    struct vb_ion_handle_data d;

    d.handle = handle;
    if (vb_sys_get()->ioctl(fd, VB_ION_IOC_FREE, &d) < 0) {
        LOG("ION free failed\n");
        return -1;
    }
    return 0;
}
