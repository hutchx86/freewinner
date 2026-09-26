/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Injectable syscall port for the ve driver: every device op goes through a
 * freecodec_ve_port, so a test can install a mock without /dev/cedar_dev. */

#ifndef FREECODEC_VE_PORT_H
#define FREECODEC_VE_PORT_H

#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct freecodec_ve_port {
    int   (*open)(const char *path, int flags);
    int   (*close)(int fd);
    int   (*ioctl)(int fd, unsigned long request, void *arg);
    void *(*mmap)(void *addr, size_t length, int prot, int flags,
                  int fd, long offset);
    int   (*munmap)(void *addr, size_t length);

    int (*mutex_init)(pthread_mutex_t *m);
    int (*mutex_lock)(pthread_mutex_t *m);
    int (*mutex_unlock)(pthread_mutex_t *m);
    int (*mutex_destroy)(pthread_mutex_t *m);
} freecodec_ve_port;

/* The libc/pthread-backed port. Never NULL. */
const freecodec_ve_port *freecodec_ve_default_port(void);

/* Install a process-global port (copied by value); NULL restores the default. */
void freecodec_ve_set_port(const freecodec_ve_port *port);

#ifdef __cplusplus
}
#endif

#endif /* FREECODEC_VE_PORT_H */
