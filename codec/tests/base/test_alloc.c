/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Behavioural test of the venc_base allocator (SPEC §1), adapter functions (§2)
 * and ION helpers (§3), with the kernel and video engine replaced through the
 * vb_sys seam. */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "freecodec/venc_base_abi.h"
#include "freecodec/ve_iface.h"
#include "vb_sys.h"

static int g_fail;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("   FAIL: %s\n", what);
        g_fail = 1;
    }
}

/* ------------------------------------------------------------ fake kernel */

#define MAX_OBJ 64

static struct {
    /* failure injection */
    int fail_open, fail_close, fail_alloc, fail_map, fail_mmap, fail_import,
        fail_free, fail_flush;
    int no_ve_ops, init_null, fail_iommu, iommu_misalign;
    unsigned int phy_offset;

    /* live resources */
    int   fds[MAX_OBJ];     int nfds;
    int   handles[MAX_OBJ]; int nhandles;
    void *maps[MAX_OBJ];    size_t maplen[MAX_OBJ]; int nmaps;
    int   iommu_live;
    int   inst_live;

    /* counters / last arguments */
    int next_fd, next_handle;
    unsigned int next_iommu;
    int opens, inits, releases, iommu_frees;
    const char *open_path; int open_flags;
    struct vb_ion_alloc_data last_alloc;
    struct vb_ion_cache_range last_flush; int flush_fd, flushes;
    int ioctl_fd; unsigned long last_req;
    int mmap_prot, mmap_flags, mmap_fd;
    int iommu_fd, iommu_free_fd;
    fc_ve_config last_cfg;
    char seq[32]; int nseq;   /* cleanup order: I=iommu free U=munmap C=close F=ion free */
} F;

static int g_inst_token;

static void seq_add(char c)
{
    if (F.nseq < (int)sizeof(F.seq) - 1)
        F.seq[F.nseq++] = c;
    F.seq[F.nseq] = 0;
}

static void fake_reset(void)
{
    int i;

    for (i = 0; i < F.nmaps; i++)
        free(F.maps[i]);
    memset(&F, 0, sizeof(F));
    F.next_fd = 100;
    F.next_handle = 1;
    F.next_iommu = 0x40000000u;
}

static void seq_clear(void)
{
    F.nseq = 0;
    F.seq[0] = 0;
}

static void list_add(int *arr, int *n, int v)
{
    if (*n < MAX_OBJ)
        arr[(*n)++] = v;
}

static int list_del(int *arr, int *n, int v)
{
    int i;

    for (i = 0; i < *n; i++)
        if (arr[i] == v) {
            arr[i] = arr[--*n];
            return 0;
        }
    return -1;
}

static int f_open(const char *path, int flags)
{
    int fd;

    F.opens++;
    F.open_path = path;
    F.open_flags = flags;
    if (F.fail_open) {
        errno = ENOENT;
        return -1;
    }
    fd = F.next_fd++;
    list_add(F.fds, &F.nfds, fd);
    return fd;
}

static int f_close(int fd)
{
    seq_add('C');
    if (F.fail_close) {
        errno = EBADF;
        return -1;
    }
    return list_del(F.fds, &F.nfds, fd);
}

static int f_ioctl(int fd, unsigned long req, void *arg)
{
    F.ioctl_fd = fd;
    F.last_req = req;
    switch (req) {
    case VB_ION_IOC_ALLOC: {
        struct vb_ion_alloc_data *d = arg;
        F.last_alloc = *d;
        if (F.fail_alloc) {
            errno = ENOMEM;
            return -1;
        }
        d->handle = F.next_handle++;
        list_add(F.handles, &F.nhandles, d->handle);
        return 0;
    }
    case VB_ION_IOC_MAP: {
        struct vb_ion_fd_data *d = arg;
        int i, ok = 0;
        if (F.fail_map)
            return -1;
        for (i = 0; i < F.nhandles; i++)
            ok |= F.handles[i] == d->handle;
        if (!ok)
            return -1;
        d->fd = F.next_fd++;
        list_add(F.fds, &F.nfds, d->fd);
        return 0;
    }
    case VB_ION_IOC_IMPORT: {
        struct vb_ion_fd_data *d = arg;
        if (F.fail_import)
            return -1;
        d->handle = 1000 + d->fd;
        return 0;
    }
    case VB_ION_IOC_FREE: {
        struct vb_ion_handle_data *d = arg;
        seq_add('F');
        if (F.fail_free)
            return -1;
        list_del(F.handles, &F.nhandles, d->handle);
        return 0;
    }
    case VB_ION_IOC_FLUSH_RANGE:
        F.last_flush = *(struct vb_ion_cache_range *)arg;
        F.flush_fd = fd;
        F.flushes++;
        return F.fail_flush ? -1 : 0;
    }
    return -1;
}

static void *f_mmap(void *addr, size_t len, int prot, int flags, int fd,
                    off_t off)
{
    void *p;

    (void)addr;
    (void)off;
    F.mmap_prot = prot;
    F.mmap_flags = flags;
    F.mmap_fd = fd;
    if (F.fail_mmap || F.nmaps >= MAX_OBJ)
        return MAP_FAILED;
    p = aligned_alloc(4096, (len + 4095) & ~(size_t)4095);
    if (!p)
        return MAP_FAILED;
    F.maps[F.nmaps] = p;
    F.maplen[F.nmaps++] = len;
    return p;
}

static int f_munmap(void *addr, size_t len)
{
    int i;

    seq_add('U');
    for (i = 0; i < F.nmaps; i++)
        if (F.maps[i] == addr && F.maplen[i] == len) {
            free(addr);
            F.maps[i] = F.maps[--F.nmaps];
            F.maplen[i] = F.maplen[F.nmaps];
            return 0;
        }
    return -1;
}

/* fake video engine */

static void *v_init(fc_ve_config *cfg)
{
    F.inits++;
    F.last_cfg = *cfg;
    if (F.init_null)
        return NULL;
    F.inst_live++;
    return &g_inst_token;
}

static void v_release(void *self)
{
    if (self == &g_inst_token) {
        F.releases++;
        F.inst_live--;
    }
}

static unsigned int v_phy_offset(void *self)
{
    (void)self;
    return F.phy_offset;
}

static int v_get_iommu(void *self, fc_ve_iommu_req *p)
{
    (void)self;
    F.iommu_fd = p->dmabuf_fd;
    if (F.fail_iommu)
        return -1;
    p->engine_addr = F.next_iommu + (F.iommu_misalign ? 0x10u : 0u);
    F.next_iommu += 0x100000u;
    F.iommu_live++;
    return 0;
}

static int v_free_iommu(void *self, fc_ve_iommu_req *p)
{
    (void)self;
    seq_add('I');
    F.iommu_free_fd = p->dmabuf_fd;
    F.iommu_frees++;
    F.iommu_live--;
    return 0;
}

static fc_ve_ops g_ve = {
    .open          = v_init,
    .close         = v_release,
    .phys_offset   = v_phy_offset,
    .iommu_map  = v_get_iommu,
    .iommu_unmap = v_free_iommu,
};

static fc_ve_ops *f_get_ve_ops(int type)
{
    if (type != FC_VE_OPS_DEFAULT || F.no_ve_ops)
        return NULL;
    return &g_ve;
}

static const struct vb_sys g_fake = {
    .open = f_open, .close = f_close, .ioctl = f_ioctl,
    .mmap = f_mmap, .munmap = f_munmap, .get_ve_ops = f_get_ve_ops,
};

/* no leaks beyond `fds` open descriptors */
static int clean(int fds)
{
    return F.nfds == fds && F.nhandles == 0 && F.nmaps == 0 &&
           F.iommu_live == 0 && F.inst_live == 0;
}

/* ------------------------------------------------------------------ tests */

static void test_table(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();
    char a[8] = "abcdefg", b[8];

    printf("-- ops table / remaining slots\n");
    check(m != NULL && m == MemAdapterGetOpsS(), "same table every call");
    check(m->open && m->close && m->palloc && m->palloc_no_cache && m->pfree &&
          m->flush_cache && m->ve_get_phyaddr && m->ve_get_viraddr &&
          m->cpu_get_phyaddr && m->cpu_get_viraddr && m->mem_set &&
          m->mem_cpy && m->mem_read && m->mem_write && m->total_size &&
          m->setup && m->shutdown && m->palloc_secure && m->get_ve_addr_offset,
          "every slot present");
    check(m->mem_set(b, 'x', sizeof(b)) == 0 && b[0] == 'x' && b[7] == 'x',
          "mem_set fills");
    check(m->mem_cpy(b, a, 8) == 0 && !memcmp(a, b, 8), "mem_cpy copies");
    memset(b, 0, 8);
    check(m->mem_read(b, a, 8) == 0 && !memcmp(a, b, 8), "mem_read copies");
    memset(b, 0, 8);
    check(m->mem_write(b, a, 8) == 0 && !memcmp(a, b, 8), "mem_write copies");
    check(m->total_size() == -1 && m->setup() == -1 && m->shutdown() == -1,
          "total_size/setup/shutdown return -1");
    check(m->palloc_secure(4096, &g_ve, &g_inst_token) == NULL,
          "palloc_secure returns NULL");
}

static void test_not_open(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();
    char buf[16];

    printf("-- not open\n");
    fake_reset();
    check(m->palloc(4096, &g_ve, &g_inst_token) == NULL, "palloc fails");
    check(m->palloc_no_cache(4096, &g_ve, &g_inst_token) == NULL,
          "palloc_no_cache fails");
    check(F.last_req == 0 && F.nmaps == 0, "no kernel calls");
    check(m->get_ve_addr_offset() == 0, "offset 0");
    m->flush_cache(buf, 16);
    check(F.flushes == 0, "flush no-op");
    m->pfree(buf, &g_ve, &g_inst_token);
    check(F.nseq == 0, "pfree no-op");
    m->close();
    check(F.nseq == 0, "close no-op");
    check(m->cpu_get_phyaddr(buf) == NULL && m->cpu_get_viraddr(buf) == NULL,
          "translations fail");
}

static void test_open_failures(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();

    printf("-- open failures\n");
    fake_reset();
    F.no_ve_ops = 1;
    check(m->open() == -1, "no ve ops: -1");
    check(F.opens == 0 && F.inits == 0 && clean(0), "no ve ops: nothing held");

    fake_reset();
    F.init_null = 1;
    check(m->open() == -1, "init NULL: -1");
    check(F.inits == 1 && F.opens == 0 && clean(0), "init NULL: nothing held");

    fake_reset();
    F.fail_open = 1;
    check(m->open() == -1, "ion open fails: -1");
    check(F.inits == 1 && F.releases == 1 && clean(0),
          "ion open fails: instance released, nothing held");
    check(m->palloc(4096, &g_ve, &g_inst_token) == NULL,
          "still not open after failure");
}

static void test_open_close(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();
    int fd;

    printf("-- open/close refcount\n");
    fake_reset();
    F.phy_offset = 0x1234000u;
    check(m->open() == 0, "open 0");
    check(F.inits == 1 && F.releases == 1 && F.inst_live == 0,
          "instance created and released");
    check(F.last_cfg.use_decoder == 1 && F.last_cfg.use_encoder == 0 &&
          F.last_cfg.work_mode == 0 && F.last_cfg.width_hint == 0 &&
          F.last_cfg.clock_mhz == 0, "init config zeroed, decoder use flag 1");
    check(F.opens == 1 && F.open_path && !strcmp(F.open_path, "/dev/ion") &&
          (F.open_flags & O_ACCMODE) == O_RDONLY, "/dev/ion opened read-only");
    check(F.nfds == 1, "one fd");
    fd = F.fds[0];
    check(m->get_ve_addr_offset() == 0x1234000u, "offset stored");

    check(m->open() == 0, "second open 0");
    check(F.opens == 1 && F.inits == 1, "second open reuses");
    m->close();
    check(F.nfds == 1 && F.fds[0] == fd, "still open after one close");
    check(m->get_ve_addr_offset() == 0x1234000u, "offset still there");
    m->close();
    check(F.nfds == 0, "fd closed on last close");
    check(m->get_ve_addr_offset() == 0, "offset 0 after close");
    seq_clear();
    m->close();
    check(F.nseq == 0, "extra close is a no-op");
    check(clean(0), "nothing held");
}

static void test_palloc(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();
    void *p, *q;
    int ionfd;

    printf("-- palloc / pfree\n");
    fake_reset();
    check(m->open() == 0, "open");
    ionfd = F.fds[0];

    p = m->palloc(10000, &g_ve, &g_inst_token);
    check(p != NULL, "cached palloc");
    check(F.last_alloc.len == 10000 && F.last_alloc.align == VB_ION_ALLOC_ALIGN &&
          F.last_alloc.heap_id_mask ==
              (VB_ION_HEAP_SYSTEM_MASK | VB_ION_HEAP_CARVEOUT_MASK) &&
          F.last_alloc.flags == (VB_ION_FLAG_CACHED | VB_ION_FLAG_CACHED_SYNC),
          "cached alloc request");
    check(F.nfds == 2 && F.nhandles == 1 && F.nmaps == 1 && F.iommu_live == 1,
          "handle, share fd, mapping, iommu held");
    check(F.mmap_prot == (PROT_READ | PROT_WRITE) && F.mmap_flags == MAP_SHARED &&
          F.mmap_fd == F.fds[1], "mmap rw shared on the share fd");
    check(F.iommu_fd == F.fds[1], "iommu param fd is the share fd");
    if (p)
        memset(p, 0xa5, 10000);

    q = m->palloc_no_cache(4096, &g_ve, &g_inst_token);
    check(q != NULL && F.last_alloc.flags == 0, "uncached palloc, flags 0");

    check(m->palloc(0, &g_ve, &g_inst_token) == NULL, "size 0 fails");
    check(m->palloc(-5, &g_ve, &g_inst_token) == NULL, "size <0 fails");
    check(m->palloc(4096, NULL, &g_inst_token) == NULL, "NULL ve ops fails");
    check(m->palloc(4096, &g_ve, NULL) == NULL, "NULL ve instance fails");
    check(F.nhandles == 2 && F.nmaps == 2, "rejected calls held nothing");

    /* pfree lookup: exact start only */
    seq_clear();
    m->pfree((char *)p + 1, &g_ve, &g_inst_token);
    check(F.nseq == 0 && F.nmaps == 2, "interior pointer not freed");
    m->pfree(NULL, &g_ve, &g_inst_token);
    check(F.nseq == 0, "NULL not freed");
    {
        int share = F.fds[1];
        m->pfree(p, &g_ve, &g_inst_token);
        check(!strcmp(F.seq, "IUCF"), "pfree order: iommu, munmap, close, free");
        check(F.iommu_free_fd == share, "iommu unmap uses stored param");
        check(F.nmaps == 1 && F.nhandles == 1 && F.iommu_live == 1 &&
              F.nfds == 2, "first allocation released");
    }
    seq_clear();
    m->pfree(p, &g_ve, &g_inst_token);
    check(F.nseq == 0, "double free is a logged no-op");

    /* pfree without engine: no iommu unmap, rest released */
    seq_clear();
    m->pfree(q, NULL, NULL);
    check(!strcmp(F.seq, "UCF"), "pfree without ops skips iommu unmap");
    check(F.nmaps == 0 && F.nhandles == 0 && F.nfds == 1, "second released");
    F.iommu_live = 0;   /* the fake's mapping was deliberately left */

    m->close();
    check(clean(0) && F.fds[0] == ionfd, "clean after close");
}

static void test_palloc_failures(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();
    static const struct {
        int *flag;
        const char *seq;
        const char *what;
    } steps[] = {
        { &F.fail_alloc,     "",     "ION alloc fails" },
        { &F.fail_map,       "F",    "ION map fails" },
        { &F.fail_mmap,      "CF",   "mmap fails" },
        { &F.fail_iommu,     "UCF",  "IOMMU map fails" },
        { &F.iommu_misalign, "IUCF", "IOMMU address low bits set" },
    };
    size_t i;

    printf("-- palloc failure cleanup\n");
    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        char msg[96];

        fake_reset();
        check(m->open() == 0, "open");
        *steps[i].flag = 1;
        seq_clear();
        snprintf(msg, sizeof(msg), "%s: NULL", steps[i].what);
        check(m->palloc(8192, &g_ve, &g_inst_token) == NULL, msg);
        snprintf(msg, sizeof(msg), "%s: reverse-order cleanup (%s)",
                 steps[i].what, F.seq);
        check(!strcmp(F.seq, steps[i].seq), msg);
        snprintf(msg, sizeof(msg), "%s: no leak", steps[i].what);
        check(clean(1), msg);
        *steps[i].flag = 0;
        check(m->palloc(8192, &g_ve, &g_inst_token) != NULL,
              "allocator still usable after failure");
        m->close();
    }
    /* aligned boundary: low 8 bits clear but bit 8 set is fine */
    fake_reset();
    F.next_iommu = 0x40000100u;
    check(m->open() == 0, "open");
    check(m->palloc(4096, &g_ve, &g_inst_token) != NULL,
          "256-byte aligned IOMMU address accepted");
    m->close();
}

static void test_translation(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();
    char *p, *q;
    uintptr_t pi, qi, off = 0x200000u;

    printf("-- address translation\n");
    fake_reset();
    F.phy_offset = (unsigned int)off;
    check(m->open() == 0, "open");
    p = m->palloc(5000, &g_ve, &g_inst_token);
    q = m->palloc(3000, &g_ve, &g_inst_token);
    check(p && q, "two allocations");
    pi = 0x40000000u;
    qi = 0x40100000u;

    check(m->cpu_get_phyaddr(p) == (void *)pi, "cpu phy of start");
    check(m->cpu_get_phyaddr(p + 4999) == (void *)(pi + 4999), "cpu phy of last byte");
    check(m->cpu_get_phyaddr(p + 5000) == NULL, "end is exclusive");
    check(m->cpu_get_phyaddr(q + 7) == (void *)(qi + 7), "cpu phy second record");
    check(m->cpu_get_phyaddr(NULL) == NULL, "cpu phy NULL");
    check(m->cpu_get_phyaddr(p - 1) == NULL, "below start not found");

    check(m->cpu_get_viraddr((void *)(pi + 123)) == p + 123, "cpu vir inverse");
    check(m->cpu_get_viraddr((void *)(qi + 2999)) == q + 2999, "cpu vir last byte");
    check(m->cpu_get_viraddr((void *)(qi + 3000)) == NULL, "cpu vir end exclusive");
    check(m->cpu_get_viraddr((void *)0x1000) == NULL, "cpu vir unknown");

    check(m->ve_get_phyaddr(p + 10) == (void *)(pi + 10 - off), "ve phy minus offset");
    check(m->ve_get_phyaddr((void *)0x10) == NULL, "ve phy unknown -> NULL");
    check(m->ve_get_viraddr((void *)(pi + 10 - off)) == p + 10, "ve vir inverse");
    check(m->ve_get_viraddr((void *)(qi - off)) == q, "ve vir second record");
    check(m->ve_get_viraddr((void *)(qi + 3000 - off)) == NULL, "ve vir end exclusive");
    check(m->get_ve_addr_offset() == off, "offset slot");

    m->pfree(p, &g_ve, &g_inst_token);
    check(m->cpu_get_phyaddr(p) == NULL, "freed record gone");
    check(m->cpu_get_phyaddr(q) == (void *)qi, "other record intact");

    /* close forgets outstanding records (does not free them) */
    m->close();
    check(F.nmaps == 1 && F.nhandles == 1, "close does not free buffers");
    check(m->open() == 0, "reopen");
    check(m->cpu_get_phyaddr(q) == NULL, "records forgotten after close");
    m->close();
}

static void test_flush(void)
{
    struct vb_mem_ops *m = MemAdapterGetOpsS();
    char *p;

    printf("-- flush_cache\n");
    fake_reset();
    check(m->open() == 0, "open");
    p = m->palloc(4096, &g_ve, &g_inst_token);
    m->flush_cache(p + 16, 100);
    check(F.flushes == 1 && F.flush_fd == F.fds[0] &&
          F.last_req == VB_ION_IOC_FLUSH_RANGE, "request 5 on the ION fd");
    check(F.last_flush.start == (long)(uintptr_t)(p + 16) &&
          F.last_flush.end == (long)(uintptr_t)(p + 116), "range, end exclusive");
    m->flush_cache(NULL, 100);
    m->flush_cache(p, 0);
    m->flush_cache(p, -1);
    check(F.flushes == 1, "NULL / non-positive size ignored");
    F.fail_flush = 1;
    m->flush_cache(p, 10);
    check(F.flushes == 2, "failure logged, no crash");
    m->pfree(p, &g_ve, &g_inst_token);
    m->close();
    check(clean(0), "clean");
}

/* ----------------------------------------------------------------- adapter */

static struct {
    int opens, closes, pallocs, pfrees, flushes, phys, offs;
    int open_rc;
    int size;
    void *mem, *ve_ops, *ve_self;
} A;

static int a_open(void) { A.opens++; return A.open_rc; }
static void a_close(void) { A.closes++; }
static void *a_palloc(int size, void *o, void *s)
{
    A.pallocs++; A.size = size; A.ve_ops = o; A.ve_self = s;
    return (void *)0x1000;
}
static void a_pfree(void *mem, void *o, void *s)
{
    A.pfrees++; A.mem = mem; A.ve_ops = o; A.ve_self = s;
}
static void a_flush(void *mem, int size)
{
    A.flushes++; A.mem = mem; A.size = size;
}
static void *a_phy(void *v) { A.phys++; A.mem = v; return (void *)0x2000; }
static unsigned int a_off(void) { A.offs++; return 0x77u; }

static void test_adapter(void)
{
    struct vb_mem_ops t;
    uint32_t regs[0x100 / 4];
    int o, s;

    printf("-- adapter\n");
    memset(&t, 0, sizeof(t));
    memset(&A, 0, sizeof(A));
    t.open = a_open; t.close = a_close; t.palloc = a_palloc; t.pfree = a_pfree;
    t.flush_cache = a_flush; t.ve_get_phyaddr = a_phy; t.get_ve_addr_offset = a_off;

    check(EncAdapterInitializeMem(&t) == 0 && A.opens == 1, "initialize ok");
    A.open_rc = -1;
    check(EncAdapterInitializeMem(&t) == -1, "initialize open failure -> -1");
    check(EncAdapterInitializeMem(NULL) == -1, "initialize NULL -> -1");
    EncAdpaterRelease(&t);
    check(A.closes == 1, "release calls close");
    EncAdpaterRelease(NULL);

    check(__EncAdapterMemPalloc(&t, 321, &o, &s) == (void *)0x1000 &&
          A.pallocs == 1 && A.size == 321 && A.ve_ops == &o && A.ve_self == &s,
          "palloc forwarded");
    __EncAdapterMemPfree(&t, (void *)0x1000, &o, &s);
    check(A.pfrees == 1 && A.mem == (void *)0x1000 && A.ve_ops == &o &&
          A.ve_self == &s, "pfree forwarded");
    __EncAdapterMemFlushCache(&t, (void *)0x3000, 55);
    check(A.flushes == 1 && A.mem == (void *)0x3000 && A.size == 55,
          "flush forwarded");
    check(__EncAdapterMemGetPhysicAddress(&t, (void *)0x4000) == (void *)0x2000 &&
          A.mem == (void *)0x4000, "ve_get_phyaddr forwarded");
    check(__EncAdapterMemGetVeAddrOffset(&t) == 0x77u && A.offs == 1,
          "get_ve_addr_offset forwarded");

    check(__EncAdapterMemPalloc(NULL, 1, NULL, NULL) == NULL, "palloc NULL table");
    __EncAdapterMemPfree(NULL, (void *)1, NULL, NULL);
    __EncAdapterMemFlushCache(NULL, (void *)1, 1);
    check(__EncAdapterMemGetPhysicAddress(NULL, (void *)1) == NULL, "phy NULL table");
    check(__EncAdapterMemGetVeAddrOffset(NULL) == 0, "offset NULL table");

    /* real table through the adapter */
    fake_reset();
    check(EncAdapterInitializeMem(MemAdapterGetOpsS()) == 0, "real table open");
    EncAdpaterRelease(MemAdapterGetOpsS());
    check(clean(0), "real table closed");
    F.fail_open = 1;
    check(EncAdapterInitializeMem(MemAdapterGetOpsS()) == -1, "real open failure");

    /* IC version */
    memset(regs, 0, sizeof(regs));
    regs[0xf0 / 4] = 0x17080000u;
    regs[0xe4 / 4] = 0xdeadu;
    check(EncAdapterGetICVersion(regs) == 0x1708u, "0xf0 upper 16 bits");
    regs[0xf0 / 4] = 0;
    check(EncAdapterGetICVersion(regs) == 0xdeadu, "fallback to 0xe4");
    regs[0xe4 / 4] = 0;
    check(EncAdapterGetICVersion(regs) == 0, "both zero -> 0");
    check(EncAdapterGetICVersion(NULL) == 0, "NULL -> 0");
}

/* ------------------------------------------------------------- ION helpers */

static void test_ion(void)
{
    int fd, h = 0;

    printf("-- ION helpers\n");
    fake_reset();
    fd = CdcIonOpen();
    check(fd >= 0 && F.open_path && !strcmp(F.open_path, "/dev/ion") &&
          (F.open_flags & O_ACCMODE) == O_RDWR, "open /dev/ion read/write");
    check(CdcIonGetMemType() == VB_MEM_IOMMU, "mem type IOMMU");
    check(CdcIonGetPhyAdr(fd, 5) == 1, "phy addr reports present");

    check(CdcIonGetFd(fd, 7) == -1, "get fd of unknown handle -> -1");
    F.handles[F.nhandles++] = 7;
    {
        int sfd = CdcIonGetFd(fd, 7);
        check(sfd >= 0 && F.ioctl_fd == fd && F.last_req == VB_ION_IOC_MAP,
              "get fd via MAP on caller fd");
        check(CdcIonImport(fd, sfd, &h) == 0 && h == 1000 + sfd &&
              F.last_req == VB_ION_IOC_IMPORT, "import stores handle");
        check(CdcIonImport(fd, sfd, NULL) == 0, "import with NULL out pointer");
        F.fail_import = 1;
        h = -7;
        check(CdcIonImport(fd, sfd, &h) == -1 && h == -7, "import failure");
        F.fail_import = 0;
        check(CdcIonClose(sfd) == 0, "close share fd");
    }
    check(CdcIonFree(fd, 7) == 0 && F.last_req == VB_ION_IOC_FREE &&
          F.nhandles == 0, "free handle");
    F.fail_free = 1;
    check(CdcIonFree(fd, 7) == -1, "free failure -> -1");
    F.fail_free = 0;

    F.fail_close = 1;
    check(CdcIonClose(fd) == -EBADF, "close failure -> -errno");
    F.fail_close = 0;
    check(CdcIonClose(fd) == 0 && F.nfds == 0, "close 0");

    F.fail_open = 1;
    check(CdcIonOpen() < 0, "open failure negative");
}

int main(void)
{
    vb_sys_set(&g_fake);
    fake_reset();

    test_table();
    test_not_open();
    test_open_failures();
    test_open_close();
    test_palloc();
    test_palloc_failures();
    test_translation();
    test_flush();
    test_adapter();
    test_ion();

    fake_reset();
    vb_sys_set(NULL);
    check(vb_sys_get()->open != f_open, "seam default restored");

    printf(g_fail ? "test_alloc: FAILED\n" : "test_alloc: all passed\n");
    return g_fail ? 1 : 0;
}
