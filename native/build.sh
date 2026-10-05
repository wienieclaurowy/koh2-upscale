#!/bin/sh
# Builds the native bridge with mingw; DEPLOY=1 copies it and amd_fidelityfx_vk.dll next to the BepInEx plugin.
set -e
cd "$(dirname "$0")"
out=../build
mkdir -p "$out"

vk_tag=vulkan-sdk-1.4.363.0
if [ ! -f "$out/vulkan-headers/include/vulkan/vulkan.h" ]; then
    mkdir -p "$out/vulkan-headers"
    curl -sfL "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/$vk_tag.tar.gz" \
        | tar -xz -C "$out/vulkan-headers" --strip-components=1 "Vulkan-Headers-$vk_tag/include"
fi

ffx_dll="$out/amd_fidelityfx_vk.dll"
ffx_md5=9718fd774c61be1af6af411cf65394cd
if [ ! -f "$ffx_dll" ]; then
    curl -sfL -o "$ffx_dll" "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/raw/v1.1.4/PrebuiltSignedDLL/amd_fidelityfx_vk.dll"
fi
echo "$ffx_md5  $ffx_dll" | md5sum -c --quiet

x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -shared -fno-exceptions -fno-rtti \
    -static -static-libgcc -static-libstdc++ -I"$out/vulkan-headers/include" \
    -o "$out/KoH2UpscaleNative.dll" koh2upscale_native.cpp fsr_vk.cpp -lpsapi
if [ "${DEPLOY:-0}" = 1 ]; then
    dest="${GAME_DIR:-$HOME/.local/share/Steam/steamapps/common/Knights of Honor II}/BepInEx/plugins/KoH2Upscale/"
    cp "$out/KoH2UpscaleNative.dll" "$ffx_dll" "$dest"
fi
