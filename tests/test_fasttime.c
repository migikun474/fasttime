/* Conversion and calibration math on fake inputs. `make asm` reads to_ns's assembly. */
#include "fasttime.h"
#include <assert.h>

__attribute__((noinline)) uint64_t to_ns(const ft_clock *c, uint64_t tsc) {
    return ft_to_ns(c, tsc);
}

/* |got - want| within ppm parts per million of want, plus 1 ns for rounding. */
static void near(uint64_t got, uint64_t want, double ppm) {
    double err = got > want ? (double)(got - want) : (double)(want - got);
    if (err > 1 + want * ppm / 1e6) {
        fprintf(stderr, "got %llu want %llu\n", (unsigned long long)got, (unsigned long long)want);
        assert(0);
    }
}

int main(void) {
    static const uint64_t hzs[] = {1000000000u, 2100000000u, 3000000000u, 3700000000u};
    for (unsigned i = 0; i < sizeof hzs / sizeof *hzs; i++) {
        uint64_t hz = hzs[i];
        ft_clock c = {0};
        ft_set_rate(&c, hz / 10, 100000000u); /* a 100 ms calibration */
        assert(c.hz == hz);
        c.tsc0 = 12345;
        c.ns0 = 777;
        assert(to_ns(&c, c.tsc0) == c.ns0);
        near(to_ns(&c, c.tsc0 + hz), c.ns0 + 1000000000u, 0.001);             /* 1 s */
        near(to_ns(&c, c.tsc0 + hz * 86400), c.ns0 + 86400000000000u, 0.001); /* 1 day */
        uint64_t ten_years = 10ull * 365 * 86400;                             /* no overflow */
        near(to_ns(&c, c.tsc0 + hz * ten_years), c.ns0 + ten_years * 1000000000u, 0.001);
    }

    /* A real-looking calibration: 2.1 GHz with 30 ns of bracket noise. */
    ft_clock c = {0};
    ft_set_rate(&c, 210000063u, 100000000u);
    near(c.hz, 2100000000u, 1);

    puts("ok");
    return 0;
}
