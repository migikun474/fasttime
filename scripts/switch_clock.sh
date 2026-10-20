#!/usr/bin/env bash
# switch_clock.sh: run tbench under every available clocksource, then restore the original.
# Root only. Never run it in CI. Writes results/<host>-<clocksource>.json.
set -euo pipefail
cs=/sys/devices/system/clocksource/clocksource0
cd "$(dirname "$0")/.."
[[ -x tbench ]] || make tbench
orig=$(cat $cs/current_clocksource)
trap 'echo "$orig" > $cs/current_clocksource && echo "restored $orig"' EXIT
mkdir -p results
read -ra all <$cs/available_clocksource
for c in "${all[@]}"; do
    if ! echo "$c" >$cs/current_clocksource 2>/dev/null || [[ $(cat $cs/current_clocksource) != "$c" ]]; then
        echo "$c: kernel refused, skipped"
        continue
    fi
    sleep 1
    ./tbench --json >"results/$(hostname)-$c.json"
    echo "$c: results/$(hostname)-$c.json"
done
