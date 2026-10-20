#!/usr/bin/env bash
# clockcheck.sh: are time reads on this machine fast, slow or unsafe?
# Exit 0 FAST, 1 SLOW, 2 UNSAFE. Set TBENCH to point at a tbench binary elsewhere.
set -u
cs_dir=/sys/devices/system/clocksource/clocksource0
tbench=${TBENCH:-$(dirname "$0")/../tbench}
slow="" unsafe=""

cur=$(cat "$cs_dir/current_clocksource" 2>/dev/null || echo unknown)
avail=$(cat "$cs_dir/available_clocksource" 2>/dev/null)
echo "clocksource:    $cur (available: ${avail:-unknown})"
case $cur in
tsc | kvm-clock | hyperv_clocksource_tsc_page) ;; # read in user space through the vDSO
*)
    fix="no tsc to switch to"
    [[ " $avail " == *" tsc "* ]] && fix="fix: echo tsc > $cs_dir/current_clocksource, or boot with clocksource=tsc"
    slow="slow clock '$cur', $fix"
    ;;
esac

flags=$(grep -m1 '^flags' /proc/cpuinfo)
for f in constant_tsc nonstop_tsc; do
    if [[ " $flags " == *" $f "* ]]; then
        echo "cpu flag:       $f"
    else
        echo "cpu flag:       $f MISSING"
        unsafe="${unsafe:+$unsafe; }$f missing"
    fi
done

klog=$(dmesg 2>/dev/null || journalctl -k -b --no-pager 2>/dev/null)
if [[ -z $klog ]]; then
    echo "kernel log:     unreadable, skipped (try as root)"
elif line=$(grep -m1 "Marking clocksource 'tsc' as unstable" <<<"$klog"); then
    echo "kernel log:     $line"
    unsafe="${unsafe:+$unsafe; }kernel marked the TSC unstable"
else
    echo "kernel log:     tsc never marked unstable"
fi

if [[ -x $tbench ]]; then
    json=$("$tbench" --quick --json 2>/dev/null)
    p50() { awk -v id="\"id\":\"$1\"" 'index($0, id) { match($0, /"p50":[0-9.]+/); print substr($0, RSTART + 6, RLENGTH - 6) }' <<<"$json"; }
    v=$(p50 monotonic) s=$(p50 syscall)
    echo "vdso:           clock_gettime $v ns, forced syscall $s ns"
    awk -v v="$v" -v s="$s" 'BEGIN { exit !(v < s / 2) }' ||
        slow="${slow:+$slow; }syscall fallback: time reads enter the kernel"
else
    echo "vdso:           skipped, run make to build tbench"
fi

virt=$(systemd-detect-virt 2>/dev/null)
[[ -n $virt ]] || virt=$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null)
echo "virtualisation: ${virt:-unknown} (note only: VM numbers run higher)"

if [[ -n $unsafe ]]; then
    echo "UNSAFE: $unsafe. Do not use fasttime; stay on clock_gettime."
    exit 2
elif [[ -n $slow ]]; then
    echo "SLOW: $slow"
    exit 1
fi
echo FAST
