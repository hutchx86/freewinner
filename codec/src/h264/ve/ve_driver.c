/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Video-engine driver (`ve`): owns /dev/cedar_dev, implements the ops table in
 * ve_iface.h per spec/ve-driver.md. All access goes through freecodec_ve_port. */

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "freecodec/ve_iface.h"
#include "freecodec/ve_port.h"

/* Soc-info requests and register offsets of the /dev/cedar_dev driver. */
#define VE_SOC_IOCTL_GET_CHIP_ID 3u
#define VE_SOC_IOCTL_SET_SPEED   5u

/* Offsets within the mapped VETOP window (spec §10, §11). */
#define VE_REG_TOP_VERSION_LO 0x00u
#define VE_REG_TOP_VERSION_HI 0x04u

/* VETOP 0x00 bits 7/6 gate the encoder engine (enable sets, disable clears, when
 * nEncoderFlag); without them the encoder sub-block never latches its script. */
#define VE_REG_TOP_CTRL       0x00u
#define VE_TOP_ENC_ENABLE     0xC0u

/* DRAM-type field is at VETOP 0x00 bits [17:16], not 0x08; V833 uses 0x30000.
 * Without it the encoder sub-block does not latch its script (cold boot). */
#define VE_TOP_DRAM_TYPE      0x30000u
#define VE_REG_TOP_DDR_MODE   0x0cu
/* VE reset pulse on this SoC: set then clear bit 24 of VETOP+0x04. */
#define VE_REG_TOP_RESET_PULSE 0x04u
#define VE_TOP_RESET_BIT       0x01000000u

/* Encoder-performance frequency/voltage; exact numbers are open (spec §11), and
 * the path is not taken on V833 (IC version >= 0x1708). */
#define VE_PERF_VOLTAGE      0u
#define VE_PERF_FREQ_DEFAULT 480u

/* Per-IC VE clock for the V833 entry (spec §6.3), passed to SET_VE_FREQ in MHz
 * like the ioctl's other callers. */
#define VE_PERF_FREQ_V833    300u

/* 144-byte GET_ENV_INFO record (spec §5); only phys_addr_bias/dram_type are used. */
struct ve_env_info {
    uint32_t phys_addr_bias;
    uint32_t dram_type;
    uint32_t ve_freq;
    uint32_t reserved;
    uint8_t  pad[144 - 16];
};

/* Parameter block for the channel-info set request; debug-only (spec §8). */
struct ve_proc_info_param {
    char         *buf;
    unsigned int  len;
    unsigned char channel;
};

typedef struct ve_env {
    int             fd;
    int             ref_count;
    int             mutex_ready;
    pthread_mutex_t mutex;
    void           *reg_base;
    int             dram_type;
    unsigned int    phys_addr_bias;
    unsigned int    ve_freq;
    int             enable_afbc;
    int             adjust_dram_speed;
    int             chip_id;
    int             use_jpeg;
    int             is_encoder;
} ve_env;

static ve_env g_env;

/* ------------------------------------------------------------------ port -- */

static int ve_port_open(const char *path, int flags)
{
    return open(path, flags);
}

static int ve_port_close(int fd)
{
    return close(fd);
}

static int ve_port_ioctl(int fd, unsigned long request, void *arg)
{
    return ioctl(fd, request, arg);
}

static void *ve_port_mmap(void *addr, size_t length, int prot, int flags,
                          int fd, long offset)
{
    return mmap(addr, length, prot, flags, fd, (off_t)offset);
}

static int ve_port_munmap(void *addr, size_t length)
{
    return munmap(addr, length);
}

static int ve_port_mutex_init(pthread_mutex_t *m)
{
    return pthread_mutex_init(m, NULL);
}

static int ve_port_mutex_lock(pthread_mutex_t *m)
{
    return pthread_mutex_lock(m);
}

static int ve_port_mutex_unlock(pthread_mutex_t *m)
{
    return pthread_mutex_unlock(m);
}

static int ve_port_mutex_destroy(pthread_mutex_t *m)
{
    return pthread_mutex_destroy(m);
}

static const freecodec_ve_port g_default_port = {
    ve_port_open,
    ve_port_close,
    ve_port_ioctl,
    ve_port_mmap,
    ve_port_munmap,
    ve_port_mutex_init,
    ve_port_mutex_lock,
    ve_port_mutex_unlock,
    ve_port_mutex_destroy,
};

static const freecodec_ve_port *g_port = &g_default_port;

const freecodec_ve_port *freecodec_ve_default_port(void)
{
    return &g_default_port;
}

void freecodec_ve_set_port(const freecodec_ve_port *port)
{
    g_port = port ? port : &g_default_port;
}

/* ----------------------------------------------------- register helpers -- */

static uint32_t ve_reg_read(ve_env *e, unsigned int off)
{
    volatile uint8_t *base = (volatile uint8_t *)e->reg_base;
    return *(volatile uint32_t *)(base + off);
}

static void ve_reg_write(ve_env *e, unsigned int off, uint32_t val)
{
    volatile uint8_t *base = (volatile uint8_t *)e->reg_base;
    *(volatile uint32_t *)(base + off) = val;
}

static void ve_mutex_ensure(ve_env *e)
{
    if (!e->mutex_ready) {
        g_port->mutex_init(&e->mutex);
        e->mutex_ready = 1;
    }
}

/* ----------------------------------------------------------- lifecycle -- */

static void *ve_init(fc_ve_config *cfg)
{
    const freecodec_ve_port *port = g_port;
    struct ve_env_info info;
    void *base;
    int fd;

    ve_mutex_ensure(&g_env);
    (void)port->mutex_lock(&g_env.mutex);

    if (getenv("FREECODEC_DBG") != NULL) {
        static int dbg_n;
        if (dbg_n++ < 16)
            fprintf(stderr, "freecodec_ve: init ref=%d dec=%d enc=%d fmt=%d\n",
                    g_env.ref_count,
                    cfg ? cfg->use_decoder : -1,
                    cfg ? cfg->use_encoder : -1,
                    cfg ? cfg->work_mode : -1);
    }

    if (g_env.ref_count > 0) {
        /* Already open: record an encoder caller even if another subsystem opened
         * it first as a decoder (the SDK ionAlloc does). */
        if (cfg != NULL && cfg->use_encoder)
            g_env.is_encoder = 1;
        g_env.ref_count++;
        (void)port->mutex_unlock(&g_env.mutex);
        return &g_env;
    }

    fd = port->open(FC_VE_NODE, O_RDWR);
    if (fd < 0) {
        (void)port->mutex_unlock(&g_env.mutex);
        return NULL;
    }

    /* POWER_SETUP is decoder-only: issued with arg 1 iff nDecoderFlag == 1,
     * never on the encoder path (spec ve-driver.md; spec r2/05 A.2, code 0x700). */
    if (cfg != NULL && cfg->use_decoder == 1) {
        if (port->ioctl(fd, FC_VE_IOC_POWER_SETUP,
                        (void *)(uintptr_t)1) < 0)
            goto fail;
    }

    if (port->ioctl(fd, FC_VE_IOC_REQUEST, NULL) < 0)
        goto fail;

    memset(&info, 0, sizeof(info));
    if (port->ioctl(fd, FC_VE_IOC_GET_ENV, &info) < 0)
        goto fail;

    base = port->mmap(NULL, FC_VE_WINDOW_BYTES,
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == NULL || base == MAP_FAILED)
        goto fail;

    g_env.fd          = fd;
    g_env.reg_base    = base;
    g_env.phys_addr_bias  = info.phys_addr_bias;
    g_env.dram_type   = (int)info.dram_type;
    g_env.ve_freq     = cfg ? cfg->clock_mhz : 0u;
    g_env.enable_afbc = cfg ? cfg->fbc_enable : 0;
    g_env.use_jpeg    = (cfg && cfg->work_mode == FC_VE_WORK_JPEG_DECODE);
    g_env.is_encoder  = (cfg && cfg->use_encoder) ? 1 : 0;
    g_env.adjust_dram_speed = 0;
    g_env.chip_id     = 0;
    g_env.ref_count   = 1;

    /* Module-reset pulse (0x104, arg 0) once per init; ENGINE_REQ only
     * de-asserts, so the encoder FSM would stay in its power-on state. */
    if (port->ioctl(fd, FC_VE_IOC_RESET, (void *)(uintptr_t)0) < 0)
        goto fail;

    /* Programme the DRAM-type field up front (the DRAM-type step); the
     * encoder block will not latch its register script without it. */
    {
        uint32_t v = ve_reg_read(&g_env, VE_REG_TOP_CTRL);
        ve_reg_write(&g_env, VE_REG_TOP_CTRL, v | VE_TOP_DRAM_TYPE);
    }
    /* VE clock set (0x107) to the per-IC rate, every init; value is MHz. */
    if (port->ioctl(fd, FC_VE_IOC_SET_CLK,
                    (void *)(uintptr_t)VE_PERF_FREQ_V833) < 0)
        goto fail;

    (void)port->mutex_unlock(&g_env.mutex);
    return &g_env;

fail:
    port->close(fd);
    (void)port->mutex_unlock(&g_env.mutex);
    return NULL;
}

static void ve_release(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    const freecodec_ve_port *port = g_port;

    (void)port->mutex_lock(&e->mutex);

    if (e->ref_count <= 0) {
        (void)port->mutex_unlock(&e->mutex);
        return;
    }

    if (--e->ref_count > 0) {
        (void)port->mutex_unlock(&e->mutex);
        return;
    }

    (void)port->ioctl(e->fd, FC_VE_IOC_RELEASE, NULL);

    if (e->reg_base) {
        (void)port->munmap(e->reg_base, FC_VE_WINDOW_BYTES);
        e->reg_base = NULL;
    }

    (void)port->ioctl(e->fd, FC_VE_IOC_POWER_DOWN, NULL);
    (void)port->close(e->fd);
    e->fd = -1;

    (void)port->mutex_unlock(&e->mutex);
}

static int ve_lock(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    return g_port->mutex_lock(&e->mutex);
}

static int ve_unlock(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    return g_port->mutex_unlock(&e->mutex);
}

static void ve_reset(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;

    if (!e->reg_base)
        return;

    /* V833 reset (ic 0x00021110_00021210): pulse VETOP+0x04 bit 24, no ioctl
     * 0x104 and no +0x10/+0x14 (the 0x104 form cleared the write pointer). */
    {
        uint32_t v = ve_reg_read(e, VE_REG_TOP_RESET_PULSE);

        ve_reg_write(e, VE_REG_TOP_RESET_PULSE, v | VE_TOP_RESET_BIT);
        ve_reg_write(e, VE_REG_TOP_RESET_PULSE, v & ~(uint32_t)VE_TOP_RESET_BIT);
    }
}

static int ve_wait_irq(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    const freecodec_ve_port *port = g_port;
    unsigned long req = e->use_jpeg ? FC_VE_IOC_WAIT_JPEG_DEC
                                    : FC_VE_IOC_WAIT_ENC;
    int ret = port->ioctl(e->fd, req, (void *)(uintptr_t)1);

    /* 0 when the interrupt fired (ioctl ret >= 1), -1 on timeout/error
     * (see spec r2/05 A.3 slot 5). The caller tests != 0 as failure. */
    return (ret >= 1) ? 0 : -1;
}

/* -------------------------------------------------------------- queries -- */

static int ve_chip_id(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    const freecodec_ve_port *port = g_port;
    int fd = port->open(FC_SOC_INFO_NODE, O_RDWR);

    if (fd >= 0) {
        int id = e->chip_id;
        (void)port->ioctl(fd, VE_SOC_IOCTL_GET_CHIP_ID, &id);
        e->chip_id = id;
        (void)port->close(fd);
    }
    return e->chip_id;
}

static uint64_t ve_ic_version(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    uint32_t lo, hi;

    if (!e->reg_base)
        return 0;
    lo = ve_reg_read(e, VE_REG_TOP_VERSION_LO);
    hi = ve_reg_read(e, VE_REG_TOP_VERSION_HI);
    return ((uint64_t)hi << 32) | lo;
}

static void *ve_group_base(void *p, int id)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    static const unsigned int group_off[] = {
        0x000u, /* VETOP        */
        0x100u, /* MPEG decode  */
        0x200u, /* H264 decode  */
        0x300u, /* VC1 decode   */
        0x400u, /* RV decode    */
        0x500u, /* H265 decode  */
        0xe00u  /* JPEG         */
    };

    if (id < 0 || id >= (int)(sizeof(group_off) / sizeof(group_off[0])))
        return NULL;
    if (!e->reg_base)
        return NULL;
    return (uint8_t *)e->reg_base + group_off[id];
}

static int ve_dram_type(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    return e->dram_type;
}

static unsigned int ve_phys_offset(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    return e->phys_addr_bias;
}

/* -------------------------------------------------------------- tuning -- */

static void ve_apply_dram_type(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    uint32_t v;

    if (!e->reg_base)
        return;

    /* The DRAM-type step writes the DRAM-type field in the topology control
     * word 0x00 (bits [17:16]), not 0x08. */
    v = ve_reg_read(e, VE_REG_TOP_CTRL) & ~(3u << 16);
    switch (e->dram_type) {
    case 0:  break;
    case 1:  v |= (1u << 16); break;
    case 2:
    case 3:  v |= (2u << 16); break;
    default: v |= VE_TOP_DRAM_TYPE; break;
    }
    ve_reg_write(e, VE_REG_TOP_CTRL, v);
}

static void ve_set_ddr_mode(void *p, int mode)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    if (e->reg_base)
        ve_reg_write(e, VE_REG_TOP_DDR_MODE, (uint32_t)mode);
}

static int ve_set_clock(void *p, unsigned int mhz)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    const freecodec_ve_port *port = g_port;
    int fd = port->open(FC_SOC_INFO_NODE, O_RDWR);

    if (fd >= 0) {
        unsigned int v = mhz;
        (void)port->ioctl(fd, VE_SOC_IOCTL_SET_SPEED, &v);
        (void)port->close(fd);
    }
    return port->ioctl(e->fd, FC_VE_IOC_SET_CLK,
                       (void *)(uintptr_t)mhz);
}

static void ve_set_fbc(void *p, int flag)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    e->enable_afbc = flag;
}

static void ve_set_dram_adjust(void *p, int flag)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    e->adjust_dram_speed = flag;
}

static void ve_enc_enable(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    (void)g_port->ioctl(e->fd, FC_VE_IOC_DRAM_HIGH_CHAN,
                        (void *)(uintptr_t)1);

    /* Encoder enable bits in the VETOP control word (the enable step:
     * when nEncoderFlag, set bits 7 and 6). */
    if (e->is_encoder && e->reg_base) {
        uint32_t v = ve_reg_read(e, VE_REG_TOP_CTRL);
        const char *dbg = getenv("FREECODEC_DBG");
        uint32_t nv = v | VE_TOP_ENC_ENABLE | VE_TOP_DRAM_TYPE;

        ve_reg_write(e, VE_REG_TOP_CTRL, nv);
        if (dbg && *dbg) {
            static int n;
            if (n++ < 8)
                fprintf(stderr, "freecodec_ve: enc-enable base=%p enc=%d v0 bef=%08x aft=%08x\n",
                        e->reg_base, e->is_encoder, v,
                        ve_reg_read(e, VE_REG_TOP_CTRL));
        }
    } else {
        const char *dbg = getenv("FREECODEC_DBG");
        if (dbg && *dbg) {
            static int n;
            if (n++ < 8)
                fprintf(stderr, "freecodec_ve: enc-enable SKIP enc=%d base=%p\n",
                        e->is_encoder, e->reg_base);
        }
    }
}

static void ve_enc_disable(void *p)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    (void)g_port->ioctl(e->fd, FC_VE_IOC_DRAM_HIGH_CHAN,
                        (void *)(uintptr_t)0);

    /* Clear the encoder enable bits (the disable step). */
    if (e->is_encoder && e->reg_base) {
        uint32_t v = ve_reg_read(e, VE_REG_TOP_CTRL);
        uint32_t nv = v & ~(uint32_t)VE_TOP_ENC_ENABLE;

        ve_reg_write(e, VE_REG_TOP_CTRL, nv);
    }
}

static void ve_perf_setup(void *p, int mode)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    const freecodec_ve_port *port = g_port;
    unsigned int freq = e->ve_freq ? e->ve_freq : VE_PERF_FREQ_DEFAULT;

    (void)mode;
    (void)port->ioctl(e->fd, FC_VE_IOC_SET_VOLTAGE,
                      (void *)(uintptr_t)VE_PERF_VOLTAGE);
    (void)port->ioctl(e->fd, FC_VE_IOC_SET_CLK,
                      (void *)(uintptr_t)freq);
}

static void ve_perf_teardown(void *p, int mode)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    const freecodec_ve_port *port = g_port;

    (void)mode;
    (void)port->ioctl(e->fd, FC_VE_IOC_SET_CLK,
                      (void *)(uintptr_t)0);
    (void)port->ioctl(e->fd, FC_VE_IOC_SET_VOLTAGE,
                      (void *)(uintptr_t)0);
}

/* --------------------------------------------------------------- IOMMU -- */

static int ve_iommu_map(void *p, fc_ve_iommu_req *param)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    return g_port->ioctl(e->fd, FC_VE_IOC_IOMMU_MAP, param);
}

static int ve_iommu_unmap(void *p, fc_ve_iommu_req *param)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    return g_port->ioctl(e->fd, FC_VE_IOC_IOMMU_UNMAP, param);
}

/* ---------------------------------------------------------------- proc -- */

static int ve_chan_info_set(void *p, char *buf, unsigned int len,
                          unsigned char channel)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    struct ve_proc_info_param param;

    param.buf = buf;
    param.len = len;
    param.channel = channel;
    return g_port->ioctl(e->fd, FC_VE_IOC_CHAN_INFO_SET, &param);
}

static int ve_chan_info_stop(void *p, unsigned char channel)
{
    ve_env *e = p ? (ve_env *)p : &g_env;
    return g_port->ioctl(e->fd, FC_VE_IOC_CHAN_INFO_STOP,
                         (void *)(uintptr_t)channel);
}

/* ----------------------------------------------------------------- ops -- */

static fc_ve_ops g_ve_ops = {
    .open                    = ve_init,
    .close                 = ve_release,
    .lock                    = ve_lock,
    .unlock                  = ve_unlock,
    .reset                   = ve_reset,
    .wait_irq           = ve_wait_irq,
    .chip_id               = ve_chip_id,
    .ic_version          = ve_ic_version,
    .group_base         = ve_group_base,
    .dram_type             = ve_dram_type,
    .phys_offset            = ve_phys_offset,
    .apply_dram_type             = ve_apply_dram_type,
    .set_ddr_mode              = ve_set_ddr_mode,
    .set_clock                = ve_set_clock,
    .set_fbc       = ve_set_fbc,
    .set_dram_adjust  = ve_set_dram_adjust,
    .enc_enable                = ve_enc_enable,
    .enc_disable               = ve_enc_disable,
    .perf_setup  = ve_perf_setup,
    .perf_teardown = ve_perf_teardown,
    .iommu_map            = ve_iommu_map,
    .iommu_unmap           = ve_iommu_unmap,
    .chan_info_set             = ve_chan_info_set,
    .chan_info_stop            = ve_chan_info_stop,
};

fc_ve_ops *GetVeOpsS(int type)
{
    if (type == FC_VE_OPS_DEFAULT)
        return &g_ve_ops;
    return NULL;
}
