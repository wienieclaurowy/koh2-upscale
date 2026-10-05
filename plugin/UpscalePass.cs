using System;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace KoH2Upscale
{
    // Lives on WVCamera after PostProcessLayer, so its OnPreCull runs after PPv2 resets the projection and
    // its OnRenderImage runs last. Between OnPreCull and OnRenderImage the camera renders into a shrunken
    // rect with a jittered projection; outside that window rect and projection are the game's, so
    // pixelRect-based UI placement and mouse picking in Update see the full screen.
    public class UpscalePass : MonoBehaviour
    {
        const int FrameSlots = 3;
        static readonly int DepthTextureId = Shader.PropertyToID("_CameraDepthTexture");
        static readonly int MotionTextureId = Shader.PropertyToID("_CameraMotionVectorsTexture");

        // Allocated once and never freed: render-thread events queued before a disable may still read them.
        static readonly IntPtr[] frames = new IntPtr[FrameSlots];
        static int slot;

        Camera cam;
        CommandBuffer cb;
        IntPtr renderEvent;
        bool initIssued;
        RenderTexture output;
        Rect gameRect;
        Vector2Int displaySize, renderSize;
        Vector2 jitter;
        bool rendering;
        bool reset = true;
        int jitterIndex;
        Vector3 lastPosition;
        Quaternion lastRotation;
        bool loggedTargets;

        void OnEnable()
        {
            cam = GetComponent<Camera>();
            cb = new CommandBuffer { name = "KoH2Upscale" };
            renderEvent = Native.KU_GetRenderEventFunc();
            for (int i = 0; i < FrameSlots; i++)
                if (frames[i] == IntPtr.Zero)
                    frames[i] = Marshal.AllocHGlobal(Marshal.SizeOf<KuFrame>());
            reset = true;
            Plugin.Log.LogInfo($"upscale pass on '{cam.name}', render scale {Settings.RenderScale}");
            SceneManager.sceneLoaded += OnSceneLoaded;
            ApplyMipBias();
            Invoke(nameof(ApplyMipBias), 10f);
        }

        void OnSceneLoaded(Scene scene, LoadSceneMode mode) => ApplyMipBias();

        void ApplyMipBias()
        {
            if (Settings.RenderScale < 0.99f)
                MipBias.Apply(Mathf.Log(Settings.RenderScale, 2f) + Settings.MipBiasOffset);
        }

        void OnDisable()
        {
            SceneManager.sceneLoaded -= OnSceneLoaded;
            MipBias.Restore();
            if (rendering)
                RestoreCamera();
            cb.Clear();
            cb.IssuePluginEventAndData(renderEvent, Native.EventShutdown, IntPtr.Zero);
            Graphics.ExecuteCommandBuffer(cb);
            initIssued = false;
            cb.Release();
            if (output) output.Release();
            output = null;
        }

        bool Active => Native.KU_GetState() != Native.StateFailed;

        void OnPreCull()
        {
            if (!Active)
                return;
            // UserSettings.UpdateCameraDepthMode strips MotionVectors unless the game's own TAA is on.
            cam.depthTextureMode |= DepthTextureMode.Depth | DepthTextureMode.MotionVectors;

            gameRect = cam.rect;
            displaySize = new Vector2Int(cam.pixelWidth, cam.pixelHeight);
            renderSize = new Vector2Int(Mathf.RoundToInt(displaySize.x * Settings.RenderScale), Mathf.RoundToInt(displaySize.y * Settings.RenderScale));
            DetectCut();

            cam.aspect = (float)displaySize.x / displaySize.y;
            cam.rect = new Rect(gameRect.x, gameRect.y, gameRect.width * renderSize.x / displaySize.x, gameRect.height * renderSize.y / displaySize.y);
            ApplyJitter();
            rendering = true;
        }

        void DetectCut()
        {
            var t = cam.transform;
            if ((t.position - lastPosition).sqrMagnitude > 30f * 30f || Quaternion.Angle(t.rotation, lastRotation) > 10f)
                reset = true;
            lastPosition = t.position;
            lastRotation = t.rotation;
        }

        // FSR jitter recipe: Halton(2,3) over 8 * (display/render)^2 phases, offsets in render pixels.
        void ApplyJitter()
        {
            float ratio = (float)displaySize.x / renderSize.x;
            int phases = Mathf.Max(1, (int)(8f * ratio * ratio));
            int index = (jitterIndex++ % phases) + 1;
            jitter = new Vector2(Halton(index, 2) - 0.5f, Halton(index, 3) - 0.5f);
            var clip = new Vector3(2f * jitter.x / renderSize.x, 2f * jitter.y / renderSize.y, 0f);
            cam.nonJitteredProjectionMatrix = cam.projectionMatrix;
            cam.projectionMatrix = Matrix4x4.Translate(clip) * cam.nonJitteredProjectionMatrix;
            cam.useJitteredProjectionMatrixForTransparentRendering = true;
        }

        static float Halton(int index, int radix)
        {
            float result = 0f, fraction = 1f / radix;
            while (index > 0)
            {
                result += (index % radix) * fraction;
                index /= radix;
                fraction /= radix;
            }
            return result;
        }

        void RestoreCamera()
        {
            cam.rect = gameRect;
            cam.ResetProjectionMatrix();
            cam.ResetAspect();
            rendering = false;
        }

        void OnRenderImage(RenderTexture src, RenderTexture dst)
        {
            if (!rendering)
            {
                Graphics.Blit(src, dst);
                return;
            }
            RestoreCamera();
            var depth = Shader.GetGlobalTexture(DepthTextureId);
            var motion = Shader.GetGlobalTexture(MotionTextureId);
            if (!Active || depth == null || motion == null)
            {
                Graphics.Blit(src, dst);
                return;
            }
            if (!output || output.width != displaySize.x || output.height != displaySize.y || output.format != src.format)
            {
                if (output) output.Release();
                output = new RenderTexture(displaySize.x, displaySize.y, 0, src.format) { name = "KoH2Upscale Output", enableRandomWrite = true };
                output.Create();
                reset = true;
            }
            if (!loggedTargets)
            {
                loggedTargets = true;
                Plugin.Log.LogInfo($"render {renderSize.x}x{renderSize.y} -> {displaySize.x}x{displaySize.y}; src {src.width}x{src.height} {src.format}, "
                    + $"depth {depth.width}x{depth.height}, motion {motion.width}x{motion.height}, dst {(dst ? $"{dst.width}x{dst.height}" : "backbuffer")}");
            }

            var frame = new KuFrame
            {
                color = NativePtr(src),
                depth = NativePtr(depth),
                motion = NativePtr(motion),
                output = NativePtr(output),
                renderWidth = renderSize.x,
                renderHeight = renderSize.y,
                outputWidth = displaySize.x,
                outputHeight = displaySize.y,
                jitterX = jitter.x,
                jitterY = jitter.y,
                mvScaleX = -renderSize.x,
                mvScaleY = -renderSize.y,
                sharpness = Settings.Sharpness,
                reset = reset ? 1 : 0,
                quality = Native.QualityFor(Settings.RenderScale),
                createFlags = Native.FlagMvLowRes | Native.FlagDepthInverted,
            };
            reset = false;
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

            Graphics.Blit(Active ? output : src, dst);
        }

        // GetNativeTexturePtr waits for the render thread, so pointers are cached. Unity can recreate the
        // native texture behind the same object, so the cache is dropped on resize and every 120 frames.
        readonly System.Collections.Generic.Dictionary<Texture, IntPtr> nativePtrs = new System.Collections.Generic.Dictionary<Texture, IntPtr>();
        int nativePtrsFrame;
        Vector2Int nativePtrsSize;

        IntPtr NativePtr(Texture tex)
        {
            if (Time.frameCount - nativePtrsFrame >= 120 || nativePtrsSize != renderSize)
            {
                nativePtrs.Clear();
                nativePtrsFrame = Time.frameCount;
                nativePtrsSize = renderSize;
            }
            if (!nativePtrs.TryGetValue(tex, out var ptr))
            {
                ptr = tex.GetNativeTexturePtr();
                nativePtrs[tex] = ptr;
            }
            return ptr;
        }
    }
}
