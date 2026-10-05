using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace KoH2Upscale
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct KuFrame
    {
        public IntPtr color, depth, motion, output;
        public int renderWidth, renderHeight, outputWidth, outputHeight;
        public float jitterX, jitterY, mvScaleX, mvScaleY, sharpness;
        public int reset, quality, createFlags;
    }

    internal static class Native
    {
        const string Dll = "KoH2UpscaleNative";
        internal const int EventInit = 1, EventEvaluate = 2, EventShutdown = 3;
        internal const int StateIdle = 0, StateReady = 1, StateFailed = -1;
        internal const int QualityDlaa = 5;
        internal const int FlagMvLowRes = 1 << 1, FlagMvJittered = 1 << 2, FlagDepthInverted = 1 << 3;

        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr LoadLibraryW(string path);

        [DllImport(Dll)] internal static extern IntPtr KU_GetRenderEventFunc();
        [DllImport(Dll)] internal static extern int KU_GetState();
        [DllImport(Dll)] static extern int KU_PopLog(byte[] buf, int size);

        static readonly byte[] logBuffer = new byte[16384];
        internal static bool Loaded { get; private set; }

        // DllImport resolves by module name, so loading the file by full path first makes it find this copy.
        internal static bool Load(string dir)
        {
            var handle = LoadLibraryW(Path.Combine(dir, Dll + ".dll"));
            if (handle == IntPtr.Zero)
            {
                Plugin.Log.LogError($"cannot load {Dll}.dll from {dir}: error {Marshal.GetLastWin32Error()}");
                return false;
            }
            Loaded = true;
            return true;
        }

        internal static void PumpLog()
        {
            if (!Loaded)
                return;
            int n = KU_PopLog(logBuffer, logBuffer.Length);
            if (n <= 0)
                return;
            foreach (var line in Encoding.UTF8.GetString(logBuffer, 0, n).Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries))
                Plugin.Log.LogInfo($"native: {line}");
        }
    }
}
