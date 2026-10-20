#!/usr/bin/env bash
# render.sh: tbench JSON files -> one markdown table, a column per file, cells p50 / p99 ns.
# usage: scripts/render.sh results/host-*.json
awk '
FNR == 1 { f++; match($0, /"clocksource":"[^"]*"/); col[f] = substr($0, RSTART + 15, RLENGTH - 16) }
/"id":/ {
    match($0, /"name":"[^"]*"/); n = substr($0, RSTART + 8, RLENGTH - 9)
    if (!(n in seen)) { seen[n] = 1; row[++r] = n }
    match($0, /"p50":[0-9.]+/); p50 = substr($0, RSTART + 6, RLENGTH - 6)
    match($0, /"p99":[0-9.]+/); p99 = substr($0, RSTART + 6, RLENGTH - 6)
    cell[n, f] = p50 " / " p99
}
END {
    printf "| Method |"; for (i = 1; i <= f; i++) printf " `%s` |", col[i]; print ""
    printf "| --- |";    for (i = 1; i <= f; i++) printf " --- |"; print ""
    for (j = 1; j <= r; j++) {
        printf "| `%s` |", row[j]; for (i = 1; i <= f; i++) printf " %s |", cell[row[j], i]; print ""
    }
}' "$@"
