// SPDX-License-Identifier: AGPL-3.0-only
/* manage.c - lifecycle, 3A bring-up/tear-down and the register program buffer */
#define _GNU_SOURCE   /* pthread_setname_np */
#include "framework_internal.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The 3A entry points (ae_init, awb_init, ...) live in isp/shim/ and return the
 * fwi_*_core_ops_t vtable reached via ctx->*_entity.ops; bring_up_3a() only wires them in. */

struct hw_isp_media_dev media_params;

extern fwi_isp_ctx_t isp_ctx[1];

static struct fwi_tuning_store tuning_store[HW_ISP_DEVICE_NUM];

struct fwi_tuning_store *isp_tuning_store_get(int id)
{
    if (id < 0 || id >= HW_ISP_DEVICE_NUM)
        return NULL;
    return &tuning_store[id];
}

int media_dev_init(void)
{
    printf("freeisp framework\n");
    return 0;
}

void media_dev_exit(void)
{
    if (media_params.isp_use_cnt[0] == 0 && media_params.mdev != NULL) {
        media_close(media_params.mdev);
        media_params.mdev = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* register program buffer                                             */
/* ------------------------------------------------------------------ */
#define BAYER_OFF 0x1600
#define RGB_OFF   0x7b40
#define YUV_OFF   0x9740

static int alloc_reg_buffer(fwi_isp_ctx_t *ctx)
{
    uint8_t *base = calloc(1, ISP_LOAD_DRAM_SIZE);
    fwi_hw_module_cfg_t *M = &ctx->hw_cfg;
    const struct freeisp_tables *t;
    int i;

    if (base == NULL)
        return -1;

    t = freeisp_get_tables();
    if (t != NULL && t->base != NULL && t->base->default_reg != NULL)
        memcpy(base, t->base->default_reg, ISP_LOAD_REG_SIZE);
    else
        memset(base, 0, ISP_LOAD_REG_SIZE);

    M->dev_id = (uint32_t)ctx->isp_index;
    M->platform_id = 1; /* ISP_PLATFORM_SUN8IW16P1 */
    isp_map_addr(M, (unsigned long)base);

    M->fe_table = base + 0x1000;
    M->bayer_table = base + BAYER_OFF;
    M->rgb_table = base + RGB_OFF;
    M->yuv_table = base + YUV_OFF;
    M->linearize_table = base + BAYER_OFF + 0x0000;
    M->wdr_table = base + BAYER_OFF + 0x0600;
    M->lens_table = base + BAYER_OFF + 0x4600;
    M->temporal_denoise_table = base + BAYER_OFF + 0x4e00;
    M->pltm_table = base + BAYER_OFF + 0x5000;
    M->mesh_shading_table = base + BAYER_OFF + 0x5600;
    M->saturation_table = base + RGB_OFF + 0x0000;
    M->drc_table = base + RGB_OFF + 0x0200;
    M->gamma_table = base + RGB_OFF + 0x0400;
    M->dehaze_table = base + RGB_OFF + 0x1400;
    M->colour_enhance_table = base + YUV_OFF + 0x0000;

    ctx->reg_image = base;
    (void)i;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 3A bring-up and tear-down                                           */
/* ------------------------------------------------------------------ */
static int bring_up_3a(fwi_isp_ctx_t *ctx, struct hw_isp_device *dev)
{
    (void)dev;
    /* order: ISO, AFS, AWB, AE, GTM, PLTM.  Each *_init returns its
     * fwi_*_core_ops_t vtable straight into ctx->*_entity.ops. */
    /* The ISO entity field is typed fwi_iso_core_ops_t; fwi_iso_ops() casts
     * it back to the fwi_iso_cfg_core_ops_t the shim returns. */
    {
        fwi_iso_cfg_core_ops_t *iso_ops = NULL;
        ctx->iso_entity.iso_entity = iso_init(&iso_ops);
        if (ctx->iso_entity.iso_entity == NULL || iso_ops == NULL)
            goto err;
        ctx->iso_entity.ops = (fwi_iso_core_ops_t *)iso_ops;
    }
    fwi_iso_ops(ctx->iso_entity.ops)
        ->iso_get_params(ctx->iso_entity.iso_entity,
                         &ctx->iso_entity.iso_param);

    ctx->afs_entity.afs_entity = afs_init(&ctx->afs_entity.ops);
    if (ctx->afs_entity.afs_entity == NULL || ctx->afs_entity.ops == NULL)
        goto err;
    ctx->afs_entity.ops->afs_get_params(ctx->afs_entity.afs_entity,
                                        &ctx->afs_entity.afs_param);

    ctx->awb_entity.awb_entity = awb_init(&ctx->awb_entity.ops);
    if (ctx->awb_entity.awb_entity == NULL || ctx->awb_entity.ops == NULL)
        goto err;
    ctx->awb_entity.ops->awb_get_params(ctx->awb_entity.awb_entity,
                                        &ctx->awb_entity.awb_param);
    if (ctx->awb_entity.awb_param)
        ctx->awb_entity.awb_param->platform_id = (int32_t)ctx->hw_cfg.platform_id;

    ctx->ae_entity.ae_entity = ae_init(&ctx->ae_entity.ops);
    if (ctx->ae_entity.ae_entity == NULL || ctx->ae_entity.ops == NULL)
        goto err;
    ctx->ae_entity.ops->ae_get_params(ctx->ae_entity.ae_entity,
                                      &ctx->ae_entity.ae_param);
    if (ctx->ae_entity.ae_param)
        ctx->ae_entity.ae_param->platform_id = (int32_t)ctx->hw_cfg.platform_id;

    ctx->gtm_entity.gtm_entity = gtm_init(&ctx->gtm_entity.ops);
    if (ctx->gtm_entity.gtm_entity == NULL || ctx->gtm_entity.ops == NULL)
        goto err;
    ctx->gtm_entity.ops->gtm_get_params(ctx->gtm_entity.gtm_entity,
                                        &ctx->gtm_entity.gtm_param);
    if (ctx->gtm_entity.gtm_param)
        ctx->gtm_entity.gtm_param->platform_id = (int)ctx->hw_cfg.platform_id;

    ctx->pltm_entity.pltm_entity = pltm_init(&ctx->pltm_entity.ops);
    if (ctx->pltm_entity.pltm_entity == NULL || ctx->pltm_entity.ops == NULL)
        goto err;
    ctx->pltm_entity.ops->pltm_get_params(ctx->pltm_entity.pltm_entity,
                                          &ctx->pltm_entity.pltm_param);
    if (ctx->pltm_entity.pltm_param)
        ctx->pltm_entity.pltm_param->platform_id = (int)ctx->hw_cfg.platform_id;

    if (alloc_reg_buffer(ctx) != 0)
        goto err;
    pthread_mutex_init((pthread_mutex_t *)&ctx->lock, NULL);
    return 0;
err:
    return -1;
}

static void tear_down_3a(fwi_isp_ctx_t *ctx)
{
    pthread_mutex_lock((pthread_mutex_t *)&ctx->lock);
    if (ctx->afs_entity.afs_entity)
        afs_exit(ctx->afs_entity.afs_entity);
    if (ctx->awb_entity.awb_entity)
        awb_exit(ctx->awb_entity.awb_entity);
    if (ctx->ae_entity.ae_entity)
        ae_exit(ctx->ae_entity.ae_entity);
    if (ctx->gtm_entity.gtm_entity)
        gtm_exit(ctx->gtm_entity.gtm_entity);
    if (ctx->pltm_entity.pltm_entity)
        pltm_exit(ctx->pltm_entity.pltm_entity);
    if (ctx->iso_entity.iso_entity)
        iso_exit(ctx->iso_entity.iso_entity);
    free(ctx->reg_image);
    ctx->reg_image = NULL;
    pthread_mutex_unlock((pthread_mutex_t *)&ctx->lock);
    pthread_mutex_destroy((pthread_mutex_t *)&ctx->lock);
}

/* ------------------------------------------------------------------ */
/* sensor info from the sensor config                                  */
/* ------------------------------------------------------------------ */
static int fill_sensor_info(fwi_isp_ctx_t *ctx, struct hw_isp_device *dev,
                            const char *name)
{
    struct sensor_config cfg;
    fwi_sensor_state_t *s = &ctx->sensor;

    if (name == NULL)
        return -1;
    s->name = (char *)name;
    memset(&cfg, 0, sizeof(cfg));
    if (isp_sensor_get_configs(dev, &cfg) != 0)
        return -1;
    s->sensor_width = (int32_t)cfg.width;
    s->sensor_height = (int32_t)cfg.height;
    s->fps_fixed = cfg.fps_fixed;
    s->wdr_mode = cfg.wdr_mode;
    s->colour_space = 0;
    /* Allwinner SDK media-bus literals (not mainline media-bus-format.h, where 0x300f is a
     * different Bayer order): 8/10/12-bit code per phase -> input_seq; unknown -> BGGR. */
    switch (cfg.mbus_code) {
    case 0x3001: /* BGGR 8-bit  */
    case 0x3007: /* BGGR 10-bit */
    case 0x3008: /* BGGR 12-bit */
        s->input_seq = (uint32_t)FWI_INPUT_SEQ_BGGR;
        break;
    case 0x3013: /* GBRG 8-bit  */
    case 0x300e: /* GBRG 10-bit */
    case 0x3010: /* GBRG 12-bit */
        s->input_seq = (uint32_t)FWI_INPUT_SEQ_GBRG;
        break;
    case 0x3002: /* GRBG 8-bit  */
    case 0x300a: /* GRBG 10-bit */
    case 0x3011: /* GRBG 12-bit */
        s->input_seq = (uint32_t)FWI_INPUT_SEQ_GRBG;
        break;
    case 0x3014: /* RGGB 8-bit  */
    case 0x300f: /* RGGB 10-bit (SDK literal; mainline calls it SBGGR12) */
    case 0x3012: /* RGGB 12-bit */
        s->input_seq = (uint32_t)FWI_INPUT_SEQ_RGGB;
        break;
    default:
        s->input_seq = (uint32_t)FWI_INPUT_SEQ_BGGR;
        break;
    }
    if (cfg.hts && cfg.vts && cfg.pclk) {
        s->hts = cfg.hts;
        s->vts = cfg.vts;
        s->pclk = cfg.pclk;
        s->bin_factor = cfg.bin_factor;
        s->gain_min = cfg.gain_min;
        s->gain_max = cfg.gain_max;
        s->hoffset = cfg.hoffset;
        s->voffset = cfg.voffset;
    } else {
        s->hts = cfg.width;
        s->vts = cfg.height;
        s->pclk = cfg.width * cfg.height * 30;
        s->bin_factor = 1;
        s->gain_min = 16;
        s->gain_max = 255;
        s->hoffset = 0;
        s->voffset = 0;
    }
    ctx->drv_stats_ref.pic_size.width = cfg.width;
    ctx->drv_stats_ref.pic_size.height = cfg.height;
    ctx->stats.pic_w = cfg.width;
    ctx->stats.pic_h = cfg.height;
    ctx->otp_en = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* isp_init                                                            */
/* ------------------------------------------------------------------ */
int isp_init(int id)
{
    fwi_isp_ctx_t *ctx;
    struct hw_isp_device *dev;
    struct fwi_tuning_store *st;

    if (id >= HW_ISP_DEVICE_NUM || id < 0)
        return -1;
    media_params.isp_use_cnt[id]++;
    if (media_params.isp_use_cnt[id] > 1)
        return 0;

    ctx = &isp_ctx[id];
    if (media_params.mdev != NULL) {
        media_close(media_params.mdev);
        media_params.mdev = NULL;
    }
    media_params.mdev = media_open("/dev/media0", 0);
    if (media_params.mdev == NULL) {
        media_params.isp_use_cnt[id] = 0;
        return -1;
    }
    if (isp_dev_open(&media_params, id) < 0) {
        media_params.isp_use_cnt[id] = 0;
        return -1;
    }
    dev = media_params.isp_dev[id];
    ctx->isp_index = id;
    ctx->callbacks = NULL;

    if (fill_sensor_info(ctx, dev, dev->sensor.info.name) != 0) {
        media_params.isp_use_cnt[id] = 0;
        return -1;
    }

    media_params.ir_flag = 0;
    media_params.wdr_flag = (int)ctx->sensor.wdr_mode;
    ctx->ir_mode = 0;
    parser_ini_info(&ctx->tuning, ctx->sensor.name, ctx->sensor.sensor_width,
                    ctx->sensor.sensor_height, (int)ctx->sensor.fps_fixed,
                    media_params.wdr_flag, 0, 0, id);

    if (bring_up_3a(ctx, dev) != 0) {
        media_params.isp_use_cnt[id] = 0;
        return -1;
    }

    st = &tuning_store[id];
    st->params = ctx->tuning;
    pthread_mutex_init(&st->lock, NULL);
    st->dev = dev;
    st->ctx = ctx;
    if (isp_config_init(ctx) != 0) {
        /* reference behaviour: init still completes when sensor_height == 0 */
    }

    if (ctx->tuning.enables.auto_focus_en ||
        ctx->tuning.enables.sweep_focus_en) {
        struct { uint16_t code_min, code_max; } act;
        act.code_min = (uint16_t)ctx->tuning.a3.vcm_min_code;
        act.code_max = (uint16_t)ctx->tuning.a3.vcm_max_code;
        isp_uapi_sys->ioctl(dev->sensor.fd, VIDIOC_VIN_ACT_INIT, &act);
    }

    isp_loop_start(isp_loop_for(id));
    {
        struct fwi_table_reg_map reg;
        reg.addr = ctx->reg_image;
        reg.size = ISP_LOAD_DRAM_SIZE;
        isp_set_load_reg(dev, &reg);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* run / stop / join; exit                                             */
/* ------------------------------------------------------------------ */
static void *isp_thread_body(void *arg)
{
    int id = (int)(long)arg;
    fwi_isp_ctx_t *ctx = &isp_ctx[id];
    char name[16] = "isp_thread";

    pthread_setname_np(pthread_self(), name);
    isp_event_start(ctx);
    isp_loop_run(isp_loop_for(id));
    isp_event_stop(ctx);
    return NULL;
}

int isp_run(int id)
{
    struct hw_isp_device *dev;
    int ret;

    if (id >= HW_ISP_DEVICE_NUM || id < 0)
        return -1;
    if (media_params.isp_use_cnt[id] > 1)
        return 0;
    dev = media_params.isp_dev[id];
    if (dev == NULL)
        return -1;
    ret = pthread_create(&media_params.isp_tid[id], NULL, isp_thread_body,
                         (void *)(long)id);
    return ret;
}

int isp_stop(int id)
{
    if (id >= HW_ISP_DEVICE_NUM || id < 0)
        return -1;
    if (media_params.isp_use_cnt[id] == 1)
        isp_loop_stop(isp_loop_for(id));
    return 0;
}

int32_t isp_pthread_join(int id)
{
    (void)id;
    return 0;
}

int isp_exit(int id)
{
    struct fwi_tuning_store *st;

    if (id >= HW_ISP_DEVICE_NUM || id < 0)
        return -1;
    if (media_params.isp_use_cnt[id] == 0)
        return 0;
    media_params.isp_use_cnt[id]--;
    if (media_params.isp_use_cnt[id] > 0)
        return 0;
    if (media_params.isp_dev[id] == NULL)
        return -1;

    pthread_join(media_params.isp_tid[id], NULL);

    st = &tuning_store[id];
    pthread_mutex_lock(&st->lock);
    pthread_mutex_unlock(&st->lock);
    pthread_mutex_destroy(&st->lock);
    st->dev = NULL;
    st->ctx = NULL;

    tear_down_3a(&isp_ctx[id]);
    isp_dev_close(&media_params, id);
    return 0;
}
