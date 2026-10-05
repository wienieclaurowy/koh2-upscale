using System.Collections.Generic;
using UnityEngine;

namespace KoH2Upscale
{
    // The built-in pipeline has no global sampler LOD bias, so each loaded texture gets mipMapBias directly.
    // Applied on enable and after scene loads; textures streamed in later keep their own bias.
    internal static class MipBias
    {
        static readonly Dictionary<Texture, float> original = new Dictionary<Texture, float>();

        internal static void Apply(float bias)
        {
            int changed = 0;
            foreach (var tex in Resources.FindObjectsOfTypeAll<Texture>())
            {
                if (!(tex is Texture2D || tex is Texture2DArray) || tex.filterMode == FilterMode.Point)
                    continue;
                if (!original.ContainsKey(tex))
                    original[tex] = tex.mipMapBias;
                if (tex.mipMapBias != original[tex] + bias)
                {
                    tex.mipMapBias = original[tex] + bias;
                    changed++;
                }
            }
            Plugin.Log.LogInfo($"mip bias {bias:F2} on {changed} textures ({original.Count} tracked)");
        }

        internal static void Restore()
        {
            foreach (var kv in original)
                if (kv.Key)
                    kv.Key.mipMapBias = kv.Value;
            original.Clear();
        }

    }
}
