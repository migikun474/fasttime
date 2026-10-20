# Results

## Cloud VM, before and after (2026-10-20)

Shared 4-core cloud VM (Firecracker/KVM), Intel Xeon at 2.8 GHz, Linux 6.18. Flags `constant_tsc`,
`nonstop_tsc`, `rdtscp`, `tsc_known_freq`. Pinned to one core, batches of 1,000 calls,
cells are **p50 / p99 ns per call**. Drift of `ft_now_ns()` against `CLOCK_MONOTONIC`: under
0.01 ppm over 2 s. Backward reads across cores: 0.

| Method | `tsc` | `kvm-clock` |
| --- | --- | --- |
| `clock_gettime(MONOTONIC)` | 19.4 / 44.1 | 23.3 / 51.7 |
| `clock_gettime(REALTIME)` | 19.4 / 44.2 | 23.3 / 50.1 |
| `clock_gettime(MONOTONIC_RAW)` | 24.6 / 57.5 | 23.5 / 49.9 |
| `clock_gettime(MONOTONIC_COARSE)` | 4.7 / 11.9 | 4.7 / 9.4 |
| `gettimeofday` | 21.8 / 46.5 | 26.0 / 60.3 |
| `time()` | 2.4 / 3.9 | 2.4 / 5.3 |
| `syscall(SYS_clock_gettime)` | 125.5 / 192.4 | 131.3 / 194.7 |
| `rdtsc` | 10.0 / 27.0 | 10.0 / 29.7 |
| `rdtscp` | 13.8 / 33.0 | 13.8 / 33.1 |
| `lfence; rdtsc` | 14.7 / 38.0 | 14.7 / 36.1 |
| `ft_now_ns()` | 19.0 / 46.8 | 16.8 / 38.3 |

`kvm-clock` is still read through the vDSO, so it costs only a few ns more than `tsc`. The
forced system call row (about 125 ns, 6x a vDSO read) is what a clocksource that cannot be read
in user space (`xen` on old AWS, `hpet`, `acpi_pm`) costs on every read.

## Bare metal

To do: `make && sudo scripts/switch_clock.sh && scripts/render.sh results/$(hostname)-*.json`.
