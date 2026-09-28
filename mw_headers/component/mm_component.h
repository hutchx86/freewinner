/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/* Middleware component handles: only the opaque handle types and the
 * ComponentInit factory shape; the component framework itself is out of scope. */
#ifndef FMW_COMPONENT_MM_COMPONENT_H
#define FMW_COMPONENT_MM_COMPONENT_H

#include "utils/plat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque component handle passed to the *ComponentInit factories. */
typedef void *COMP_HANDLETYPE;

/* The component object itself is opaque; callers only hold a pointer. */
typedef struct MM_COMPONENTTYPE MM_COMPONENTTYPE;

/* Factory signature the *ComponentInit stubs mirror. */
typedef ERRORTYPE (*ComponentInit)(COMP_HANDLETYPE hComponent);

#ifdef __cplusplus
}
#endif

#endif /* FMW_COMPONENT_MM_COMPONENT_H */
