#!/bin/bash
# Usage: [KOH2_AA=TAA] dev/bench.sh LABEL [KEY=value ...]
# Loads the save from dev/local.env, runs the built-in Europe benchmark, samples GPU busy %, quits.
set -uo pipefail
. "$(dirname "$(readlink -f "$0")")/lib.sh"
label=${1:?label}; shift
gpu_busy=$(ls /sys/class/drm/card*/device/gpu_busy_percent | head -n 1)

launch_and_load "$@" || { finish; exit 1; }
sleep 5
samples=$(mktemp)
( while :; do cat "$gpu_busy" >> "$samples"; sleep 0.1; done ) &
sampler=$!
send bench
wait_log 'bench done' 600; ok=$?
kill $sampler
result=$(grep -oE 'bench done: .*' "$log" | tail -n 1)
gpu=$(awk '{s+=$1; n++} END {if (n) printf "%.0f", s/n}' "$samples")
rm -f "$samples"
finish
[ $ok -eq 0 ] || exit 1
line="$(date -u +%FT%TZ) $label | aa ${KOH2_AA:-None} | $result | gpu busy avg ${gpu}% | env: $*"
echo "$line" | tee -a "$dir/bench-results.txt"
