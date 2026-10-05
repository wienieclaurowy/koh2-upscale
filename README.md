# KoH2 Upscale

FSR upscaling for *Knights of Honor II: Sovereign*, which ships without any upscaler.

The mod renders the 3D campaign map at a lower resolution and rebuilds it at full size with FSR. The UI stays at native resolution, so text and icons stay sharp. Battles and the political map are left alone for now.

On a 4K screen at 67% render scale, the built-in Europe benchmark goes from about 57 to 68 fps.

## Status

This is an experimental hobby project. It has only been tested on one PC, on Linux with Proton, and never on Windows or other hardware. Expect bugs, and back up your saves before you try it.

There is no installer yet, so setup is manual.

## How it works

- A BepInEx plugin shrinks the world-map camera, jitters it, and hands the frame, depth and motion vectors to a small native DLL.
- By default, the native DLL calls the NGX (DLSS) interface, and [OptiScaler](https://github.com/optiscaler/OptiScaler) turns those calls into FSR 3.1 on the game's own DX11 device.
- An experimental `vulkan` backend runs FSR 3.1 directly on DXVK's Vulkan device, without OptiScaler.

`docs/render-notes.md` maps how the game renders the world map, if you want to dig in.

## Building

You need the .NET SDK, mingw-w64 and a copy of the game with [BepInEx 5](https://github.com/BepInEx/BepInEx) installed.

```sh
dotnet build plugin -p:Deploy=true   # builds the plugin into BepInEx/plugins/KoH2Upscale
DEPLOY=1 native/build.sh             # builds the native DLL and fetches amd_fidelityfx_vk.dll
```

Both look for the game in the default Steam library. Set `GameDir` (plugin) or `GAME_DIR` (native) if yours lives elsewhere.

Then set `Mode = fsr` in `BepInEx/config/io.github.wienieclaurowy.koh2upscale.cfg`. That file also holds the render scale, sharpness and the in-game toggle key (F8 by default).

## Credits

- [BepInEx](https://github.com/BepInEx/BepInEx) and [HarmonyX](https://github.com/BepInEx/HarmonyX) for loading and patching the game.
- [OptiScaler](https://github.com/optiscaler/OptiScaler) for turning NGX calls into FSR.
- AMD's [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) for FSR itself. Its API headers are in `native/third_party/ffx_api` under their MIT licence.
- [FSR3Unity](https://github.com/ndepoel/FSR3Unity) by Nico de Poel, a helpful reference for wiring FSR into Unity.
- [DXVK](https://github.com/doitsujin/dxvk) and Proton, which the Vulkan backend builds on.
- Black Sea Games and THQ Nordic for the game. This is an unofficial fan mod, not affiliated with them.

## License

MIT, see `LICENSE`. Third-party files keep their own licences.
