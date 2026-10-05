#!/bin/bash
# Usage: [KOH2_AA=TAA] [KOH2_VIEWS="0 5 9"] dev/shots.sh LABEL [KEY=value ...]
# Loads the save, parks the camera on benchmark views and saves full-size screenshots to ref/shots/LABEL/
# (monitor: KOH2_OUTPUT in dev/local.env, all monitors if unset).
set -uo pipefail
. "$(dirname "$(readlink -f "$0")")/lib.sh"
label=${1:?label}; shift
out="$dir/../ref/shots/$label"
mkdir -p "$out"

launch_and_load "$@" || { finish; exit 1; }
sleep 5
for v in ${KOH2_VIEWS:-0 5 9}; do
    send "view $v"
    sleep 4
    grim ${KOH2_OUTPUT:+-o "$KOH2_OUTPUT"} "$out/view$v.png"
done
finish
ls "$out"
