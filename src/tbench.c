/* tbench: cost, resolution and monotonicity of every way to read the time. */
#define _GNU_SOURCE
#include "fasttime.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

#define BATCH 1000

static ft_clock C;
static volatile uint64_t sink;

static uint64_t m_mono(void) { return ft__ns(CLOCK_MONOTONIC); }
static uint64_t m_real(void) { return ft__ns(CLOCK_REALTIME); }
static uint64_t m_raw(void) { return ft__ns(CLOCK_MONOTONIC_RAW); }
static uint64_t m_coarse(void) { return ft__ns(CLOCK_MONOTONIC_COARSE); }
static uint64_t m_tod(void) {
    struct timeval v;
    gettimeofday(&v, NULL);
    return (uint64_t)v.tv_sec * 1000000000u + (uint64_t)v.tv_usec * 1000u;
}
static uint64_t m_time(void) { return (uint64_t)time(NULL) * 1000000000u; }
static uint64_t m_sys(void) {
    struct timespec t;
    syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
static uint64_t m_rdtsc(void) { return __rdtsc(); }
static uint64_t m_rdtscp(void) {
    unsigned a;
    return __rdtscp(&a);
}
static uint64_t m_ft(void) { return ft_now_ns(&C); }

/* One loop per method so each call is inlined, not made through a pointer. */
#define LOOP(f)                                                                                    \
    static void loop_##f(void) {                                                                   \
        uint64_t s = 0;                                                                            \
        for (int i = 0; i < BATCH; i++)                                                            \
            s += f();                                                                              \
        sink = s;                                                                                  \
    }
LOOP(m_mono)
LOOP(m_real)
LOOP(m_raw)
LOOP(m_coarse)
LOOP(m_tod)
LOOP(m_time)
LOOP(m_sys)
LOOP(m_rdtsc)
LOOP(m_rdtscp)
LOOP(ft_ticks)
LOOP(m_ft)

typedef struct {
    const char *id, *name;
    uint64_t (*now)(void);
    void (*loop)(void);
    int tsc; /* now() returns ticks, not ns */
} method;

#define M(id, name, f, tsc)                                                                        \
    { id, name, f, loop_##f, tsc }
static const method methods[] = {
    M("monotonic", "clock_gettime(MONOTONIC)", m_mono, 0),
    M("realtime", "clock_gettime(REALTIME)", m_real, 0),
    M("monotonic_raw", "clock_gettime(MONOTONIC_RAW)", m_raw, 0),
    M("monotonic_coarse", "clock_gettime(MONOTONIC_COARSE)", m_coarse, 0),
    M("gettimeofday", "gettimeofday", m_tod, 0),
    M("time", "time()", m_time, 0),
    M("syscall", "syscall(SYS_clock_gettime)", m_sys, 0),
    M("rdtsc", "rdtsc", m_rdtsc, 1),
    M("rdtscp", "rdtscp", m_rdtscp, 1),
    M("lfence_rdtsc", "lfence; rdtsc", ft_ticks, 1),
    M("ft_now_ns", "ft_now_ns()", m_ft, 0),
};

static uint64_t read_ns(const method *m) { return m->tsc ? ft_to_ns(&C, m->now()) : m->now(); }

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* p50 and p99 ns per call over batches of BATCH calls, timed with the TSC. */
static void cost(const method *m, double *v, int batches, double *p50, double *p99) {
    m->loop(); /* warm up */
    for (int b = 0; b < batches; b++) {
        uint64_t t0 = ft_ticks();
        m->loop();
        v[b] = (double)(ft_ticks_end() - t0) * 1e9 / (double)C.hz / BATCH;
    }
    qsort(v, batches, sizeof *v, cmp);
    *p50 = v[batches / 2];
    *p99 = v[batches * 99 / 100];
}

/* Smallest non-zero step between consecutive reads within `ms`; 0 if none was seen. */
static uint64_t resolution(const method *m, int ms) {
    uint64_t best = 0, prev = read_ns(m), end = ft__ns(CLOCK_MONOTONIC) + ms * 1000000ull;
    for (long i = 1;; i++) {
        uint64_t t = read_ns(m);
        if (t > prev && (!best || t - prev < best))
            best = t - prev;
        prev = t;
        if (!(i & 1023) && ft__ns(CLOCK_MONOTONIC) > end)
            return best;
    }
}

/* Two threads on different cores hand a TSC reading back and forth; count reads that went
 * backwards. Catches TSCs that are not synchronised across cores. */
static _Atomic uint64_t last;
static atomic_int turn;
static long backward;
static int rounds, cpu[2];

static void *pingpong(void *arg) {
    int me = (int)(intptr_t)arg;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu[me], &s);
    pthread_setaffinity_np(pthread_self(), sizeof s, &s);
    for (int i = 0; i < rounds; i++) {
        while (atomic_load(&turn) != me)
            ;
        uint64_t t = ft_ticks();
        if (t < atomic_load(&last))
            backward++;
        atomic_store(&last, t);
        atomic_store(&turn, 1 - me);
    }
    return NULL;
}

/* -1 if fewer than two CPUs are available. */
static long monotonicity(int n) {
    cpu_set_t s;
    int k = 0;
    sched_getaffinity(0, sizeof s, &s);
    for (int i = 0; i < CPU_SETSIZE && k < 2; i++)
        if (CPU_ISSET(i, &s))
            cpu[k++] = i;
    if (k < 2)
        return -1;
    rounds = n;
    pthread_t t[2];
    for (intptr_t i = 0; i < 2; i++)
        pthread_create(&t[i], NULL, pingpong, (void *)i);
    pthread_join(t[0], NULL);
    pthread_join(t[1], NULL);
    return backward;
}

static void sleep_until(uint64_t start_ns, long secs) {
    struct timespec t = {(time_t)(start_ns / 1000000000u + secs), (long)(start_ns % 1000000000u)};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
}

/* Error of ft_now_ns() against CLOCK_MONOTONIC since calibration, printed at checkpoints up to
 * `secs`, calling ft_recalibrate every `recal` seconds if non-zero. */
static void long_drift(long secs, long recal) {
    static const long at[] = {1, 10, 60, 600, 1800, 3600, 6 * 3600, 24 * 3600};
    uint64_t start = C.mono0;
    long next = recal;
    printf("TSC %.6f GHz, error of ft_now_ns() vs CLOCK_MONOTONIC since calibration", C.hz / 1e9);
    if (recal)
        printf(", recalibrating every %ld s", recal);
    printf("\n\n%10s %14s %10s\n", "after s", "error us", "ppm");
    for (unsigned i = 0; i <= sizeof at / sizeof *at; i++) {
        long t = i < sizeof at / sizeof *at && at[i] < secs ? at[i] : secs;
        for (; recal && next < t; next += recal) {
            sleep_until(start, next);
            if (ft_recalibrate(&C))
                printf("%10ld recalibration refused, error too large\n", next);
        }
        sleep_until(start, t);
        uint64_t tsc, mono = ft__pair(CLOCK_MONOTONIC, &tsc);
        double err = (double)(int64_t)(ft_to_ns(&C, tsc) - mono);
        printf("%10ld %14.3f %10.3f\n", t, err / 1e3, err / (double)(mono - start) * 1e6);
        fflush(stdout);
        if (t == secs)
            break;
    }
}

int main(int argc, char **argv) {
    int json = 0, quick = 0;
    long drift_secs = 0, recal_secs = 0;
    static const char usage[] =
        "usage: tbench [--json] [--quick] | tbench --drift SECONDS [--recal SECONDS]\n";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json"))
            json = 1;
        else if (!strcmp(argv[i], "--quick"))
            quick = 1;
        else if (!strcmp(argv[i], "--drift") && i + 1 < argc && (drift_secs = atol(argv[++i])) > 0)
            ;
        else if (!strcmp(argv[i], "--recal") && i + 1 < argc && (recal_secs = atol(argv[++i])) > 0)
            ;
        else {
            fputs(usage, stderr);
            return 2;
        }
    }
    if (recal_secs && !drift_secs) {
        fputs(usage, stderr);
        return 2;
    }
    if (drift_secs) {
        ft_calibrate(&C, 100);
        long_drift(drift_secs, recal_secs);
        return 0;
    }

    char cs[64] = "unknown";
    FILE *f = fopen("/sys/devices/system/clocksource/clocksource0/current_clocksource", "r");
    if (f) {
        if (fscanf(f, "%63s", cs) != 1)
            strcpy(cs, "unknown");
        fclose(f);
    }

    int safe = ft_tsc_safe();
    if (!safe)
        fprintf(stderr, "tbench: TSC unsafe (no constant_tsc/nonstop_tsc); calibrating anyway\n");
    ft_calibrate(&C, 100);
    long back = monotonicity(quick ? 20000 : 200000);
    double drift = ft_drift_ppm(&C, quick ? 200 : 2000);

    cpu_set_t one; /* pin to the current core for the cost runs */
    CPU_ZERO(&one);
    CPU_SET(sched_getcpu(), &one);
    sched_setaffinity(0, sizeof one, &one);

    int batches = quick ? 200 : 2000, n = sizeof methods / sizeof *methods;
    double *v = malloc(batches * sizeof *v);
    if (json)
        printf("{\"clocksource\":\"%s\",\"tsc_hz\":%llu,\"tsc_safe\":%s,\"drift_ppm\":%.3f,"
               "\"backward\":%ld,\"methods\":[\n",
               cs, (unsigned long long)C.hz, safe ? "true" : "false", drift, back);
    else
        printf("clocksource %s, TSC %.3f GHz%s, drift %.3f ppm, backward reads %ld\n\n"
               "%-32s %8s %8s %10s\n",
               cs, C.hz / 1e9, safe ? "" : " (UNSAFE)", drift, back, "method", "p50 ns", "p99 ns",
               "res ns");
    for (int i = 0; i < n; i++) {
        const method *m = &methods[i];
        double p50, p99;
        cost(m, v, batches, &p50, &p99);
        uint64_t res = resolution(m, quick ? 20 : 100);
        if (json)
            printf("{\"id\":\"%s\",\"name\":\"%s\",\"p50\":%.1f,\"p99\":%.1f,\"res\":%llu}%s\n",
                   m->id, m->name, p50, p99, (unsigned long long)res, i < n - 1 ? "," : "");
        else if (res)
            printf("%-32s %8.1f %8.1f %10llu\n", m->name, p50, p99, (unsigned long long)res);
        else
            printf("%-32s %8.1f %8.1f %10s\n", m->name, p50, p99, "-");
    }
    if (json)
        puts("]}");
    free(v);
    return 0;
}
