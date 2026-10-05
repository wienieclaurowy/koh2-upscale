using System;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;

namespace KoH2Upscale
{
    // Runs last in the world camera's OnRenderImage chain and hands the finished frame to the NGX provider.
    // M2 spike: render size == output size, no jitter, zero motion vectors.
    public class UpscalePass : MonoBehaviour
    {
        const int FrameSlots = 3;

        Camera cam;
        CommandBuffer cb;
        IntPtr renderEvent;
        // Allocated once and never freed: render-thread events queued before a disable may still read them.
        static readonly IntPtr[] frames = new IntPtr[FrameSlots];
        static int slot;
        bool initIssued;
        RenderTexture output, motion;
        int framesSinceReset;

        void OnEnable()
        {
            cam = GetComponent<Camera>();
            cam.depthTextureMode |= DepthTextureMode.Depth;
            cb = new CommandBuffer { name = "KoH2Upscale" };
            renderEvent = Native.KU_GetRenderEventFunc();
            for (int i = 0; i < FrameSlots; i++)
                if (frames[i] == IntPtr.Zero)
                    frames[i] = Marshal.AllocHGlobal(Marshal.SizeOf<KuFrame>());
            framesSinceReset = 0;
            Plugin.Log.LogInfo($"upscale pass on '{cam.name}'");
        }

        void OnDisable()
        {
            cb.Clear();
            cb.IssuePluginEventAndData(renderEvent, Native.EventShutdown, IntPtr.Zero);
            Graphics.ExecuteCommandBuffer(cb);
            initIssued = false;
            cb.Release();
            ReleaseTargets();
        }

        void ReleaseTargets()
        {
            if (output) output.Release();
            if (motion) motion.Release();
            output = motion = null;
        }

        void EnsureTargets(RenderTexture src)
        {
            if (output && output.width == src.width && output.height == src.height && output.format == src.format)
                return;
            ReleaseTargets();
            output = new RenderTexture(src.width, src.height, 0, src.format) { name = "KoH2Upscale Output", enableRandomWrite = true };
            output.Create();
            motion = new RenderTexture(src.width, src.height, 0, RenderTextureFormat.RGHalf) { name = "KoH2Upscale ZeroMotion" };
            motion.Create();
            var previous = RenderTexture.active;
            RenderTexture.active = motion;
            GL.Clear(false, true, Color.clear);
            RenderTexture.active = previous;
            framesSinceReset = 0;
            Plugin.Log.LogInfo($"targets {src.width}x{src.height} {src.format}");
        }

        void OnRenderImage(RenderTexture src, RenderTexture dst)
        {
            var depth = Shader.GetGlobalTexture("_CameraDepthTexture");
            if (Native.KU_GetState() == Native.StateFailed || depth == null)
            {
                Graphics.Blit(src, dst);
                return;
            }
            EnsureTargets(src);

            var frame = new KuFrame
            {
                color = src.GetNativeTexturePtr(),
                depth = depth.GetNativeTexturePtr(),
                motion = motion.GetNativeTexturePtr(),
                output = output.GetNativeTexturePtr(),
                renderWidth = src.width,
                renderHeight = src.height,
                outputWidth = output.width,
                outputHeight = output.height,
                mvScaleX = -src.width,
                mvScaleY = -src.height,
                reset = framesSinceReset == 0 ? 1 : 0,
                quality = Native.QualityDlaa,
                createFlags = Native.FlagMvLowRes | Native.FlagDepthInverted,
            };
            var ptr = frames[slot];
            slot = (slot + 1) % FrameSlots;
            Marshal.StructureToPtr(frame, ptr, false);

            cb.Clear();
            if (!initIssued)
            {
                cb.IssuePluginEventAndData(renderEvent, Native.EventInit, ptr);
                initIssued = true;
            }
            cb.IssuePluginEventAndData(renderEvent, Native.EventEvaluate, ptr);
            Graphics.ExecuteCommandBuffer(cb);
            framesSinceReset++;

            Graphics.Blit(Native.KU_GetState() == Native.StateFailed ? src : output, dst);
        }
    }
}
