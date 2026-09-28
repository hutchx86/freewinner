/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* Platform scalar aliases used by the middleware prototypes. Declarations only. */
#ifndef FMW_UTILS_PLAT_TYPE_H
#define FMW_UTILS_PLAT_TYPE_H

/* Daemon sources rely on these arriving transitively (NULL in the component
 * stubs, getenv in isp_config.c). */
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
