/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Behavioural test for the ve driver via a mock port: lifecycle, group table, flag
 * and IOMMU ioctls (spec/ve-driver.md §3-§8). */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freecodec/ve_iface.h"
#include "freecodec/ve_port.h"

/* One ops-table layout: 24 slots, encoder enable at slot 16 (spec r2/05 A.3);
 * ve_iface.h asserts it for 32-bit, this re-checks the slot order on the host. */
_Static_assert(sizeof(fc_ve_ops) == 24 * sizeof(void (*)(void)),
               "operations table has 24 slots");
_Static_assert(offsetof(fc_ve_ops, wait_irq) == 5 * sizeof(void (*)(void)),
               "wait_irq is slot 5");
_Static_assert(offsetof(fc_ve_ops, group_base) == 8 * sizeof(void (*)(void)),
               "group_base is slot 8");
_Static_assert(offsetof(fc_ve_ops, enc_enable) == 16 * sizeof(void (*)(void)),
               "enc_enable is slot 16");
_Static_assert(offsetof(fc_ve_ops, iommu_map) == 20 * sizeof(void (*)(void)),
               "iommu_map is slot 20");
_Static_assert(offsetof(fc_ve_ops, chan_info_stop) == 23 * sizeof(void (*)(void)),
               "chan_info_stop is slot 23");

enum ev_type { EV_OPEN, EV_CLOSE, EV_IOCTL, EV_MMAP, EV_MUNMAP };

typedef struct {
    enum ev_type  type;
    int           fd;
    unsigned long req;
    void         *arg;
    size_t        len;
    long          off;
    const char   *path;
} event;

#define LOG_MAX 256

static event g_log[LOG_MAX];
static int   g_nlog;
static int   g_fail;
static int   g_wait_ret;   /* what the mock returns for the wait ioctl 0x103 */

static unsigned char g_regwin[FC_VE_WINDOW_BYTES];

static const unsigned int g_group_off[7] = {
    0x000u, 0x100u, 0x200u, 0x300u, 0x400u, 0x500u, 0xe00u
};

static void log_add(enum ev_type type, int fd, unsigned long req, void *arg,
                    size_t len, long off, const char *path)
{
    if (g_nlog >= LOG_MAX)
        return;
    g_log[g_nlog].type = type;
    g_log[g_nlog].fd   = fd;
    g_log[g_nlog].req  = req;
    g_log[g_nlog].arg  = arg;
    g_log[g_nlog].len  = len;
    g_log[g_nlog].off  = off;
    g_log[g_nlog].path = path;
    g_nlog++;
}

static void log_clear(void)
{
    g_nlog = 0;
}

/* First index of an ioctl with the given request, or -1. */
static int find_ioctl(unsigned long req)
{
    int i;
    for (i = 0; i < g_nlog; i++)
        if (g_log[i].type == EV_IOCTL && g_log[i].req == req)
            return i;
    return -1;
}

/* First index of an event type, or -1. */
static int find_event(enum ev_type type)
{
    int i;
    for (i = 0; i < g_nlog; i++)
        if (g_log[i].type == type)
            return i;
    return -1;
}

static int mock_open(const char *path, int flags)
{
    (void)flags;
    log_add(EV_OPEN, -1, 0, NULL, 0, 0, path);
    if (strcmp(path, FC_VE_NODE) == 0)
        return 42;
    if (strcmp(path, FC_SOC_INFO_NODE) == 0)
        return 43;
    return -1;
}

static int mock_close(int fd)
{
    log_add(EV_CLOSE, fd, 0, NULL, 0, 0, NULL);
    return 0;
}

static int mock_ioctl(int fd, unsigned long request, void *arg)
{
    log_add(EV_IOCTL, fd, request, arg, 0, 0, NULL);
    if (request == FC_VE_IOC_GET_ENV && arg) {
        memset(arg, 0, 144);
    } else if (request == FC_VE_IOC_RESET) {
        /* Module reset (spec 14 §2 step 6): model the kernel de-asserting the
         * engine reset, i.e. clearing bit 24 of the mapped register 0x04. */
        uint32_t v;

        memcpy(&v, g_regwin + 0x04, sizeof(v));
        v &= ~(uint32_t)0x01000000u;
        memcpy(g_regwin + 0x04, &v, sizeof(v));
    } else if (request == FC_VE_IOC_WAIT_ENC) {
        return g_wait_ret;
    }
    return 0;
}

static void *mock_mmap(void *addr, size_t length, int prot, int flags,
                       int fd, long offset)
{
    (void)addr;
    (void)prot;
    (void)flags;
    log_add(EV_MMAP, fd, 0, NULL, length, offset, NULL);
    return g_regwin;
}

static int mock_munmap(void *addr, size_t length)
{
    log_add(EV_MUNMAP, -1, 0, addr, length, 0, NULL);
    return 0;
}

static void check(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        g_fail = 1;
    }
}

int main(void)
{
    fc_ve_ops *ops;
    freecodec_ve_port mock;
    fc_ve_config cfg;
    fc_ve_iommu_req iommu;
    void *h;
    int i_open, i_power, i_eng, i_env, i_mmap, i_reset, i_clk;
    int i_rel, i_munmap, i_shut, i_close;
    int i, rc;

    ops = GetVeOpsS(FC_VE_OPS_DEFAULT);
    check(ops != NULL, "GetVeOpsS(0) != NULL");
    check(GetVeOpsS(1) == NULL, "GetVeOpsS(1) == NULL");
    if (!ops)
        return 1;

    mock = *freecodec_ve_default_port();
    mock.open   = mock_open;
    mock.close  = mock_close;
    mock.ioctl  = mock_ioctl;
    mock.mmap   = mock_mmap;
    mock.munmap = mock_munmap;
    freecodec_ve_set_port(&mock);

    memset(&cfg, 0, sizeof(cfg));
    cfg.use_encoder = 1;
    cfg.clock_mhz   = 480;

    /* Assert the engine-reset bit as it would be after a cold power-on; init's
     * module-reset request must de-assert it (spec 14 §2 step 6, §3, §6). */
    {
        uint32_t v = 0x01000000u;

        memcpy(g_regwin + 0x04, &v, sizeof(v));
    }

    log_clear();
    h = ops->open(&cfg);
    check(h != NULL, "init returns a handle");

    i_open  = find_event(EV_OPEN);
    i_power = find_ioctl(FC_VE_IOC_POWER_SETUP);
    i_eng   = find_ioctl(FC_VE_IOC_REQUEST);
    i_env   = find_ioctl(FC_VE_IOC_GET_ENV);
    i_mmap  = find_event(EV_MMAP);
    check(i_open >= 0, "init opens the device node");
    check(i_power < 0, "encoder init does not issue POWER_SETUP");
    check(i_eng > i_open, "ENGINE_REQ after open");
    check(i_env > i_eng, "GET_ENV_INFO after ENGINE_REQ");
    check(i_mmap > i_env, "mmap after GET_ENV_INFO");
    if (i_mmap >= 0) {
        check(g_log[i_mmap].len == FC_VE_WINDOW_BYTES,
              "mmap size is FC_VE_WINDOW_BYTES");
        check(g_log[i_mmap].off == 0, "mmap offset is 0");
        check(g_log[i_mmap].fd == 42, "mmap uses the device fd");
    }
    if (i_open >= 0)
        check(g_log[i_open].path != NULL &&
              strcmp(g_log[i_open].path, FC_VE_NODE) == 0,
              "init opens /dev/cedar_dev");

    /* Bring-up order, encoder init (spec 14 §2, §6):
     * open < 0x206 < 0x101 < mmap < 0x104 < 0x107 (0x700 is decoder-only). */
    i_reset = find_ioctl(FC_VE_IOC_RESET);
    i_clk   = find_ioctl(FC_VE_IOC_SET_CLK);
    check(i_reset > i_mmap, "module reset (0x104) after mmap");
    check(i_clk > i_reset, "clock set (0x107) after module reset");
    if (i_reset >= 0)
        check(g_log[i_reset].arg == NULL,
              "0x104 carries a NULL arg on encoder init");
    if (i_clk >= 0)
        check(g_log[i_clk].arg == (void *)(uintptr_t)300u,
              "0x107 carries the 300 MHz V831 clock");

    /* Engine-top state after init (spec 14 §3, §6). */
    {
        uint32_t top, r04;

        memcpy(&top, g_regwin, sizeof(top));
        memcpy(&r04, g_regwin + 0x04, sizeof(r04));
        check(((top >> 16) & 0x3u) == 0x3u,
              "init sets top 0x00 bits 16-17 = 3 (DRAM type)");
        check((r04 & 0x01000000u) == 0u,
              "register 0x04 bit 24 clear after init (reset de-asserted)");
    }

    /* register-group offset table (spec §3) */
    for (i = 0; i < 7; i++) {
        void *got = ops->group_base(h, i);
        void *want = g_regwin + g_group_off[i];
        check(got == want, "group-base offset table id 0..6");
    }
    check(ops->group_base(h, 7) == NULL, "unknown group id returns NULL");

    /* Wait return contract (spec 14 §5, §6): 0x103 >= 1 -> 0; 0x103 = 0 -> -1. */
    g_wait_ret = 1;
    log_clear();
    rc = ops->wait_irq(h);
    check(rc == 0, "wait returns 0 when the wait ioctl returns >= 1");
    i = find_ioctl(FC_VE_IOC_WAIT_ENC);
    check(i >= 0 && g_log[i].arg == (void *)(uintptr_t)1,
          "wait passes arg 1 to 0x103");

    g_wait_ret = 0;
    log_clear();
    rc = ops->wait_irq(h);
    check(rc == -1, "wait returns -1 when the wait ioctl times out");

    /* Encoder enable bits in the VETOP control word (the enable step):
     * the enable step sets 0x80|0x40, the disable step clears them. */
    {
        uint32_t v;

        v = 0u;
        memcpy(g_regwin, &v, 4);
        log_clear();
        ops->enc_enable(h);
        i = find_ioctl(FC_VE_IOC_DRAM_HIGH_CHAN);
        check(i >= 0, "enc-enable issues SET_DRAM_HIGH_CHANNAL");
        check(i >= 0 && g_log[i].arg == (void *)(uintptr_t)1,
              "enc-enable arg is 1");
        memcpy(&v, g_regwin, 4);
        check((v & 0xC0u) == 0xC0u, "enc-enable sets VETOP 0x00 bits 7/6");

        v = 0xC5u;
        memcpy(g_regwin, &v, 4);
        log_clear();
        ops->enc_disable(h);
        i = find_ioctl(FC_VE_IOC_DRAM_HIGH_CHAN);
        check(i >= 0, "enc-disable issues SET_DRAM_HIGH_CHANNAL");
        check(i >= 0 && g_log[i].arg == (void *)(uintptr_t)0,
              "enc-disable arg is 0");
        memcpy(&v, g_regwin, 4);
        check((v & 0xC0u) == 0u, "enc-disable clears VETOP 0x00 bits 7/6");
    }

    /* Per-picture reset (slot 4, spec 14 §4, §6): a bit-24 pulse on register
     * 0x04 that issues no ioctl and preserves the other bits. */
    {
        uint32_t v = 0x01000005u;

        memcpy(g_regwin + 0x04, &v, sizeof(v));
        log_clear();
        ops->reset(h);
        memcpy(&v, g_regwin + 0x04, sizeof(v));
        check((v & 0x01000000u) == 0u, "reset clears register 0x04 bit 24");
        check((v & 0x5u) == 0x5u, "reset preserves the other register bits");
        check(find_ioctl(FC_VE_IOC_RESET) < 0, "register reset issues no 0x104");
    }

    /* IOMMU map/unmap pass the caller's struct pointer */
    memset(&iommu, 0, sizeof(iommu));
    iommu.dmabuf_fd = 7;
    log_clear();
    rc = ops->iommu_map(h, &iommu);
    check(rc == 0, "iommu-map returns the ioctl result");
    i = find_ioctl(FC_VE_IOC_IOMMU_MAP);
    check(i >= 0, "iommu-map issues 0x502");
    check(i >= 0 && g_log[i].arg == (void *)&iommu,
          "iommu-map passes the caller pointer");

    log_clear();
    rc = ops->iommu_unmap(h, &iommu);
    check(rc == 0, "iommu-unmap returns the ioctl result");
    i = find_ioctl(FC_VE_IOC_IOMMU_UNMAP);
    check(i >= 0, "iommu-unmap issues 0x503");
    check(i >= 0 && g_log[i].arg == (void *)&iommu,
          "iommu-unmap passes the caller pointer");

    /* teardown order: ENGINE_REL, munmap, POWER_SHUTDOWN, close */
    log_clear();
    ops->close(h);
    i_rel    = find_ioctl(FC_VE_IOC_RELEASE);
    i_munmap = find_event(EV_MUNMAP);
    i_shut   = find_ioctl(FC_VE_IOC_POWER_DOWN);
    i_close  = find_event(EV_CLOSE);
    check(i_rel >= 0, "release issues ENGINE_REL");
    check(i_munmap > i_rel, "munmap after ENGINE_REL");
    check(i_shut > i_munmap, "POWER_SHUTDOWN after munmap");
    check(i_close > i_shut, "close after POWER_SHUTDOWN");
    if (i_munmap >= 0)
        check(g_log[i_munmap].len == FC_VE_WINDOW_BYTES,
              "munmap size is FC_VE_WINDOW_BYTES");

    /* Decoder init still issues POWER_SETUP(1) (gated on nDecoderFlag == 1,
     * spec ve-driver.md); the encoder path above must not. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.use_decoder = 1;
    log_clear();
    h = ops->open(&cfg);
    check(h != NULL, "decoder init returns a handle");
    i_power = find_ioctl(FC_VE_IOC_POWER_SETUP);
    i_eng   = find_ioctl(FC_VE_IOC_REQUEST);
    check(i_power >= 0, "decoder init issues POWER_SETUP");
    check(i_power >= 0 && g_log[i_power].arg == (void *)(uintptr_t)1,
          "decoder POWER_SETUP arg is 1");
    check(i_eng > i_power, "decoder ENGINE_REQ after POWER_SETUP");
    ops->close(h);

    printf("ve: bring-up order, reset/DRAM state, group table, wait contract, "
           "flags, POWER_SETUP gate and IOMMU %s\n",
           g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
