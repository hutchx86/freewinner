# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 freewinner contributors
#
# H.265 encoder device (spec h265/12): the video_encoder_h265 table. The host
# test drives it through a fake engine and fake memory ops, like the H.264
# device test; no kernel or engine is touched.

H265_ENC_SRCS := src/h265/enc/freecodec_h265_enc.c src/h265/enc/freecodec_h265_enc_regs.c
H265_ENC_OBJS := $(BUILD)/h265/enc/freecodec_h265_enc.o \
                 $(BUILD)/h265/enc/freecodec_h265_enc_regs.o
H265_ENC_DEV_HDRS := src/h265/enc/freecodec_h265_enc_priv.h \
    include/freecodec/h265_rc.h include/freecodec/h265_regs.h \
    include/freecodec/h265_headers.h include/freecodec/h264_isp.h \
    include/freecodec/h264_bufs.h include/freecodec/vencoder.h

$(BUILD)/h265/enc/%.o: src/h265/enc/%.c $(H265_ENC_DEV_HDRS) $(H265_HDRS) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc/h265/enc -c $< -o $@

# Unit objects from mk/h265.mk; the shared ISP object from mk/h264_isp.mk; the
# VE/support-library objects satisfy vb_sys.c's GetVeOpsS reference (the test
# wires a fake ops table).
$(BUILD)/h265/test_enc: tests/h265/test_enc.c $(H265_ENC_OBJS) $(H265_UNIT_OBJS) \
		$(H265_RC_OBJS) $(BUILD)/h264/isp/h264_isp.o \
		$(VENC_BASE_OBJS) $(VE_OBJS) $(H265_ENC_DEV_HDRS) $(H265_HDRS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc/h265/enc -Isrc/base -pthread \
	    $< $(H265_ENC_OBJS) $(H265_UNIT_OBJS) $(H265_RC_OBJS) \
	    $(BUILD)/h264/isp/h264_isp.o $(VENC_BASE_OBJS) $(VE_OBJS) \
	    $(LDFLAGS) -pthread -o $@

H265_ENC_TESTS := $(BUILD)/h265/test_enc
