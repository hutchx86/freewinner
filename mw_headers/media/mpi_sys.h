/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* SYS MPI entry points the daemon calls. Declarations only. */
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
