# KoH2 rendering path notes

Facts for an upscaling mod: render the 3D map at a lower resolution, upscale with FSR 4, keep UI native.
Unity 2019.4.41f2, Mono, built-in pipeline, DX11 only (`m_GraphicsAPIs` = [D3D11]), linear colour space,
graphics jobs + native graphics jobs on (`<game>/Sovereign_Data/boot.config`).

Code refs point into the ilspycmd projects: `AC/` = `ref/Assembly-CSharp/`, `FP/` =
`ref/Assembly-CSharp-firstpass/`, `PP/` = `ref/Unity.Postprocessing.Runtime/UnityEngine.Rendering.PostProcessing/`,
`SC/` = `ref/sc.posteffects.runtime/SCPE/`. Serialized-data facts come from UnityPy reads of
`<game>/Sovereign_Data/*` and `<game>/AssetBundles/*` (world-map scene: `Assets/Europe/europe.unity` in
`AssetBundles/maps_europe`; battle scenes in `AssetBundles/maps_battleview`; prefabs in `europe`,
`battleview`). "(inferred)" marks anything not read directly in code or data.

Layers (globalgamemanagers): 4 Water, 5 UI, 8 Terrain, 9 Settlements, 10 Labels, 11 Animals, 12 Decals,
13 Roads, 14 Rivers, 15 Armies, 16 Political, 17 Mountains, 18 ChimneySmoke, 19 Tiny Props, 20 Ships,
21 Rocks, 22 Lakes, 23 PostProcessing, 24 PostProcessVolumes, 25 GUIParticles, 26 Physics,
27 Vegetation, 28 PassableSettlement, 29 ImpassableSettlement, 31 Facegen.

## 1. Cameras

### Game cameras (CameraController)

`CameraController` (AC/CameraController.cs) finds child `GameCamera`s named `WVCamera`, `PVCamera`,
`BVCamera` in `Init()` (AC/CameraController.cs:145-170). `ViewModeChanged()`
(AC/CameraController.cs:217-256): if a `BVCamera` exists it is current; otherwise `WVCamera` and
`PVCamera` are swapped with `gameObject.SetActive(...)` by `ViewMode.IsPoliticalView()`
(AC/ViewMode.cs:372). Game code reads the live camera through `CameraController.MainCamera` /
`CameraController.GameCamera` (AC/CameraController.cs:28-54), not `Camera.main`.
`GameCamera.Init()` sets `clearStencilAfterLightingPass` and `opaqueSortMode = NoDistanceSort`
(AC/GameCamera.cs:111-123).

| Camera (scene path) | Role | depth | clear | path | mask | other |
|---|---|---|---|---|---|---|
| `Cameras/WVCamera` (europe scene) | **3D campaign map** | 0 | Skybox | Deferred, HDR, no MSAA | Everything | FOV 50, near 0.3, far 5000 (rewritten per frame from zoom, AC/GameMode.cs:491), no targetTexture, no dyn-res |
| `Cameras/PVCamera` (europe scene, inactive until a political map mode) | political map modes | -1 | SolidColor | Deferred, HDR | UI + Political | FOV 45, far 10000, **no PostProcessLayer** |
| `BattleViewCameras/BVCamera` (battle scenes, prefab in `battleview`) | **battles** | -1 | Skybox | Deferred, HDR, MSAA allowed, ForceIntoRT | all but Political, PostProcessing, PostProcessVolumes, GUIParticles, Passable/ImpassableSettlement, Facegen | FOV 50, far 10000 |
| `UI/Canvas/Bottom/Minimap_Right/id_Minimap/Camera` | disabled scene camera, ortho, mask Terrain | 1 | | | | unused by code (inferred; `MiniMap` builds its own) |
| `level0` `Cameras/PVCamera`, `BenchmarkingCamera`, `FXCamera` | title scene / hardware autodetect / GUI particles | | | | | no PostProcessLayer in `level0` |

Components on `WVCamera`, in serialized order: `PostProcessLayer`, `CTAA_PC` (enabled in the scene,
disabled in the `europe` prefab; the menu toggles it, section 2), `VerticalTint`, `SceneCamSync`,
`GameCamera`, `SceneSpecificUserSettings` (quality "wv_dev"), `SCPE.RenderScreenSpaceSkybox`
(disabled), `FarPlanePerLayer`, `CustomMotionVectors`.

`GameCamera` settings on WVCamera: supportedModes [GameMode, FirstPerson, FreeLook]; dist 50-115,
pitch 35-55, lookAtHeight 30, minHeight 60, farPlane 5000. `FarPlanePerLayer` culls per layer
(Labels 250, Roads 280, Settlements 300, Terrain 5000, ...), rescaled by the extra-zoom setting
(AC/UserSettings.cs:1555-1593).

Components on `BVCamera`: `PostProcessLayer` (TAA, finalBlitToCameraTarget on; one scene uses SMAA),
`CustomMotionVectors`, `EmissionRenderer`, `SSRT` (serialized disabled) + `SSRTQualityManager`,
`VerticalTint` (enabled in scene copies), `GameCamera` (adds a "Scenic" mode), FMOD listener,
`FarPlanePerLayer` (disabled); a few scenes also carry a disabled `CTAA_PC`.

Scene roots looked up by name (AC/SceneSpecificUserSettings.cs:36-40): `Cameras`, `BattleViewCameras`,
`PPV`, `Terrain`, `Ocean`. Battles load additively (AC/BattleViewLoader.cs:106) and the world scene's
roots, `Cameras` included, are deactivated (AC/BattleViewLoader.cs:247-280).

### Helper and render-to-texture cameras (runtime)

| Camera | Owner | Target | When |
|---|---|---|---|
| `MinmapCamera` | `MiniMap.CreateCamera()` AC/MiniMap.cs:811-830 | 1024x1024 ARGB32 temp RT, ortho, Forward | manual `Render()` on minimap refresh (AC/MiniMap.cs:127-166, 807) |
| `HideTreesCamera` | AC/TreesBatching.cs:272-299 | 512x512 R8 RT | battles only; `Render()` from inside the main camera's `onPreCull` (AC/TreesBatching.cs:302-315) |
| `LayerMaskRenderCam`, `LayerRenderCam` | `CTAA_PC.Start()` AC/CTAA_PC.cs:326-364 | backbuffer, depth+1 | only with `CTAA_PC.ExtendedFeatures` (not used in shipped data, SuperSampleMode 0, inferred off) |
| Mirror reflection camera | `MirrorReflection` on `Ocean` (AC/MirrorReflection.cs:79-96, 140-197) | RT at 0.5x the viewing camera's `pixelWidth/Height` | `OnWillRenderObject` per rendering camera; mask Terrain+Ships+Rocks |
| `CubemapCamera` | `SSRT.RenderCubemap()` AC/SSRT.cs:325-340 | cubemap face per frame | battles with SSRT on |
| `BATTLE_UI_CAM/Camera` | `ui` bundle | RT "BattleUI_BGR" 1386x175 | battle UI, inactive by default |
| Bake cameras | AC/Common.cs:1150-1215, `TerrainInfo`, `TerrainTypesBuilder*`, `PathDataBuilder`, `RuntimePathDataBuilder`, `TerrainHeightsRenderer` | fixed-size temp RTs | one-shot `RenderWithShader` bakes |

Only the current game camera renders to the backbuffer each frame.

### Per-frame order on the world map

1. `Update`: `GameCamera.Update()` moves the camera via its control scheme (AC/GameCamera.cs:205-229);
   world-anchored UI is placed from the camera (section 3).
2. `WVCamera` (or `PVCamera`) renders: component `OnPreCull` (PPv2 projection reset
   PP/PostProcessLayer.cs:306-340, then CTAA jitter AC/CTAA_PC.cs:382-438 by component order) and the
   static `Camera.onPreCull` delegates of the WV batchers (section 4); G-buffer, lighting, command buffers
   (section 5), `[ImageEffectOpaque]` `VerticalTint`, transparents, PPv2 stack in the `BeforeImageEffects`
   command buffer, then the `OnRenderImage` chain (`PostProcessLayer` copy, then `CTAA_PC`).
3. Screen Space Overlay canvases draw last, at native resolution.

The relative order of MonoBehaviour `OnPreCull` and the static `Camera.onPreCull` delegate was not
verified.

## 2. Post-processing

### PostProcessLayer per camera

- `WVCamera`: yes. Serialized antialiasingMode None, volumeLayer = PostProcessVolumes (24), trigger =
  WVCamera, finalBlitToCameraTarget off, stopNaN on, deferred fog on. BeforeStack bundles include SCPE
  Fog, CloudShadows, EdgeDetection, LensFlares, LightStreaks, Pixelize, Posterize, Sketch; AfterStack
  about 24 SCPE effects. Which run depends on the profiles below.
- `PVCamera`: none. `BVCamera`: yes, TAA.

### Menu AA -> code

- Options in `<game>/Defs/Preferences.def` (`antialiasing_mode`): `None`, `FXAA`, `SMAA_low`,
  `SMAA_high`, `TAA`. Presets: Low None, Medium FXAA, High `"SMAA High"`, Best TAA. The High preset
  string does not match the option id `SMAA_high`; how `ApplyValue` resolves it was not checked.
- `UserSettings.SetAntialiasing(string mode)` (AC/UserSettings.cs:1753-1787), called from the settings
  switch (AC/UserSettings.cs:1217-1220) and on every scene start via
  `SceneSpecificUserSettings.ApplyQualityUserSettings()` (AC/SceneSpecificUserSettings.cs:10-14). For each
  `PostProcessLayer` under the CameraController object: if a `CTAA_PC` sits on the same GameObject,
  `TAA` enables it and sets PPv2 to None, any other mode disables it; without `CTAA_PC`, `TAA` becomes
  PPv2 `TemporalAntialiasing`. Mode mapping: `GetPostProcessAntialiasinMode()`
  (AC/UserSettings.cs:1344-1360), SMAA quality `GetSMAAQuality()` (AC/UserSettings.cs:1362-1369).
- So on the world map "TAA" = Livenda CTAA; in battles it is PPv2 TAA (or CTAA where a scene carries a
  `CTAA_PC`).

### CTAA_PC (world-map TAA)

- `OnEnable` adds `Depth | MotionVectors` (AC/CTAA_PC.cs:175-177).
- `OnPreCull` jitter (AC/CTAA_PC.cs:425-438): `m02 += (x_jit[i]*TemporalJitterScale*2-1)/pixelRect.width`.
  Shipped `TemporalJitterScale` is 0.001, so the offset is a near-constant -1/width in NDC (about half a
  pixel) with no real sub-pixel sequence: CTAA runs as reprojected temporal smoothing, effectively
  unjittered (inferred from the formula and the serialized value).
- `OnRenderImage` (AC/CTAA_PC.cs:440-524): sharpen pre-pass `Hidden/CTAA_Enhance_PC` with texel size from
  `Screen.width/height` (not `source`), then a history ping-pong with `Hidden/CTAA_PC`; history RTs sized
  from `source`. Shipped: TemporalStability 3, AdaptiveSharpness 1.5, SuperSampleMode 0.
- `LateUpdate` nudges the camera transform for 4 frames after enable/resolution change
  (AC/CTAA_PC.cs:186-205).
- Because `CTAA_PC` comes after `PostProcessLayer` and PPv2 renders its stack in the `BeforeImageEffects`
  command buffer, CTAA runs on the already tonemapped and graded image (inferred from component order).

### Volumes and effects (world map)

`Cameras/PPV` holds three volumes on layer 24: "PPV Global" (global, profile Global), "PPV Nordic" and
"PPV South" (local, blend 200 / 70, priority 1). Profiles (`AssetBundles/europe`, `europe_profiles/*.asset`)
share one effect set: AmbientOcclusion (ScalableAO), Bloom (intensity 5, threshold 0.94), DepthOfField
(active), Vignette (0.35), ColorGrading (LDR mode, ACES), SCPE.Fog (distance + height), LensDistortion
(inactive). No MotionBlur, AutoExposure, Grain or ChromaticAberration.

Menu toggles act on the `PPV` volumes (AC/UserSettings.cs:1834-1931): SSAO -> `AmbientOcclusion`,
Fog -> `SCPE.Fog`, Depth of field -> `DepthOfField`, Bloom -> `Bloom`. `GameMode` drives DoF focus and
SC Fog start distance from zoom (AC/GameMode.cs:505-540).

Battle profiles (`AssetBundles/battleview`): "PP OpenField" and "PP default": AO, ColorGrading (HDR,
ACES), Bloom, SSR (inactive), Vignette, DoF, SCPE.Fog, SCPE.LUT (inactive).

### SC Post Effects

Used only as PPv2 effect settings inside the profiles (`[PostProcess(...)]`, SC/*.cs); no SC component is
active on the WV camera (`SCPE.RenderScreenSpaceSkybox` is disabled there). Only `SCPE.Fog`
(`BeforeStack`, needs `Depth`, SC/Fog.cs:8, SC/FogRenderer.cs:180-182) is in the world-map profiles.

### PPv2 order (built-in)

Command buffers at `BeforeReflections`, `BeforeLighting`, `BeforeImageEffectsOpaque`,
`BeforeImageEffects` (PP/PostProcessLayer.cs:151-154). `Render()` (PP/PostProcessLayer.cs:662-770): PPv2
TAA -> BeforeStack (SC Fog) -> builtins (DoF, bloom, colour grading uber, vignette) -> AfterStack ->
final pass (FXAA/SMAA). AO runs in the opaque buffer. `OnRenderImage` is a plain copy when
`finalBlitToCameraTarget` is off (PP/PostProcessLayer.cs:171-181).

### Non-PPv2 image effects on WVCamera

`VerticalTint` (AC/VerticalTint.cs): `OnPreRender` uploads camera matrices (AC/VerticalTint.cs:257-341),
`[ImageEffectOpaque] OnRenderImage` blits one full-screen pass (AC/VerticalTint.cs:343-350) that
rebuilds world position from depth and draws height tint, clouds, fog of war, realm hatching, SDF borders
and two fog layers. `GameMode` updates its fog distances from zoom (AC/GameMode.cs:533-539).

## 3. UI

- Every root Canvas in every file is **Screen Space Overlay** (48 roots). No ScreenSpaceCamera canvases;
  no `worldCamera` set. The 25 "WorldSpace" canvases found are nested sub-canvases (mode ignored).
- The only true world-space root canvases are the battle `squad_icon` prefabs in `units` (drawn by
  BVCamera, inferred).
- Status bars, army/battle icons and PV figures are overlay UI under `id_StatusBars`, placed each frame by
  projecting world points through the game camera: `WorldToScreenObject` uses
  `cam.nonJitteredProjectionMatrix * cam.worldToCameraMatrix` and **`cam.pixelRect`**
  (AC/WorldToScreenObject.cs:395-447, 577-592); `UICommon.WorldToScreen` /
  `Screen2World` likewise use `pixelRect` / `pixelWidth` (AC/UICommon.cs:743-790). Subclasses:
  `UIArmyStatusBar`, `UIBattleStatusBar`, `UIFortificationHealthbar`, `UIBattleViewSquad`, `UIPVFigure`.
- Mouse picking uses `MainCamera.ScreenPointToRay(Input.mousePosition)` (AC/BaseUI.cs:2177, 2194, 3920;
  AC/GameCamera.cs:410, 441) and `UICommon.Screen2World(Input.mousePosition..., camera)`
  (AC/BaseUI.cs:1754).
- 3D text drawn by the game camera (so it would be rendered at the low resolution):
  - settlement labels: `SettlementLabel` template with `BillBoard`, layer `Labels`
    (AC/Settlement.cs:538-550), drawn by `WVCamera` (mask Everything);
  - realm/kingdom labels: `TextMeshPro` objects on layer `Political` (AC/LabelUpdater.cs:297, 710-744),
    drawn by whichever game camera includes layer 16 (WV mask Everything, PV mask UI+Political);
  - `FloatingText` (TMP 3D, AC/FloatingText.cs) with its own motion-vector draw (section 6).
- Selection rings/outlines in battles are instanced draws through the battle camera
  (`TextureBaker.Instanced*Drawer`, AC/TextureBaker.cs:409, 788).

## 4. World-view (WV) draw path

All WV batchers hook the static `Camera.onPreCull` delegate and issue
`Graphics.DrawMeshInstancedIndirect(..., camera: cam)` after a compute-shader cull, so the draws join that
camera's normal queues (no CommandBuffer, no CameraEvent, no DrawProcedural).

| System | Hook | Camera filter | Compute kernel | Shader (data) | Layer |
|---|---|---|---|---|---|
| Roads: `LinesBatching` on `Roads` (`AutoRoads`) | AC/LinesBatching.cs:244-249 | `cam == CameraController.MainCamera` or `cam.name == "SceneCamera"` (AC/LinesBatching.cs:207) | `WVCullLines` (WVLinesCulling) + bitonic sort | `BSG/Instanced/RoadsInstanced`: Deferred pass, ZWrite off | 12 Decals |
| Borders: `BordersBatching : LinesBatching` (`_wv_kingdom_borders`, `_wv_realm_borders`, `_pv_*`, `_h_*`) | AC/BordersBatching.cs:71-102 | same as roads | `WVCullLines` | WV kingdom `BSG/Instanced/KingdomBorderInstanced`, others `BSG/Instanced/LinesInstanced`: forward, Transparent, ZWrite off | 12 (WV) / 16 (PV, H) |
| Trees: `TreesBatching` on `Terrain` (~488k trees, wind off) | AC/TreesBatching.cs:321 | same (AC/TreesBatching.cs:426) | `WVCullTrees` (WVTreeCulling), per LOD | `BSG/WV_Trees` (Forward, Deferred, ShadowCaster); `BSG/SpeedTree8`, `BSG/GrassShader` by type (AC/TreesBatching.cs:818-850) | 27 Vegetation |
| Terrain, Medium/Low quality: `BSGTerrain` | AC/BSGTerrain.cs:439 | `cullingMask` has layer 8 and name has no "Ocean" (AC/BSGTerrain.cs:219) | Burst job cull | `Graphics.DrawMeshInstanced` (AC/BSGTerrain.cs:280) | 8 |
| Terrain, High quality | Unity `Terrain` (`drawHeightmap`), `PARALLAX_EFFECT` keyword (AC/TerrainQualityManager.cs:98-125) | normal culling | | material template `Nature/Terrain/Standard`; `BSG/Terrain/WorldView` exists (runtime swap inferred) | 8 |
| Settlements (battles only): `GeometryBatching` | AC/GeometryBatching.cs:121 | `cam == MainCamera` and name is not "SceneCamera" (AC/GeometryBatching.cs:292) | `BSGGeometryCull` | per model | 9 |

Depth: trees and terrain are opaque deferred and write depth (inferred from their Deferred/ShadowCaster
passes). Roads are a ZWrite-off deferred decal; borders are ZWrite-off transparents; `BSG/RiverSmall` is
ZWrite off; the ocean is `CalmWater/Calm Water [DX11]` with a GrabPass and `ForceNoMotion`. Shader
source is not shipped; ZWrite/ZTest values driven by material properties were not resolved.

Per-frame GPU readback: roads have `BitonicCS` set, so `LinesBatching.Draw` calls
`arg_buf.GetData()` every frame for the main camera (AC/LinesBatching.cs:229-238), a CPU-GPU sync point.

## 5. Command buffers, image effects, screen-sized targets

On `WVCamera`:
- PPv2: CBs at `BeforeReflections`, `BeforeLighting`, `BeforeImageEffectsOpaque`, `BeforeImageEffects`
  (PP/PostProcessLayer.cs:151-154); `OnRenderImage` copy (PP/PostProcessLayer.cs:171-181).
- `CustomMotionVectors`: CB at `BeforeImageEffectsOpaque`, renders into
  `BuiltinRenderTextureType.MotionVectors` (AC/CustomMotionVectors.cs:64-127).
- `VerticalTint`: `OnPreRender` + `[ImageEffectOpaque] OnRenderImage` Blit (AC/VerticalTint.cs:257-350).
- `CTAA_PC`: `OnPreCull` jitter, `OnRenderImage` Blits (AC/CTAA_PC.cs:382-524).

On `BVCamera` additionally:
- `EmissionRenderer`: CB at `BeforeGBuffer`, RT sized from `camera.pixelWidth/Height`
  (AC/EmissionRenderer.cs:101-120, 142).
- `SSRT` when enabled: CBs at `BeforeImageEffectsOpaque`, `BeforeLighting`, `BeforeGBuffer`
  (AC/SSRT.cs:392-394); RTs from `cam.pixelWidth/Height` (AC/SSRT.cs:288-314, 372).

Screen-size assumptions found:
- `CTAA_PC` sharpen texel size from `Screen.width/height` (AC/CTAA_PC.cs:471-472); `ExtendedFeatures`
  target from `Screen.width` (AC/CTAA_PC.cs:313-324).
- `UIHostMigrationBlocker` screenshot RT `Screen.width x Screen.height` (AC/UIHostMigrationBlocker.cs:68);
  `GPUBenchmark` RT (autodetect only).
- `GameCamera.GetDefaultLookAtPt` uses `Screen.width/2` (AC/GameCamera.cs:197); edge scroll uses
  `Screen.width/height` (AC/GameMode.cs:575-615).

Present in code but not attached to any camera in the shipped data: `Kino.Obscurance`,
`DepthCullController`, `CommandBufferStencil` (uses `Camera.main`), `UnderWaterFog`, `ShaderControl.Effect`.
`StylizedFog` and `GlobalFog` exist only on an inactive object in one battle scene.

## 6. depthTextureMode and motion vectors

- `UserSettings.UpdateCameraDepthMode()` (AC/UserSettings.cs:1484-1506) on every game camera: adds
  `DepthNormals` when SSAO is on; adds `MotionVectors` when AA is TAA or SSRT is not None, otherwise
  removes it two frames later (`CameraController.DisableCameraDepthMode`, AC/CameraController.cs:269-281).
- `CTAA_PC.OnEnable` adds `Depth | MotionVectors` (AC/CTAA_PC.cs:175-177); `SSRT` adds
  `Depth | MotionVectors` (AC/SSRT.cs:345); PPv2 effects request their own flags each frame (SC Fog:
  Depth).
- Motion-vector producers:
  - Unity's built-in camera motion pass (`Hidden/Internal-MotionVectors` in GraphicsSettings) for
    depth-writing geometry, plus per-object vectors for moving renderers (inferred, standard built-in
    behaviour).
  - `CustomMotionVectors` CB adds: instanced battle units with a `MotionVectors` pass
    (AC/TextureBaker.cs:772-778) and `FloatingText` via `Hidden/TextMeshPro/Distance Field MotionVectors`
    (AC/FloatingText.cs:56-66). Both only when the camera has `MotionVectors` set.
  - Shaders with a `MotionVectors` LightMode pass: only `BSG/ArmySelection`, `BSG/DeferredDecal`,
    `BSG/Instanced/UnitTintedBatched`. No world-map shader has one.
- Consumers: CTAA (world map), PPv2 TAA (battles), SSRT (battles, off by default).

## 7. Loading a save from code

Save layout: `Game.GetSavesRootDir(SavesRoot.Single)` = `<persistentDataPath>/Saves/SinglePlayer`
(`Saves2` for project "Kings2"), FP/Logic/Game.cs:5853-5880; campaign folder = campaign id
(FP/Logic/Campaign.cs:1431-1440); each slot folder holds `savegame.txt`, `world.txt`, `radio.txt`.

Menu chain:
1. `UILoadGameWindow.OnLoadGame()` (AC/UILoadGameWindow.cs:795-850): `SaveGame.UpdateList(...)`,
   `SaveGame.FindById(id)`, then from the main menu `game.load_game = Game.LoadedGameType.LoadFromMainMenu;
   game.StartGame(new_game: false, null, sg.fullPath)` (in game: `LoadFromInGameMenu` after a confirm box).
2. `Logic.Game.StartGame` -> `Logic.Multiplayer.StartGame` (FP/Logic/Game.cs:7159-7175,
   FP/Logic/Multiplayer.cs:981-1026): returns early if `state == LoadingMap`; sets `LoadingMap`, then a
   coroutine posts "loading_saved_game", "show_loading_screen" and a delayed "game_started" with
   `[new_game, map_name, fullPath]`.
3. `Multiplayer.OnMessage("game_started")` (AC/Multiplayer.cs:43-110): `SaveGame.GetValidLoadInfo(path)`,
   loads the `Campaign` if needed, then `SaveGame.Load(path)`.
4. `SaveGame.Load(fullPath)` (AC/SaveGame.cs:1095-1163): reads `savegame.txt`, sets
   `Game.isLoadingSaveGame`, `game.UnloadMap()`, and if the map scene differs calls
   `GameLogic.LoadScene(key)` (async scene load, AC/GameLogic.cs:180-205) and returns.
5. `WorldMap.Start()` -> `SaveGame.FinishLoading()` (AC/WorldMap.cs:833-845, AC/SaveGame.cs:1180-1258):
   reads `world.txt`, loads objects, `WorldUI.Load` (restores the 2D `cam_pos` look-at point, not zoom,
   AC/WorldUI.cs:200-220), logs "Game loaded", `OnLoadingFinished()` -> `game.OnFullGameStateReceived`.
6. "full_game_state_received" starts `HideLoadingScreenCoro` (AC/Multiplayer.cs:267-330): waits for the
   player's kingdom/royal family and `CameraController.IsReady()`, centres on the capital unless the save
   restored a camera position, then hides the loading screen.

Plugin call (main thread, title screen, `game.state != LoadingMap`):
`SaveGame.ScanSavesDir(forced: true)`; pick the `SaveGame.Info` from `SaveGame.list` after
`SaveGame.UpdateList(true, true)` whose `campaign_id == <guid>` and folder name is the slot (paths are
`DirectoryInfo.FullName` strings, Windows-style under Proton, so match by parts, not by a built string);
then `game.load_game = LoadedGameType.LoadFromMainMenu; game.StartGame(false, null, info.fullPath)` with
`game = GameLogic.Get(false)`. `SaveGame.Load(path)` alone skips the campaign setup in step 3.
`SaveGame.CanLoad` refuses during battles or while a save is writing (AC/SaveGame.cs:640-676).

"World map loaded and interactive" check: `!LoadingScreen.IsShown()` (AC/LoadingScreen.cs:126-133) and
`!Game.isLoadingSaveGame` and `game.state == Game.State.Running` (set at the end of `LoadMap`,
FP/Logic/Game.cs:7347, so not sufficient alone) and `CameraController.IsReady()`
(AC/CameraController.cs:79-86). Waiting a few extra frames for streaming and the first tree/road rebuild
(`do_rebuild`) is advisable (inferred).

## 8. Moving the world-map camera

- Controller: `GameCamera` (AC/GameCamera.cs) with an `ICameraControllScheme`; the world map uses
  `GameMode : CameraMode` (AC/GameMode.cs). State: `ptLookAt` (public, AC/CameraMode.cs:28),
  `cam_dist` / `cam_dist_smooth` (protected, AC/CameraMode.cs:40-42), `cam_pitch` and `cam_yaw`
  (private, AC/GameMode.cs:11, 23). Pitch is derived from distance every frame:
  `RecalcCameraPitch()` lerps `Settings.pitch` over `Settings.dist` (AC/GameMode.cs:494-503).
- Entry points: `CameraController.LookAt/Set` (all cameras, AC/CameraController.cs:172-215),
  `GameCamera.LookAt(pt, force_update)`, `GameCamera.Set(pos, rot, force_update)`
  (AC/GameCamera.cs:256-306). `GameMode.Set` derives `cam_dist`, `ptLookAt`, `cam_yaw` from a pose
  (AC/GameMode.cs:115-126); `Zoom()`/`Yaw()` are no-ops in `GameMode` (AC/GameMode.cs:107-113).
  `CalcPositionAndRotations(lookAt, zoom01)` gives a pose for a zoom fraction (AC/GameMode.cs:648-660).
- `GameCamera.LockUserInput(true)` stops mouse/keyboard/edge-scroll input; `Lock(true)` stops the whole
  update (AC/GameCamera.cs:345-353).
- Built-in benchmark: `EuropeBenchmark` (AC/EuropeBenchmark.cs) is in the europe scene, with a
  `CameraPresets` asset ("Camera presets", `AssetBundles/europe`) holding 14 poses.
  `EuropeBenchmark.Instance.Benchmark(callback)` adds a performer to the game camera that applies each
  preset for 300 frames via `SetScheme("FreeLook")` + `CameraController.Set` (AC/CameraPresets.cs:31-35)
  and times OnPreCull->OnRenderImage. Console command `benchmark europe` needs cheat level High
  (AC/DevCheats.cs:11686-11705). Scene `Camera Paths/*` objects (`CameraPath`) also exist for fly-bys.

## 9. Shaders and MotionVectors passes

374 unique shaders across the asset files. Only `BSG/ArmySelection`, `BSG/DeferredDecal` and
`BSG/Instanced/UnitTintedBatched` have a `MotionVectors` LightMode pass; everything else relies on
Unity's internal motion-vector shader. Compute shaders and kernels: WVLinesCulling (`WVCullLines`),
WVTreeCulling (`WVCullTrees`), BitonicSortLines (`Sort`), GeomCulling (`BSGGeometryCull`), BorderSDF /
SDFGenerator / TownSDFGenerator (`GenerateSDF`), plus stock PPv2. GrabPass shaders: CalmWater (world
ocean), NatureManufacture rivers, FX/Water4.

## Risks for a low-resolution 3D render with native UI

1. **`pixelRect` coupling.** Giving `WVCamera` a smaller `targetTexture` changes `pixelRect`, which the
   overlay status bars (`WorldToScreenObject`, `UICommon.WorldToScreen`) and all mouse picking
   (`ScreenPointToRay(Input.mousePosition)`, `Screen2World`) read outside rendering. The WV batchers and
   battle troops only draw for `CameraController.MainCamera` (roads/trees also accept a camera named
   `SceneCamera`; GeometryBatching rejects it), so a separate render camera loses roads, borders and
   trees. The low-res target has to live on the same Camera only for the duration of its render, or the
   call sites need patching (inferred design consequence).
2. **No usable jitter or complete motion vectors.** Shipped CTAA jitter is effectively constant;
   PPv2 and CTAA both reset the projection in `OnPreCull`, so a plugin jitter must run after them and keep
   `nonJitteredProjectionMatrix`. Roads, borders, rivers and ocean are ZWrite-off (no depth of their own,
   GrabPass ocean), `VerticalTint` draws fog/clouds/SDF borders as a full-screen pass, and 3D labels
   (settlement/realm TMP text) are rendered by the game camera, so they would be upscaled and may ghost.
3. **Where post-processing sits.** PPv2 (bloom, DoF, vignette, ACES grading, SC Fog) runs inside the
   camera at its render size, and CTAA runs after it on the graded image; FSR either replaces CTAA after
   the stack (upscaling an LDR, tonemapped image) or needs the stack split around it. Unity's dynamic
   resolution is not an option on DX11 in 2019.4 (inferred), and the per-frame road readback
   (`arg_buf.GetData`) can keep the frame CPU-bound and mute GPU savings.
