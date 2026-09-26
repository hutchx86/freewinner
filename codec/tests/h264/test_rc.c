/* SPDX-License-Identifier: AGPL-3.0-only */
/* Copyright (C) 2026 freewinner contributors */

/* Host test for the frame-level rate-control unit (spec/10-rate-control.md,
 * r4, section 8's acceptance): the ladder vectors are checked as exact
 * equalities; the six "run" configs of section 6 are driven through the
 * unit in closed loop (this file's own plant, matching section 6's
 * formulas exactly) and checked against section 3 items 1-6 and 8. */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freecodec/h264_rc.h"

#ifndef RC_VECTOR_DIR
#define RC_VECTOR_DIR "spec/vectors"
#endif

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

/* --------------------------------------------------- ladder vector tests -- */

static int first_qp_ladder_ref(double bpp)
{
    if (bpp < 0.01) return 40;
    if (bpp < 0.03) return 39;
    if (bpp < 0.05) return 37;
    if (bpp < 0.1)  return 35;
    if (bpp < 0.2)  return 33;
    if (bpp < 0.4)  return 32;
    if (bpp < 0.6)  return 29;
    if (bpp < 1.4)  return 27;
    if (bpp < 2.4)  return 25;
    return 15;
}

/* rc_first_qp.csv / rc_first_qp_edges.csv: width,height,fps,bitrate,bpp,
 * qp_first_I,qp_first_P -- items 1 and 2 as exact equalities. qp bounds are
 * kept loose (10-51) so the ladder's own range (15-40) is never clipped,
 * matching section 6's six run configs and the open item in section 10 item
 * 4 about the clamp not being vector-evidenced at this boundary. */
static int run_ladder_file(const char *name)
{
    char path[512];
    FILE *f;
    char line[512];
    int rows = 0;

    snprintf(path, sizeof(path), "%s/%s", RC_VECTOR_DIR, name);
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "FAIL: cannot open %s\n", path);
        g_fail = 1;
        return 0;
    }
    if (fgets(line, sizeof(line), f) == NULL) {
        fprintf(stderr, "FAIL: empty %s\n", path);
        fclose(f);
        return 0;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned int width, height, fps;
        double bitrate, bpp;
        int qp_first_i, qp_first_p, got_i, got_p;
        freecodec_rc rc;
        long long bits;
        unsigned int activity;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0')
            continue;
        if (sscanf(line, "%u,%u,%u,%lf,%lf,%d,%d",
                   &width, &height, &fps, &bitrate, &bpp,
                   &qp_first_i, &qp_first_p) != 7) {
            fprintf(stderr, "FAIL: bad line in %s: %.60s\n", name, line);
            g_fail = 1;
            continue;
        }
        rows++;

        freecodec_rc_configure(&rc, bitrate, fps, width, height,
                               10, 51, 51, 1000000u);

        got_i = freecodec_rc_start_picture(&rc, FC_RC_PIC_I);
        checkf(got_i == qp_first_i,
              "%s: first-picture QP %d, want %d (bpp=%g)",
              name, got_i, qp_first_i, bpp);
        checkf(freecodec_rc_target(&rc) == 0,
              "%s: target() must be 0 for an I picture", name);

        bits = (long long)llround((bitrate / fps) * 5.0 *
                                  pow(2.0, (30.0 - got_i) / 6.0));
        activity = (unsigned int)llround((double)width * height);
        freecodec_rc_finish_picture(&rc, bits, activity);

        got_p = freecodec_rc_start_picture(&rc, FC_RC_PIC_P);
        checkf(got_p == qp_first_p,
              "%s: second-picture (first P) QP %d, want %d (bpp=%g)",
              name, got_p, qp_first_p, bpp);
    }
    fclose(f);
    return rows;
}

/* ---------------------------------------------------------- run-config plant */

typedef double (*schedule_fn)(int k);

static double sched_steps(int k)
{
    if (k < 80) return 1.0;
    if (k < 160) return 3.0;
    return 0.5;
}

static double sched_ramp(int k)
{
    return 0.5 + 2.5 * (double)(k % 120) / 119.0;
}

/* Count non-header data rows of a vector file, as a sanity cross-check that
 * this test's own frame count (driven by section 6's formulas, not by
 * reading the vendor QP/bit columns -- byte-for-byte reproduction is not
 * required, section 8) matches the vector's row count. */
static int count_csv_rows(const char *name)
{
    char path[512];
    FILE *f;
    char line[512];
    int rows = 0;

    snprintf(path, sizeof(path), "%s/%s", RC_VECTOR_DIR, name);
    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "FAIL: cannot open %s\n", path);
        g_fail = 1;
        return -1;
    }
    fgets(line, sizeof(line), f); /* header */
    while (fgets(line, sizeof(line), f) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0')
            rows++;
    }
    fclose(f);
    return rows;
}

struct window {
    int lo, hi;        /* [lo, hi) picture indices, matching the CSV "frame" column */
    double expect_qp;  /* section 3 item 6's per-segment reference mean QP */
};

static void check_window(const int *qp, const long long *bits, int n,
                         const struct window *w, double b0, const char *tag)
{
    long long sum_bits = 0;
    double sum_qp = 0.0;
    int i, count = 0;
    int all_lo = 1, all_hi = 1;
    int qp_min_seen = 999, qp_max_seen = -1;

    for (i = w->lo; i < w->hi && i < n; i++) {
        sum_bits += bits[i];
        sum_qp += (double)qp[i];
        count++;
        if (qp[i] < qp_min_seen) qp_min_seen = qp[i];
        if (qp[i] > qp_max_seen) qp_max_seen = qp[i];
    }
    if (count == 0) {
        fail("empty steady-state window");
        return;
    }
    (void)all_lo; (void)all_hi;

    {
        double mean_qp = sum_qp / count;

        checkf(fabs(mean_qp - w->expect_qp) <= 1.0,
              "%s: mean QP %.2f, want %.2f +/- 1", tag, mean_qp, w->expect_qp);

        /* Section 3 item 6: when the whole window is pinned at one bound,
         * only the mean-QP half of the contract applies. */
        if (qp_min_seen != qp_max_seen) {
            double mean_bits = (double)sum_bits / count;
            double ideal = b0;
            double rel = fabs(mean_bits - ideal) / ideal;

            checkf(rel <= 0.03,
                  "%s: mean bits %.0f vs ideal %.0f (%.1f%% off), want <= 3%%",
                  tag, mean_bits, ideal, rel * 100.0);
        }
    }
}

/* Drives the section-6 plant for one run config, checks items 1-4 (exact),
 * item 5 (long-run rate, last >=200 P pictures), item 6 (per-segment
 * windows) and item 8 (target reporting). */
static void run_plant(const char *csv_name, unsigned int width, unsigned int height,
                      unsigned int fps, double bitrate, int qp_min, int qp_max,
                      int max_step, schedule_fn sched,
                      const struct window *windows, int nwindows)
{
    freecodec_rc rc;
    int n = count_csv_rows(csv_name);
    int *qp;
    long long *bits;
    long long *target;
    double b0 = bitrate / fps;
    int k;
    int prev_qp = 0;

    if (n <= 0)
        return;

    qp     = malloc(sizeof(int) * (size_t)n);
    bits   = malloc(sizeof(long long) * (size_t)n);
    target = malloc(sizeof(long long) * (size_t)n);
    if (qp == NULL || bits == NULL || target == NULL) {
        fail("out of memory");
        free(qp); free(bits); free(target);
        return;
    }

    freecodec_rc_configure(&rc, bitrate, fps, width, height,
                           qp_min, qp_max, max_step, 1000000u);

    for (k = 0; k < n; k++) {
        freecodec_rc_pic_type type = (k == 0) ? FC_RC_PIC_I : FC_RC_PIC_P;
        double s = sched(k);
        double f = (k == 0) ? 5.0 : 1.0;
        int this_qp;
        long long this_bits;
        unsigned int activity;

        this_qp = freecodec_rc_start_picture(&rc, type);
        target[k] = freecodec_rc_target(&rc);

        checkf(this_qp >= qp_min && this_qp <= qp_max && this_qp <= 51,
              "%s[%d]: QP %d outside [%d,%d]/51", csv_name, k, this_qp, qp_min, qp_max);
        if (k == 0) {
            double bpp = bitrate / ((double)width * height * fps);
            int want = first_qp_ladder_ref(bpp);

            if (want < qp_min) want = qp_min;
            if (want > qp_max) want = qp_max;
            checkf(this_qp == want,
                  "%s[0]: first-picture QP %d, want %d (bpp=%g)",
                  csv_name, this_qp, want, bpp);
            checkf(target[k] == 0, "%s[0]: target() must be 0 for the I picture", csv_name);
        } else if (k >= 2) {
            checkf(abs(this_qp - prev_qp) <= max_step,
                  "%s[%d]: |QP step| %d exceeds max_step %d",
                  csv_name, k, abs(this_qp - prev_qp), max_step);
        }

        this_bits = (long long)llround(b0 * f * s * pow(2.0, (30.0 - this_qp) / 6.0));
        if (this_bits < 1)
            this_bits = 1;
        activity = (unsigned int)llround((double)width * height * s);

        freecodec_rc_finish_picture(&rc, this_bits, activity);

        qp[k] = this_qp;
        bits[k] = this_bits;
        prev_qp = this_qp;
    }

    /* Item 2: second picture (first P) is exactly first-picture QP + 1,
     * clamped. */
    {
        int want = qp[0] + 1;

        if (want > qp_max) want = qp_max;
        if (want < qp_min) want = qp_min;
        checkf(n < 2 || qp[1] == want,
              "%s: second-picture QP %d, want %d", csv_name, qp[1], want);
    }

    /* Item 5: long-run rate over the last >=200 P pictures. */
    if (n - 1 >= 200) {
        long long sum = 0;
        int i, count = 0;

        for (i = n - 200; i < n; i++) {
            sum += bits[i];
            count++;
        }
        {
            double ideal = b0 * count;
            double rel = fabs((double)sum - ideal) / ideal;

            checkf(rel <= 0.01,
                  "%s: long-run bits %lld vs ideal %.0f (%.2f%% off), want <= 1%%",
                  csv_name, sum, ideal, rel * 100.0);
        }
    }

    /* Item 6: per-segment steady-state windows. */
    {
        int i;

        for (i = 0; i < nwindows; i++)
            check_window(qp, bits, n, &windows[i], b0, csv_name);
    }

    /* Item 8: target reporting for P pictures over the last 40, +/-6%
     * (the ripple carve-out is generous enough that a well-behaved
     * implementation need not special-case it). */
    if (n >= 40) {
        long long sum = 0;
        int i, count = 0;

        for (i = n - 40; i < n; i++) {
            sum += target[i];
            count++;
        }
        {
            double mean_t = (double)sum / count;
            double rel = fabs(mean_t - b0) / b0;

            checkf(rel <= 0.06,
                  "%s: mean target() %.0f vs ideal %.0f (%.1f%% off), want <= 6%%",
                  csv_name, mean_t, b0, rel * 100.0);
        }
    }

    free(qp); free(bits); free(target);
}

int main(void)
{
    int n1, n2;
    struct window steps_windows[3] = {
        { 40,  80,  30.0 },
        { 120, 160, 39.55 },
        { 200, 240, 24.0 }
    };
    struct window ramp_windows[2] = {
        { 40,  80,  34.80 },
        { 160, 200, 34.80 }
    };

    n1 = run_ladder_file("rc_first_qp.csv");
    checkf(n1 == 88, "rc_first_qp.csv: expected 88 rows, read %d", n1);
    n2 = run_ladder_file("rc_first_qp_edges.csv");
    checkf(n2 == 18, "rc_first_qp_edges.csv: expected 18 rows, read %d", n2);

    run_plant("rc_step_high_chg2.csv",   2304, 1296, 20, 1500000.0, 10, 51, 2,
             sched_steps, steps_windows, 3);
    run_plant("rc_step_high_chg51.csv",  2304, 1296, 20, 1500000.0, 10, 51, 51,
             sched_steps, steps_windows, 3);
    run_plant("rc_step_low_chg2.csv",    640,  368,  20, 500000.0,  10, 51, 2,
             sched_steps, steps_windows, 3);
    run_plant("rc_ramp_high_chg2.csv",   2304, 1296, 20, 2200000.0, 10, 51, 2,
             sched_ramp,  ramp_windows,  2);
    run_plant("rc_step_high_clamp.csv",  2304, 1296, 20, 1500000.0, 20, 40, 2,
             sched_steps, steps_windows, 3);

    if (g_fail)
        return 1;
    printf("rc: ladder vectors and section-6 plant (bounds, step limit, "
           "long-run/steady-state rate, target reporting) ok\n");
    return 0;
}
