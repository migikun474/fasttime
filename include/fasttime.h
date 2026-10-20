/* fasttime.h: a calibrated TSC clock for x86-64 Linux. Header-only.
 *
 *   ft_clock c;
 *   if (ft_init(&c, 100) == 0) t = ft_now_ns(&c);  // comparable to CLOCK_MONOTONIC
 *   ft_recalibrate(&c);                             // every few minutes, to follow NTP
 */
#ifndef FASTTIME_H
#define FASTTIME_H

#ifndef __x86_64__
#error "fasttime.h needs x86-64"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <x86intrin.h>

typedef struct {
    uint64_t tsc0, ns0; /* TSC and ft_now_ns() at the base point */
    uint64_t mult;      /* ns = ns0 + (((tsc - tsc0) * mult) >> 32) */
    uint64_t hz;        /* measured TSC frequency */
    uint64_t mono0;     /* CLOCK_MONOTONIC at the base point */
} ft_clock;

/* lfence; rdtsc: waits for earlier instructions, for the start of a timed region. */
static inline uint64_t ft_ticks(void) {
    _mm_lfence();
    return __rdtsc();
}

/* rdtscp; lfence: for the end of a timed region. */
static inline uint64_t ft_ticks_end(void) {
    unsigned aux;
    uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}

/* One 64x64->128 mul and a shift: no division, cannot overflow for centuries. */
static inline uint64_t ft_to_ns(const ft_clock *c, uint64_t tsc) {
    return c->ns0 + (uint64_t)(((unsigned __int128)(tsc - c->tsc0) * c->mult) >> 32);
}

static inline uint64_t ft_now_ns(const ft_clock *c) { return ft_to_ns(c, ft_ticks()); }

static inline uint64_t ft__ns(clockid_t id) {
    struct timespec t;
    clock_gettime(id, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

static inline void ft__sleep_ms(int ms) {
    struct timespec d = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&d, NULL);
}

/* Read clock `id` and the TSC at nearly the same instant: best of 10 brackets. */
static inline uint64_t ft__pair(clockid_t id, uint64_t *tsc) {
    uint64_t best = UINT64_MAX, ns = 0;
    *tsc = 0;
    for (int i = 0; i < 10; i++) {
        uint64_t a = ft__ns(id), t = ft_ticks(), b = ft__ns(id);
        if (b - a < best) {
            best = b - a;
            ns = a + (b - a) / 2;
            *tsc = t;
        }
    }
    return ns;
}

/* Rate from dtsc ticks over dns nanoseconds. Pure, so tests can feed it fake inputs. */
static inline void ft_set_rate(ft_clock *c, uint64_t dtsc, uint64_t dns) {
    c->hz = (uint64_t)((unsigned __int128)dtsc * 1000000000u / dns);
    c->mult = (uint64_t)(((unsigned __int128)dns << 32) / dtsc);
}

/* 1 if the CPU reports constant_tsc and nonstop_tsc. */
static inline int ft_tsc_safe(void) {
    char line[16384];
    int ok = 0;
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "flags", 5)) {
            ok = strstr(line, " constant_tsc") && strstr(line, " nonstop_tsc");
            break;
        }
    fclose(f);
    return ok;
}

/* Measure the rate against CLOCK_MONOTONIC_RAW, then base on CLOCK_MONOTONIC.
 * Does not check the TSC is safe: use ft_init unless you only need a ruler. */
static inline void ft_calibrate(ft_clock *c, int calib_ms) {
    uint64_t t1, t2, n1 = ft__pair(CLOCK_MONOTONIC_RAW, &t1);
    ft__sleep_ms(calib_ms);
    uint64_t n2 = ft__pair(CLOCK_MONOTONIC_RAW, &t2);
    ft_set_rate(c, t2 - t1, n2 - n1);
    c->ns0 = c->mono0 = ft__pair(CLOCK_MONOTONIC, &c->tsc0);
}

/* -1 if the TSC is unsafe; calib_ms <= 0 means 100 ms. */
static inline int ft_init(ft_clock *c, int calib_ms) {
    if (!ft_tsc_safe())
        return -1;
    ft_calibrate(c, calib_ms > 0 ? calib_ms : 100);
    return 0;
}

/* Steer toward CLOCK_MONOTONIC, which read `mono` at TSC `tsc`, without a jump: ft_now_ns()
 * stays continuous, and the new rate removes the current error over as long again as since the
 * last base. -1, changing nothing, if under 1 ms since the last base or the error is over half
 * that time. Pure, so tests can feed it fake inputs. */
static inline int ft_steer(ft_clock *c, uint64_t tsc, uint64_t mono) {
    uint64_t dmono = mono - c->mono0, now = ft_to_ns(c, tsc);
    int64_t err = (int64_t)(now - mono);
    if (dmono < 1000000 || err > (int64_t)dmono / 2 || -err > (int64_t)dmono / 2)
        return -1;
    c->mult = (uint64_t)(((unsigned __int128)((int64_t)dmono - err) << 32) / (tsc - c->tsc0));
    c->tsc0 = tsc;
    c->ns0 = now;
    c->mono0 = mono;
    return 0;
}

/* Call every few minutes to keep ft_now_ns() within a few ppm of CLOCK_MONOTONIC while NTP
 * adjusts it. -1 if called too soon, or the error is too big to steer out (after a suspend, say):
 * call ft_init again, accepting a jump.
 * ponytail: not safe while other threads read c; recalibrate a copy and swap a pointer if so. */
static inline int ft_recalibrate(ft_clock *c) {
    uint64_t tsc, mono = ft__pair(CLOCK_MONOTONIC, &tsc);
    return ft_steer(c, tsc, mono);
}

/* Rate error of ft_now_ns() against CLOCK_MONOTONIC over `ms`, in parts per million.
 * ponytail: no background recalibration thread; callers run ft_recalibrate on their own timer. */
static inline double ft_drift_ppm(const ft_clock *c, int ms) {
    uint64_t t1, t2, m1 = ft__pair(CLOCK_MONOTONIC, &t1);
    ft__sleep_ms(ms);
    uint64_t m2 = ft__pair(CLOCK_MONOTONIC, &t2);
    double ft = (double)(ft_to_ns(c, t2) - ft_to_ns(c, t1)), mono = (double)(m2 - m1);
    return (ft - mono) / mono * 1e6;
}

#endif
