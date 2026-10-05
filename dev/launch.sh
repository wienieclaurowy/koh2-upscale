#!/bin/sh
# Steam launch wrapper. dev/launch.env (untracked, KEY=value lines) holds per-test overrides; absent = vanilla game.
dir=$(dirname "$(readlink -f "$0")")
export PROTON_ENABLE_WAYLAND=1
if [ -f "$dir/launch.env" ]; then
    set -a
    . "$dir/launch.env"
    set +a
fi
exec "$@"
