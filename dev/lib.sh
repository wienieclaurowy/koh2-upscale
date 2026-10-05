# Shared by the dev scripts: launch the game with BepInEx plus extra env, drive it through the plugin's
# command file, and always quit and clean up. Source it, then call launch_and_load KEY=value...
dir=$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")
game="$HOME/.local/share/Steam/steamapps/common/Knights of Honor II"
log="$game/BepInEx/LogOutput.log"
cmd="$game/BepInEx/koh2upscale.cmd"
. "$dir/local.env"

game_pid() { pgrep -f '[S]overeign.exe' | head -n 1; }

wait_log() {
    local re=$1 timeout=$2 i
    for ((i = 0; i < timeout * 2; i++)); do
        grep -qE "$re" "$log" 2>/dev/null && return 0
        [ $i -gt 60 ] && [ -z "$(game_pid)" ] && { echo "game exited while waiting for: $re"; return 1; }
        sleep 0.5
    done
    echo "timeout waiting for: $re"; return 1
}

send() { printf '%s\n' "$@" >> "$cmd"; }

finish() {
    [ -n "$(game_pid)" ] && send "aa None" quit
    for i in $(seq 1 60); do [ -z "$(game_pid)" ] && break; sleep 1; done
    local pid; pid=$(game_pid); [ -n "$pid" ] && kill "$pid"
    rm -f "$dir/launch.env" "$cmd"
}

launch_and_load() {
    [ -n "$(game_pid)" ] && { echo "game already running"; return 1; }
    {
        echo "WINEDLLOVERRIDES=winhttp=n,b"
        echo "KOH2UPSCALE_NO_CRASH_REPORTS=1"
        for kv in "$@"; do echo "$kv"; done
    } > "$dir/launch.env"
    rm -f "$log" "$cmd"
    steam -applaunch 736820 >/dev/null 2>&1 &
    wait_log 'scene loaded: title' 180 || return 1
    send "load $KOH2_SAVE_CAMPAIGN $KOH2_SAVE_SLOT"
    wait_log 'world ready' 300 || return 1
    [ -n "${KOH2_AA:-}" ] && send "aa $KOH2_AA"
    return 0
}
