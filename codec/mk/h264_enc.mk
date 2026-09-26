# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.264 encoder device (spec/12-encoder-device.md, r4): the video_encoder_h264
# VENC_DEVICE tables. Built against the kept register/header/ISP/gop units,
# this pass's rate-control unit (h264_rc), and the kept support library
# (venc_base) for the bitstream ring. The host test drives the device table
# through a fake engine (register window + counters) and a fake memory-
# operations table -- no kernel or engine is touched.

H264_ENC_SRCS := src/h264/enc/freecodec_enc.c src/h264/enc/freecodec_enc_regs.c
H264_ENC_OBJS := $(BUILD)/h264/enc/freecodec_enc.o $(BUILD)/h264/enc/freecodec_enc_regs.o
H264_ENC_DEV_HDRS := src/h264/enc/freecodec_enc_priv.h include/freecodec/h264_rc.h \
    include/freecodec/h264_regs.h include/freecodec/h264_headers.h \
    include/freecodec/h264_isp.h include/freecodec/h264_bufs.h \
    include/freecodec/vencoder.h include/freecodec/venc_ext.h

$(BUILD)/h264/enc/%.o: src/h264/enc/%.c $(H264_ENC_DEV_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc/h264/enc -c $< -o $@

# The device needs the register/header/ISP/rate-control units' objects plus
# the support library's bitstream ring (venc_base). H264_UNIT_OBJS (regs,
# headers, isp) comes from mk/h264.mk; H264_RC_OBJS from mk/h264_rc.mk;
# VENC_BASE_OBJS from mk/venc_base.mk.
# VE_OBJS (mk/ve.mk) supplies GetVeOpsS(), referenced by venc_base's own
# vb_sys.c seam but never actually invoked by this test (the fake fc_ve_ops
# table is wired in directly) -- linked only to satisfy the symbol.
$(BUILD)/h264/test_enc: tests/h264/test_enc.c $(H264_ENC_OBJS) $(H264_UNIT_OBJS) \
		$(H264_RC_OBJS) $(VENC_BASE_OBJS) $(VE_OBJS) $(H264_ENC_DEV_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc/h264/enc -Isrc/base -pthread \
	    -DENC_VECTOR_DIR='"$(CURDIR)/../spec/vectors"' \
	    $< $(H264_ENC_OBJS) $(H264_UNIT_OBJS) $(H264_RC_OBJS) $(VENC_BASE_OBJS) $(VE_OBJS) \
	    $(LDFLAGS) -pthread -o $@

H264_ENC_TESTS += $(BUILD)/h264/test_enc
