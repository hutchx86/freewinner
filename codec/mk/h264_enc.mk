# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
# H.264 encoder device (spec 12): the video_encoder_h264 tables. The host test
# drives them through a fake engine and fake memory ops; no kernel is touched.

H264_ENC_SRCS := src/h264/enc/freecodec_enc.c src/h264/enc/freecodec_enc_regs.c
H264_ENC_OBJS := $(BUILD)/h264/enc/freecodec_enc.o $(BUILD)/h264/enc/freecodec_enc_regs.o
H264_ENC_DEV_HDRS := src/h264/enc/freecodec_enc_priv.h include/freecodec/h264_rc.h \
    include/freecodec/h264_regs.h include/freecodec/h264_headers.h \
    include/freecodec/h264_isp.h include/freecodec/h264_bufs.h \
    include/freecodec/vencoder.h include/freecodec/venc_ext.h

$(BUILD)/h264/enc/%.o: src/h264/enc/%.c $(H264_ENC_DEV_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc/h264/enc -c $< -o $@

# Unit objects from mk/h264.mk, mk/h264_rc.mk and mk/venc_base.mk; VE_OBJS only
# satisfies vb_sys.c's GetVeOpsS() reference (the test wires a fake ops table).
$(BUILD)/h264/test_enc: tests/h264/test_enc.c $(H264_ENC_OBJS) $(H264_UNIT_OBJS) \
		$(H264_RC_OBJS) $(VENC_BASE_OBJS) $(VE_OBJS) $(H264_ENC_DEV_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc/h264/enc -Isrc/base -pthread \
	    -DENC_VECTOR_DIR='"$(CURDIR)/../spec/vectors"' \
	    $< $(H264_ENC_OBJS) $(H264_UNIT_OBJS) $(H264_RC_OBJS) $(VENC_BASE_OBJS) $(VE_OBJS) \
	    $(LDFLAGS) -pthread -o $@

H264_ENC_TESTS += $(BUILD)/h264/test_enc
