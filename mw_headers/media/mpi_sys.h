/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * media/mpi_sys.h - clean-room interface declarations for the eyesee-mpp
 * middleware ABI (SYS MPI entry points).  Declarations only; re-authored from
 * cleanroom/middleware/headers/SPEC.md sections 2.6 and 3.2.  Nothing here is
 * copied from a vendor header.
 */
#ifndef FMW_MEDIA_MPI_SYS_H
#define FMW_MEDIA_MPI_SYS_H

#include "mm_common.h"
#include "mm_comm_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

AW_S32 AW_MPI_SYS_SetConf(const fwm_sys_config_t *pSysConf);
AW_S32 AW_MPI_SYS_Init(void);
AW_S32 AW_MPI_SYS_Exit(void);

#ifdef __cplusplus
}
#endif

#endif /* FMW_MEDIA_MPI_SYS_H */
