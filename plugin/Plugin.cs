using System.Linq;
using System.Text;
using BepInEx;
using BepInEx.Logging;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.PostProcessing;
using UnityEngine.SceneManagement;

namespace KoH2Upscale
{
    [BepInPlugin("io.github.wienieclaurowy.koh2upscale", "KoH2 Upscale", "0.1.0")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;
        string lastCameraDump;

        void Awake()
        {
            Log = Logger;
            Log.LogInfo($"graphics {SystemInfo.graphicsDeviceType} {SystemInfo.graphicsDeviceVersion}, screen {Screen.width}x{Screen.height}");
            SceneManager.sceneLoaded += (scene, mode) => { Log.LogInfo($"scene loaded: {scene.name} ({mode})"); DumpCameras(false); };
            InvokeRepeating(nameof(PollCameras), 5f, 5f);
        }

        void PollCameras() => DumpCameras(false);

        void Update()
        {
            if (Input.GetKeyDown(KeyCode.F10))
                DumpCameras(true);
        }

        void DumpCameras(bool force)
        {
            var sb = new StringBuilder();
            foreach (var cam in Camera.allCameras.OrderBy(c => c.depth))
            {
                sb.Append($"\n  cam '{cam.name}' depth={cam.depth} clear={cam.clearFlags} mask=0x{cam.cullingMask:X8} ");
                sb.Append($"rt={(cam.targetTexture ? $"{cam.targetTexture.name} {cam.targetTexture.width}x{cam.targetTexture.height}" : "screen")} ");
                sb.Append($"rect={cam.rect} hdr={cam.allowHDR} msaa={cam.allowMSAA} depthTex={cam.depthTextureMode} path={cam.actualRenderingPath} ");
                sb.Append($"ortho={cam.orthographic} fov={cam.fieldOfView:F1} near={cam.nearClipPlane} far={cam.farClipPlane}");
                var ppl = cam.GetComponent<PostProcessLayer>();
                if (ppl)
                    sb.Append($"\n    PostProcessLayer enabled={ppl.enabled} aa={ppl.antialiasingMode} volumeMask=0x{ppl.volumeLayer.value:X8}");
                foreach (CameraEvent ev in System.Enum.GetValues(typeof(CameraEvent)))
                    foreach (var cb in cam.GetCommandBuffers(ev))
                        sb.Append($"\n    cmdbuf {ev}: '{cb.name}'");
                foreach (var mb in cam.GetComponents<MonoBehaviour>())
                    sb.Append($"\n    component {mb.GetType().FullName} enabled={mb.enabled}");
            }
            var dump = sb.ToString();
            if (!force && dump == lastCameraDump)
                return;
            lastCameraDump = dump;
            Log.LogInfo($"cameras ({Camera.allCamerasCount}), screen {Screen.width}x{Screen.height}, scene {SceneManager.GetActiveScene().name}:{dump}");
        }
    }
}
