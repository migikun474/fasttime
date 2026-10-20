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

## Drift of one calibration over an hour, bare metal laptop (2026-10-20)

`./tbench --drift 3600` on `ouroboros` (TSC 3.792875 GHz): error of `ft_now_ns()` against
`CLOCK_MONOTONIC` since a single 100 ms calibration.

| After | Error | Average since calibration | Rate in this interval |
| --- | --- | --- | --- |
| 1 s | -30.0 us | -30.0 ppm | -30 ppm |
| 10 s | -300.1 us | -30.0 ppm | -30 ppm |
| 1 min | -1.80 ms | -30.0 ppm | -30 ppm |
| 10 min | +22.0 ms | +36.7 ppm | +44 ppm |
| 30 min | +47.1 ms | +26.2 ppm | +21 ppm |
| 1 h | +74.8 ms | +20.8 ppm | +15 ppm |

- **The rate changes, so the moving clock is most likely `CLOCK_MONOTONIC`, not the TSC.** A TSC
  with `constant_tsc` ticks at a fixed rate. `ft_init` calibrates against `CLOCK_MONOTONIC_RAW`,
  which NTP never touches, but `CLOCK_MONOTONIC` is disciplined by NTP (systemd-timesyncd here).
- **NTP was slewing an offset, not correcting the crystal.** `timedatectl timesync-status` after
  the run: frequency correction -0.779 ppm, but offset -32.9 ms, delay 407 ms, jitter 45 ms. The
  kernel removes an offset by running `CLOCK_MONOTONIC` faster or slower by tens of ppm for a
  while; with a slow, noisy path to the NTP server the measured offset jumps, so the slew keeps
  changing size and direction: -30, +44, +21, +15 ppm.
- **Short intervals are unaffected:** 30 ppm is 30 ns per millisecond measured.
- **Long-run timestamps are:** after one hour on one calibration, `ft_now_ns()` was 75 ms away
  from `CLOCK_MONOTONIC`. Programs that compare `ft_now_ns()` with other clocks over minutes or
  hours should call `ft_recalibrate` every few minutes; `tbench --drift 3600 --recal 60` shows
  the result.
- On the cloud VM the same test drifted 0.01 ppm over 12 s: its clock was not being slewed during the run.

## Drift with `ft_recalibrate` every 60 s, same laptop (2026-10-20)

`./tbench --drift 3600 --recal 60` on `ouroboros` (TSC 3.792876 GHz).

| After | Error | Without recalibration (run above) |
| --- | --- | --- |
| 1 s | -68.3 us | -30.0 us |
| 10 s | -679.2 us | -300.1 us |
| 1 min | -3.94 ms | -1.80 ms |
| 10 min | +115.0 us | +22.0 ms |
| 30 min | +10.9 us | +47.1 ms |
| 1 h | +2.12 ms | +74.8 ms |

- **Until the first recalibration at 60 s nothing changes:** NTP was slewing `CLOCK_MONOTONIC` by
  about 68 ppm at the start of this run, against 30 ppm in the run above.
- **After that the error stops accumulating:** 115 us at 10 min, 11 us at 30 min and 2.1 ms at
  1 h, against 22, 47 and 75 ms on one calibration.
- **What remains is NTP changing its slew between two recalibrations.** 2.1 ms within one 60 s
  interval means the slew changed by about 35 ppm in that minute. The error is bounded by the
  slew change times the interval, so recalibrating every 10 s would cut it about six-fold.
- Each checkpoint falls at the end of a 60 s interval, just before a recalibration, where the
  error is largest.
