/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host test for the H.265 rate-control model, pinning the parts the spec states
 * exactly: the bpp ratio table and +2, the every-fifth-GOP ratio_p rule, the
 * fixed model lambda constants and the QP estimator (spec h265/10 sections 2-3). */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "freecodec/h265_rc.h"

static int g_fail;

static void fail(const char *what)
{
    fprintf(stderr, "FAIL: %s\n", what);
    g_fail = 1;
}

static void checkf(int cond, const char *fmt, ...)
{
    if (!cond) {
        va_list ap;
        char buf[256];

        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        fail(buf);
    }
}

static void test_ratio_tables(void)
{
    /* 10 section 2 item 5, then +2. */
    checkf(freecodec_h265_ratio_i(0.0005, 0) == 27, "bpp<=0.001 -> 25+2");
    checkf(freecodec_h265_ratio_i(0.001, 0) == 27, "bpp==0.001 -> 25+2");
    checkf(freecodec_h265_ratio_i(0.005, 0) == 17, "bpp<=0.01 -> 15+2");
    checkf(freecodec_h265_ratio_i(0.03, 0) == 9, "bpp<=0.05 -> 7+2");
    checkf(freecodec_h265_ratio_i(0.07, 0) == 7, "bpp<=0.1 -> 5+2");
    checkf(freecodec_h265_ratio_i(0.15, 0) == 6, "bpp<=0.2 -> 4+2");
    checkf(freecodec_h265_ratio_i(0.5, 0) == 5, "bpp>0.2 -> 3+2");

    /* The pre-clamp to 2 before the +2 (10 section 2 item 5). */
    checkf(freecodec_h265_ratio_i(0.0005, 1) == 4, "force-two then +2");
    checkf(freecodec_h265_ratio_i(0.5, 1) == 4, "force-two then +2 (high bpp)");

    /* 10 section 2 item 6. */
    {
        static const unsigned int want[10] = { 1, 1, 1, 1, 4, 1, 1, 1, 1, 4 };
        int i;

        for (i = 0; i < 10; i++)
            checkf(freecodec_h265_ratio_p((unsigned int)i) == want[i],
                   "ratio_p[%d]", i);
    }
}

static void test_lambda(void)
{
    checkf(FC_H265_LAMBDA_I == 0x18a00u, "lambda I constant");
    checkf(FC_H265_LAMBDA_P == 0x31400u, "lambda P constant");

    /* alpha * 1^beta = alpha. */
    checkf(fabs(freecodec_h265_lambda_from_bits(1.0, 1.0) - FC_H265_RC_ALPHA) < 1e-9,
           "lambda(1,1) = alpha");

    /* qp_est = round(13.7122 + 4.2005 * ln(lambda)). */
    checkf(freecodec_h265_qp_from_lambda(1.0) == 14, "qp_from_lambda(1) = 14");
    checkf(freecodec_h265_qp_from_lambda(exp(1.0)) == 18, "qp_from_lambda(e) = 18");
    checkf(freecodec_h265_qp_from_lambda(0.0) == 3, "qp_from_lambda(0) floors at 3");
}

static void test_state(void)
{
    freecodec_h265_rc rc;

    freecodec_h265_rc_configure(&rc, 2000000.0, 20, 1280, 720, 10, 51, 20, 40);

    freecodec_h265_rc_start_picture(&rc, 0, 1);
    checkf(rc.cur_qp >= 10 && rc.cur_qp <= 51, "I QP in range");
    checkf(rc.cur_budget > 0, "I budget positive");
    freecodec_h265_rc_finish_picture(&rc, 30000);

    freecodec_h265_rc_start_picture(&rc, 1, 0);
    checkf(rc.cur_qp >= 10 && rc.cur_qp <= 51, "P QP in range");
    freecodec_h265_rc_finish_picture(&rc, 25000);

    freecodec_h265_rc_start_picture(&rc, 2, 0);
    checkf(rc.cur_qp >= rc.prev_p_qp - 5 && rc.cur_qp <= rc.prev_p_qp + 5,
           "P QP within the +/-5 step");
}


/* Plant: a quiet scene, bits per P frame ~ 260 kbit * 0.88^(qp-18); an I frame
 * costs 8x. Drives the RC for `frames` pictures and returns mean Mbps and the
 * last P QP. */
static double run_plant(int track, int frames, int *last_qp)
{
    freecodec_h265_rc rc;
    double total = 0.0;
    int i, qp = 0;

    freecodec_h265_rc_configure(&rc, 2000000.0, 20u, 2304u, 1296u, 18, 45, 20u, 100u);
    freecodec_h265_rc_set_tracking(&rc, track);
    for (i = 0; i < frames; i++) {
        int is_i = (i % 100) == 0;
        double bits;

        freecodec_h265_rc_start_picture(&rc, (long)i, is_i);
        qp = rc.cur_qp;
        bits = 260000.0 * pow(0.88, (double)(qp - 18)) * (is_i ? 8.0 : 1.0);
        freecodec_h265_rc_finish_picture(&rc, (long long)bits);
        total += bits;
    }
    if (last_qp)
        *last_qp = qp;
    return total / ((double)frames / 20.0) / 1e6;
}

static void test_target_tracking(void)
{
    int qp_open = 0, qp_trk = 0;
    double open = run_plant(0, 1000, &qp_open);
    double trk = run_plant(1, 1000, &qp_trk);

    /* The vendor model collapses to the QP ceiling on a quiet scene... */
    checkf(qp_open == 45, "open-loop P QP pinned at the ceiling, got %d", qp_open);
    checkf(open < 1.0, "open-loop under-spends a 2 Mbps target, got %.2f Mbps", open);
    /* ...the tracking mode reaches the target. */
    checkf(trk > 1.7 && trk < 2.3, "tracking mode within 15%% of 2 Mbps, got %.2f Mbps", trk);
    checkf(qp_trk < 45 && qp_trk >= 18, "tracking QP inside the window, got %d", qp_trk);
}

int main(void)
{
    test_ratio_tables();
    test_target_tracking();
    test_lambda();
    test_state();

    if (g_fail) {
        fprintf(stderr, "test_rc: FAILED\n");
        return 1;
    }
    printf("test_rc: ok\n");
    return 0;
}
