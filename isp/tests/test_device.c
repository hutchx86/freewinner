// SPDX-License-Identifier: AGPL-3.0-only
/* Device-layer tests: A-GRAPH-1..4, A-DEV-OPEN, A-SENSOR, A-VIDEO (23 §2) */
#include "test.h"
#include "fake_kernel.h"
#include <string.h>

static struct media_entity *ent(struct media_device *md, const char *name)
{
    unsigned int i;
    for (i = 0; i < md->num_entities; i++)
        if (strcmp(md->entities[i].info.name, name) == 0)
            return &md->entities[i];
    return NULL;
}

static void setup(void)
{
    fk_reset();
    fk_topology_default();
    isp_uapi_set_sys(&fake_sys);
}

int main(void)
{
    struct media_device *md;
    struct hw_isp_media_dev mdev;
    struct hw_isp_device *dev;
    struct isp_table_reg_map reg;
    int i;

    setup();
    md = media_open("/dev/media0", 0);
    CHECK(md != NULL, "media_open");
    CHECK(md->num_entities == 8, "8 entities (got %u)", md->num_entities);

    /* A-GRAPH-1: NEXT enumeration, NEXT stripped, devnames resolved */
    for (i = 0; i < md->num_entities; i++)
        CHECK(md->entities[i].info.id == (unsigned)(i + 1),
              "entity %d id %u", i, md->entities[i].info.id);
    CHECK(ent(md, "gc3003_mipi")->devname[0] != '\0', "sensor devname");
    CHECK(strcmp(ent(md, "vin_video0")->devname, "/dev/video0") == 0,
          "video0 devname=%s", ent(md, "vin_video0")->devname);

    /* A-GRAPH-3: pipeline head */
    CHECK(media_pipeline_head(md, ent(md, "sunxi_isp.0")) ==
              ent(md, "gc3003_mipi"), "head is sensor");
    CHECK(media_pipeline_head(md, ent(md, "gc3003_mipi")) ==
              ent(md, "gc3003_mipi"), "head of sensor itself");
    {
        unsigned int t = ent(md, "gc3003_mipi")->info.type;
        ent(md, "gc3003_mipi")->info.type = ISP_MEDIA_ENT_T_V4L2_SUBDEV;
        CHECK(media_pipeline_head(md, ent(md, "sunxi_isp.0")) == NULL,
              "non-sensor head rejected");
        ent(md, "gc3003_mipi")->info.type = (typeof(t))t;
    }

    /* A-GRAPH-2: links on both endpoints, merged */
    CHECK(ent(md, "sun6i_csi")->num_links >= 2, "csi links %u",
          ent(md, "sun6i_csi")->num_links);
    CHECK(ent(md, "gc3003_mipi")->num_links >= 1, "sensor links");

    /* A-GRAPH-4: id walk hidden, then after refresh */
    CHECK(isp_entity_to_isp_id(md, ent(md, "vin_video0")) == -1,
          "hidden link -> -1");
    fk_configure_video_links();
    CHECK(media_refresh_links(md) == 0, "refresh");
    CHECK(isp_entity_to_isp_id(md, ent(md, "vin_video0")) == 0,
          "id after refresh");

    /* A-DEV-OPEN: order + H3A size honoured */
    memset(&mdev, 0, sizeof(mdev));
    mdev.mdev = md;
    fk.h3a_buf_size = 0x4000;
    CHECK(isp_dev_open(&mdev, 0) == 0, "dev_open");
    dev = mdev.isp_dev[0];
    CHECK(dev != NULL && dev->stats_size == 0x4000, "stats size %u",
          dev ? dev->stats_size : 0);
    CHECK(fk_count(VIDIOC_VIN_ISP_H3A_CFG) == 1, "one H3A_CFG");
    {
        struct isp_h3a_cfg *c =
            (struct isp_h3a_cfg *)fk_last(VIDIOC_VIN_ISP_H3A_CFG)->data;
        CHECK(c->config_counter == 0, "config_counter 0");
    }

    /* A-SENSOR: config fetch */
    fk.sensor_cfg.width = 1920;
    fk.sensor_cfg.height = 1080;
    fk.sensor_cfg.hts = 2200;
    {
        struct sensor_config cfg;
        memset(&cfg, 0, sizeof(cfg));
        CHECK(isp_sensor_get_configs(dev, &cfg) == 0, "cfg req");
        CHECK(cfg.width == 1920 && cfg.height == 1080 && cfg.hts == 2200,
              "cfg payload");
    }
    /* load-reg size override */
    reg.addr = (void *)0x1234;
    reg.size = 0x99;
    dev->load_type = 0;
    isp_set_load_reg(dev, &reg);
    CHECK(fk.load_size == 0x99, "size untouched when load_type 0");
    dev->load_type = 1;
    isp_set_load_reg(dev, &reg);
    CHECK(fk.load_size == ISP_LOAD_DRAM_SIZE, "size override");

    /* A-DEV-OPEN unwind on failure */
    isp_dev_close(&mdev, 0);
    CHECK(mdev.isp_dev[0] == NULL, "dev closed");
    fk_fail_on(VIDIOC_VIN_ISP_H3A_CFG, 1);
    CHECK(isp_dev_open(&mdev, 0) == -1, "dev_open fails");
    CHECK(mdev.isp_dev[0] == NULL, "no leak on failure");

    /* unknown id rejected */
    CHECK(isp_dev_open(&mdev, 1) == -1, "id 1 rejected");

    /* A-VIDEO: video open (fallback to id 0 when hidden), set_fmt */
    {
        struct video_fmt vfmt;
        struct isp_video_device *v;
        CHECK(isp_video_open(&mdev, 0) == 0, "video open");
        v = mdev.video_dev[0];
        CHECK(v != NULL && v->isp_id == 0, "video isp_id 0");
        memset(&vfmt, 0, sizeof(vfmt));
        vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        vfmt.memtype = V4L2_MEMORY_MMAP;
        vfmt.fps = 30;
        vfmt.capturemode = 1;
        vfmt.use_current_win = 1;
        vfmt.wdr_mode = 2;
        vfmt.nbufs = 4;
        vfmt.format.width = 1920;
        vfmt.format.height = 1080;
        vfmt.format.pixelformat = v4l2_fourcc('N', 'M', '1', '2');
        vfmt.format.num_planes = 2;
        CHECK(video_set_fmt(v, &vfmt) == 0, "set_fmt");
        {
            struct fk_call *c = fk_last(VIDIOC_S_PARM);
            struct v4l2_streamparm *p = (struct v4l2_streamparm *)c->data;
            CHECK(p->parm.capture.timeperframe.numerator == 1 &&
                      p->parm.capture.timeperframe.denominator == 30,
                  "timeperframe");
            CHECK(p->parm.capture.reserved[0] == 1 &&
                      p->parm.capture.reserved[1] == 2 &&
                      p->parm.capture.reserved[2] == 0 &&
                      p->parm.capture.reserved[3] == 0,
                  "reserved");
        }
        CHECK(v->nplanes == 2 && v->nbufs == 4, "cached fmt");

        /* buffers */
        CHECK(buffers_pool_new(v) != NULL, "pool new");
        CHECK(video_req_buffers(v, v->pool) == 0, "req buffers");
        CHECK(v->pool->nbufs == 4, "pool nbufs");
        {
            struct video_buffer b;
            struct video_plane pl[2];
            memset(&b, 0, sizeof(b));
            memset(pl, 0, sizeof(pl));
            b.planes = pl;
            fk.dq_reserved = 77;
            fk.dq_reserved2 = 88;
            CHECK(video_dequeue_buffer(v, &b) == 0, "dqbuf");
            CHECK(b.frame_cnt == 77 && b.exp_time == 88, "dq fields");
            CHECK(pl[0].mem_phy == 0x1000, "mem_phy");
        }
        {
            struct fk_call *c;
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            CHECK(video_stream_on(v) == 0, "stream on");
            c = fk_last(VIDIOC_STREAMON);
            CHECK(c != NULL && *(int *)c->data == type, "streamon type");
        }
        buffers_pool_delete(v);
        isp_video_close(&mdev, 0);
        CHECK(mdev.video_dev[0] == NULL, "video closed");
    }

    /* A-VIDEO: SoC 4K>25fps refusal */
    {
        struct video_fmt vfmt;
        struct isp_video_device *v;
        CHECK(isp_video_open(&mdev, 0) == 0, "reopen video");
        v = mdev.video_dev[0];
        fk.soc_bits = 0x20;
        memset(&vfmt, 0, sizeof(vfmt));
        vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        vfmt.memtype = V4L2_MEMORY_MMAP;
        vfmt.fps = 30;
        vfmt.nbufs = 2;
        vfmt.format.width = 3840;
        vfmt.format.height = 2160;
        vfmt.format.num_planes = 2;
        CHECK(video_set_fmt(v, &vfmt) == -1, "soc refusal");
        fk.soc_bits = 0;
        CHECK(video_set_fmt(v, &vfmt) == 0, "no soc bit -> ok");
        isp_video_close(&mdev, 0);
    }

    /* A-ENUM (host): isp_table_reg_map field order */
    CHECK(offsetof(struct isp_table_reg_map, addr) == 0, "reg.addr@0");
    CHECK(offsetof(struct isp_table_reg_map, size) == sizeof(void *),
          "reg.size@ptr");

    media_close(md);
    TEST_END("F-DEV");
}
