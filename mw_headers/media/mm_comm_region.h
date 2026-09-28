/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* Include-name shim: keeps mediad's #include line; the types are declared in
 * fwm_media_abi.h. */
#ifndef FMW_MEDIA_MM_COMM_REGION_H
#define FMW_MEDIA_MM_COMM_REGION_H

#include "mm_comm_video.h"
#include "fwm_media_abi.h"

/* Plain scalar handle, not an MPP-boundary struct, so kept local. */
typedef unsigned int RGN_HANDLE;

#endif /* FMW_MEDIA_MM_COMM_REGION_H */
