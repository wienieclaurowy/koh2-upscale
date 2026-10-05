using System;
using System.IO;
using System.Linq;
using BepInEx;
using UnityEngine;

namespace KoH2Upscale
{
    // Drives the game from a shell without clicks: write one command per line to BepInEx/koh2upscale.cmd.
    public class DevCommands : MonoBehaviour
    {
        string cmdPath;
        float nextPoll;
        bool waitingForWorld;
        string[] pendingLoad;
        int readyFrames;
        System.Collections.Generic.List<float> benchFrames;

        float nextCameraPoll;

        void Awake() => cmdPath = Path.Combine(Paths.BepInExRootPath, "koh2upscale.cmd");

        void Update()
        {
            benchFrames?.Add(Time.unscaledDeltaTime * 1000f);
            if (waitingForWorld)
                CheckWorldReady();
            if (pendingLoad != null && Title.allLoaded)
            {
                var args = pendingLoad;
                pendingLoad = null;
                Load(args[0], args[1]);
            }
            if (Input.GetKeyDown(KeyCode.F10))
                Plugin.DumpCameras(true);
            if (Time.unscaledTime >= nextCameraPoll)
            {
                nextCameraPoll = Time.unscaledTime + 5f;
                Plugin.DumpCameras(false);
            }
            if (Time.unscaledTime < nextPoll)
                return;
            nextPoll = Time.unscaledTime + 0.5f;
            if (!File.Exists(cmdPath))
                return;
            string[] lines;
            try
            {
                lines = File.ReadAllLines(cmdPath);
                File.Delete(cmdPath);
            }
            catch (IOException)
            {
                return;
            }
            foreach (var line in lines.Select(l => l.Trim()).Where(l => l.Length > 0))
                Run(line);
        }

        void Run(string line)
        {
            Plugin.Log.LogInfo($"[{Stamp()}] cmd: {line}");
            var parts = line.Split(new[] { ' ' }, 3, StringSplitOptions.RemoveEmptyEntries);
            try
            {
                switch (parts[0])
                {
                    case "load": pendingLoad = new[] { parts[1], parts[2] }; break;
                    case "bench": Bench(); break;
                    case "view": View(int.Parse(parts[1])); break;
                    case "aa": Plugin.Log.LogInfo($"aa {parts[1]}: {UserSettings.SetAntialiasing(parts[1])}"); break;
                    case "cams": Plugin.DumpCameras(true); break;
                    case "quit": Application.Quit(); break;
                    default: Plugin.Log.LogWarning($"unknown command '{parts[0]}'"); break;
                }
            }
            catch (Exception e)
            {
                Plugin.Log.LogError($"cmd '{line}' failed: {e}");
            }
        }

        void Load(string campaignId, string slot)
        {
            var game = GameLogic.Get(false);
            if (game == null || game.state == Logic.Game.State.LoadingMap)
            {
                Plugin.Log.LogWarning($"load: game not ready ({game?.state})");
                return;
            }
            SaveGame.ScanSavesDir(true);
            SaveGame.UpdateList(true, true);
            var info = SaveGame.list.FirstOrDefault(i => i.campaign_id == campaignId
                && Path.GetFileName(i.fullPath.TrimEnd('\\', '/')) == slot);
            if (info == null)
            {
                Plugin.Log.LogWarning($"load: no save {campaignId}/{slot} among {SaveGame.list.Count}");
                return;
            }
            game.load_game = Logic.Game.LoadedGameType.LoadFromMainMenu;
            game.StartGame(false, null, info.fullPath);
            waitingForWorld = true;
            readyFrames = 0;
        }

        void CheckWorldReady()
        {
            var game = GameLogic.Get(false);
            var ready = game != null && game.state == Logic.Game.State.Running && !Logic.Game.isLoadingSaveGame
                && !LoadingScreen.IsShown() && CameraController.IsReady();
            readyFrames = ready ? readyFrames + 1 : 0;
            if (readyFrames < 120)
                return;
            waitingForWorld = false;
            Plugin.Log.LogInfo($"[{Stamp()}] world ready");
        }

        void Bench()
        {
            if (EuropeBenchmark.Instance == null)
            {
                Plugin.Log.LogWarning("bench: no EuropeBenchmark in this scene");
                return;
            }
            Plugin.Log.LogInfo($"[{Stamp()}] bench start");
            benchFrames = new System.Collections.Generic.List<float>(5000);
            EuropeBenchmark.Instance.Benchmark(measures =>
            {
                var frames = benchFrames.Skip(1).OrderBy(m => m).ToList();
                benchFrames = null;
                var render = measures.OrderBy(m => m).ToList();
                Plugin.Log.LogInfo($"[{Stamp()}] bench done: {frames.Count} frames, avg {1000f * frames.Count / frames.Sum():F1} fps, "
                    + $"frame median {frames[frames.Count / 2]:F2} ms, p95 {frames[frames.Count * 95 / 100]:F2} ms, p99 {frames[frames.Count * 99 / 100]:F2} ms, "
                    + $"camera render (cpu) median {render[render.Count / 2]:F2} ms");
            });
        }

        // Parks the camera on one of the built-in benchmark's poses, for like-for-like screenshots.
        void View(int index)
        {
            var presets = HarmonyLib.Traverse.Create(EuropeBenchmark.Instance).Field("camera_presets").GetValue<CameraPresets>();
            presets.ApplyCameraPreset(index);
            CameraController.GameCamera.LockUserInput(true);
            Plugin.Log.LogInfo($"[{Stamp()}] view {index} of {presets.presets.Count}");
        }

        static string Stamp() => DateTime.UtcNow.ToString("HH:mm:ss.fff");
    }
}
