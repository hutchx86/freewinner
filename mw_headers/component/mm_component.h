/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */
/*
 * component/mm_component.h - clean-room interface declarations for the
 * eyesee-mpp middleware ABI (component handles).  Declarations only;
 * re-authored from cleanroom/middleware/headers/SPEC.md section 2.11.  Nothing
 * here is copied from a vendor header.  See NOT-IMPLEMENTED.md.
 *
 * Only the two opaque handle types and the ComponentInit factory shape are
 * carried; the whole component framework (states, ports, messages) is out of
 * scope.
 */
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
