/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Behavioural test for the AAC-LC wrapper: ADTS framing, timestamps and underflow,
 * encoding a one-second sine to build/aac_out.aac. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "freecodec/aac_iface.h"

#define FS        16000
#define CH        1
#define SAMPLES   1024u
#define FRAME_B   4096u

/* ---- a minimal PCM ring matching the consumer's semantics ---- */
typedef struct {
    unsigned char buf[64 * 1024];
    unsigned char *rd;
    unsigned char *wr;
    int data_len;
    int free_len;
} test_ring;

static test_ring g_ring;
static int        g_fail;

int GetPcmDataSize(__pcm_buf_manager_t *m)
{
    (void)m;
    return g_ring.data_len;
}

int ReadPcmDataForEnc(void *dst, int len, __pcm_buf_manager_t *m)
{
    (void)m;
    if (len <= 0 || g_ring.data_len < len)
        return 0;
    memcpy(dst, g_ring.rd, (size_t)len);
    g_ring.rd += len;
    g_ring.data_len -= len;
    g_ring.free_len += len;
    return len;
}

static void ring_reset(void)
{
    memset(&g_ring, 0, sizeof(g_ring));
    g_ring.rd = g_ring.buf;
    g_ring.wr = g_ring.buf;
    g_ring.free_len = (int)sizeof(g_ring.buf);
}

/* append a 16-bit sine, in samples */
static void ring_fill_sine(unsigned *phase, unsigned samples)
{
    for (unsigned i = 0; i < samples; i++) {
        double t = (double)(*phase) / FS;
        int16_t v = (int16_t)(10000.0 * sin(2.0 * 3.14159265358979 * 440.0 * t));
        memcpy(g_ring.wr, &v, 2);
        g_ring.wr += 2;
        g_ring.data_len += 2;
        g_ring.free_len -= 2;
        (*phase)++;
    }
}

static void check(int cond, const char *what, int frame)
{
    if (!cond) {
        fprintf(stderr, "FAIL [frame %d]: %s\n", frame, what);
        g_fail = 1;
    }
}

int main(void)
{
    struct __AudioENC_AC320 *enc;
    __pcm_buf_manager_t      man;
    __audio_enc_inf_t        inf;
    __com_internal_prameter_t com;
    unsigned char            out[FRAME_B];
    unsigned                 phase = 0;
    int                      emitted = 0, calls = 0, ret;
    unsigned                 last_ts = 0;
    FILE                    *fp;

    ring_reset();
    memset(&man, 0, sizeof(man));
    memset(&inf, 0, sizeof(inf));
    memset(&com, 0, sizeof(com));

    inf.InSamplerate  = FS;
    inf.InChan        = CH;
    inf.bitrate       = 0;      /* library default (consumer passes 0) */
    inf.SamplerBits   = 16;
    inf.OutSamplerate = FS;
    inf.OutChan       = CH;
    inf.frame_style   = 0;      /* ADTS */

    enc = AudioAACENCEncInit();
    if (!enc) { fprintf(stderr, "FAIL: AudioAACENCEncInit\n"); return 1; }
    enc->pPcmBufManager = &man;
    enc->AudioBsEncInf  = &inf;
    enc->EncoderCom     = &com;
    if (enc->EncInit(enc) != 0) { fprintf(stderr, "FAIL: EncInit\n"); return 1; }
    check(com.header_len == 7, "EncInit header length == 7", -1);
    check(com.header[0] == 0xFF && com.header[1] == 0xF1,
          "EncInit ADTS sync", -1);

    /* underflow must report PCMUNDERFLOW, not error */
    {
        int n = -1;
        ret = enc->EncFrame(enc, (char *)out, &n);
        check(ret == FC_AAC_WANT_PCM, "underflow returns PCMUNDERFLOW", -1);
        check(n == 0, "underflow leaves outLen 0", -1);
    }

    fp = fopen("build/aac_out.aac", "wb");
    if (!fp) { fprintf(stderr, "note: cannot open build/aac_out.aac\n"); }

    /* feed ~1.2 s of audio one frame at a time */
    while (calls < 20) {
        int n = 0;
        ring_fill_sine(&phase, SAMPLES);
        ret = enc->EncFrame(enc, (char *)out, &n);
        calls++;
        if (ret != ERR_AUDIO_ENC_NONE) {
            fprintf(stderr, "FAIL: EncFrame ret=%d at call %d\n", ret, calls);
            g_fail = 1;
            break;
        }
        if (n == 0)
            continue;   /* priming */

        check(n >= 7, "frame at least 7 bytes", emitted);
        check(out[0] == 0xFF && out[1] == 0xF1, "ADTS sync", emitted);
        check(((out[2] >> 6) & 3) == 1, "AAC-LC profile", emitted);
        check(((out[2] >> 2) & 0xF) == 8, "sample-rate index 8 (16 kHz)", emitted);
        check((((out[2] & 1) << 2) | (out[3] >> 6)) == 1, "mono channel config", emitted);
        {
            unsigned flen = ((unsigned)(out[3] & 3) << 11) |
                            ((unsigned)out[4] << 3) |
                            ((unsigned)out[5] >> 5);
            check((int)flen == n, "aac_frame_length matches returned size", emitted);
        }
        check(com.pts_ms > last_ts || emitted == 0, "timestamp increases", emitted);
        last_ts = com.pts_ms;

        if (fp) fwrite(out, 1, (size_t)n, fp);
        emitted++;
    }

    if (fp) fclose(fp);

    check(emitted >= 15, "emitted about one frame per input frame", -1);
    check(com.frames_out == (unsigned)emitted, "frames_out tracks emitted frames", -1);

    /* stop condition */
    com.command = FC_AAC_CMD_DRAIN;
    {
        int n = 0;
        ret = enc->EncFrame(enc, (char *)out, &n);
        check(ret == FC_AAC_INPUT_ENDED, "stop returns ABSEND", -1);
    }

    enc->EncExit(enc);
    AudioAACENCEncExit(enc);

    printf("aac: %d frames emitted from %d calls, %u ms last ts, %d failures\n",
           emitted, calls, last_ts, g_fail);
    return g_fail ? 1 : 0;
}
