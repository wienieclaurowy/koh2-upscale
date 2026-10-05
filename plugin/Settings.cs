using System;
using System.Globalization;
using BepInEx.Configuration;

namespace KoH2Upscale
{
    // Config values with KOH2UPSCALE_* environment overrides, so test runs never edit the config file.
    internal static class Settings
    {
        internal static string Mode { get; private set; }
        internal static string Backend { get; private set; }
        internal static float RenderScale { get; private set; }
        internal static float Sharpness { get; private set; }
        internal static float MipBiasOffset { get; private set; }

        internal static void Load(ConfigFile config)
        {
            Mode = Env("MODE") ?? config.Bind("Upscaler", "Mode", "off",
                "off, or fsr: render the world map at RenderScale and rebuild it with the NGX provider (OptiScaler).").Value;
            Backend = Env("BACKEND") ?? config.Bind("Upscaler", "Backend", "ngx",
                "ngx: an NGX provider, OptiScaler with Dx11Upscaler=fsr31 in practice. vulkan: experimental FSR 3.1 on DXVK's own Vulkan device, no OptiScaler needed, slower and flickers on foliage.").Value;
            RenderScale = Clamp(EnvFloat("SCALE") ?? config.Bind("Upscaler", "RenderScale", 0.667f,
                "Render size as a fraction of the screen: 1.0 = native AA, 0.667 = quality, 0.5 = performance.").Value, 0.33f, 1f);
            Sharpness = Clamp(EnvFloat("SHARPNESS") ?? config.Bind("Upscaler", "Sharpness", 0f, "0 to 1.").Value, 0f, 1f);
            MipBiasOffset = EnvFloat("MIP_BIAS_OFFSET") ?? config.Bind("Upscaler", "MipBiasOffset", -1f,
                "Added to log2(render/display) for texture mip bias; FSR recommends -1. Less negative if textures shimmer.").Value;
        }

        static string Env(string name) => Environment.GetEnvironmentVariable("KOH2UPSCALE_" + name);

        static float? EnvFloat(string name) =>
            float.TryParse(Env(name), NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : (float?)null;

        static float Clamp(float v, float min, float max) => v < min ? min : v > max ? max : v;
    }
}
