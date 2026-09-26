/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * utils/plat_type.h - clean-room interface declarations for the eyesee-mpp
 * middleware ABI (platform scalar aliases).  Declarations only; re-authored
 * from cleanroom/middleware/headers/SPEC.md section 2.8.  Nothing here is
 * copied from a vendor header.  See NOT-IMPLEMENTED.md.
 */
#ifndef FMW_UTILS_PLAT_TYPE_H
#define FMW_UTILS_PLAT_TYPE_H

/* The vendor platform header pulled the libc basics in transitively and the
 * daemon sources rely on that (e.g. NULL in the component stubs, getenv in
 * isp_config.c).  Provide them explicitly here. */
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The shared alias guard keeps a mixed include set (e.g. this header plus the
 * media-utils ABI header) from re-typedefing the same name. */
#ifndef ERRORTYPE_DEFINED
#define ERRORTYPE_DEFINED
typedef int ERRORTYPE;
#endif

typedef int BOOL;

#ifndef SUCCESS
#define SUCCESS 0
#endif
#ifndef FAILURE
#define FAILURE (-1)
#endif

#ifdef __cplusplus
}
#endif

#endif /* FMW_UTILS_PLAT_TYPE_H */
