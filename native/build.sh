#!/bin/sh
# Builds the native bridge with mingw; DEPLOY=1 copies it next to the BepInEx plugin.
set -e
cd "$(dirname "$0")"
mkdir -p ../build
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -fno-exceptions -fno-rtti \
    -static -static-libgcc -static-libstdc++ \
    -o ../build/KoH2UpscaleNative.dll koh2upscale_native.cpp -lpsapi
if [ "${DEPLOY:-0}" = 1 ]; then
    cp ../build/KoH2UpscaleNative.dll "${GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Knights of Honor II}/BepInEx/plugins/KoH2Upscale/"
fi
