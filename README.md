# fasttime

Tells you whether a Linux machine's time reads are fast or secretly slow, measures every way to
read the time, and gives you a cheaper clock straight from the CPU's timestamp counter.

**Why.** On a healthy machine `clock_gettime` never enters the kernel: the vDSO reads the counter
in user space in about 20 ns. With the wrong clocksource every read becomes a system call costing
hundreds of ns, and nothing in your program changes to show it. Netflix hit this on older AWS
instances, where the `xen` clocksource made time reads 32% of CPU in one service. Switching to
`tsc` cut each read from about 680 ns to 33 ns and write latency by 43%.

## Use

```sh
make                       # builds tbench and the tests
scripts/clockcheck.sh      # FAST (exit 0), SLOW (1, with the fix) or UNSAFE (2, with the reason)
./tbench                   # p50/p99 cost and resolution of every time source; --json, --quick
./tbench --drift 3600       # how far ft_now_ns() wanders from CLOCK_MONOTONIC over an hour
make check                 # unit tests, plus the assembly check that ft_to_ns has no div or call
sudo scripts/switch_clock.sh && scripts/render.sh results/$(hostname)-*.json   # before/after
```

```c
#include "fasttime.h"

ft_clock c;
if (ft_init(&c, 100) != 0)        // -1: no constant_tsc/nonstop_tsc, stay on clock_gettime
    ...;
uint64_t t = ft_now_ns(&c);       // ns, comparable to CLOCK_MONOTONIC
double ppm = ft_drift_ppm(&c, 1000);
```

`ft_now_ns` is monotonic time, not wall time. A 100 ms calibration usually lands within a few ppm;
long-running programs should call `ft_init` again every few minutes. Run `clockcheck.sh` first:
a fast clock that is quietly wrong is worse than a slow one.

## Results

See [RESULTS.md](RESULTS.md). On a bare-metal laptop (p50 per call):

| Clocksource | `clock_gettime(MONOTONIC)` | `ft_now_ns()` |
| --- | --- | --- |
| `tsc` | 20 ns | 16 ns |
| `hpet` | 1,213 ns | 16 ns |
| `acpi_pm` | 1,966 ns | 16 ns |

x86-64 Linux only. Not covered: clock sync across machines (PTP, NTP), NIC timestamps.
