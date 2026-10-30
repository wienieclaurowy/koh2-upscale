using UnityEngine;

namespace KoH2Upscale
{
    // Keeps an UpscalePass on the live world-map camera; battles and the political map stay untouched.
    public class UpscaleController : MonoBehaviour
    {
        void Update()
        {
            Native.PumpLog();
            var cam = CameraController.MainCamera;
            if (cam == null || cam.name != "WVCamera")
                return;
            var pass = cam.GetComponent<UpscalePass>();
            if (pass == null)
                pass = cam.gameObject.AddComponent<UpscalePass>();
            if (Input.GetKeyDown(Settings.ToggleKey))
                pass.SetBypassed(!UpscalePass.Bypassed);
        }
    }
}
