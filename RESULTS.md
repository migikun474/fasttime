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

## Bare metal laptop, before and after (2026-10-20)

Laptop `ouroboros`, bare metal, same method as above: **p50 / p99 ns per call**. The kernel offers
three clocksources; `switch_clock.sh` ran each and restored `tsc`.

| Method | `tsc` | `hpet` | `acpi_pm` |
| --- | --- | --- | --- |
| `clock_gettime(MONOTONIC)` | 20.3 / 24.4 | 1213.0 / 1379.0 | 1966.2 / 4274.9 |
| `clock_gettime(REALTIME)` | 20.3 / 24.9 | 1157.9 / 1273.0 | 2279.6 / 4369.1 |
| `clock_gettime(MONOTONIC_RAW)` | 20.5 / 26.4 | 1188.5 / 1490.5 | 2238.7 / 3842.4 |
| `clock_gettime(MONOTONIC_COARSE)` | 4.0 / 5.9 | 4.0 / 5.2 | 4.2 / 6.3 |
| `gettimeofday` | 21.9 / 27.7 | 1192.0 / 1500.5 | 2129.0 / 3693.8 |
| `time()` | 2.3 / 2.9 | 2.2 / 2.2 | 2.2 / 2.7 |
| `syscall(SYS_clock_gettime)` | 176.7 / 196.0 | 1164.2 / 1490.9 | 2169.5 / 5538.0 |
| `rdtsc` | 8.2 / 10.5 | 8.1 / 12.2 | 7.9 / 8.5 |
| `rdtscp` | 15.5 / 20.0 | 15.9 / 25.1 | 15.3 / 35.1 |
| `lfence; rdtsc` | 14.0 / 18.1 | 14.4 / 23.6 | 14.0 / 63.2 |
| `ft_now_ns()` | 16.3 / 20.7 | 16.3 / 28.0 | 15.8 / 74.6 |

- **`tsc` is the fast path.** `clock_gettime` costs 20 ns through the vDSO, 9x cheaper than
  entering the kernel (177 ns).
- **`hpet` and `acpi_pm` cannot be read in user space**, so every `clock_gettime` and
  `gettimeofday` becomes a system call plus a slow hardware register read: about 1.2 us under
  `hpet` (60x) and about 2 us under `acpi_pm` (100x), with p99 up to 5.5 us. This is the Netflix
  failure, reproduced on a laptop.
- **`ft_now_ns()` (16 ns) beats the vDSO (20 ns)** and does not care which clocksource the kernel
  picked, because it reads the TSC directly. Raw `rdtsc` is 8 ns here against 10 ns on the VM.
- `COARSE` and `time()` stay at 2 to 4 ns under every clocksource: they return the last timer tick
  and never read the counter.
