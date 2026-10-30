# Editor and packaged player: the same picture

`kb_editor_scene3d_parity_packaged` checks that a 3D scene drawn by the editor's Play view and by the packaged player looks the
same. It is one representative scene, not a promise for every scene or every graphics backend (see Limits).

`kb_editor_native_image_regression` extends that fixture with scripted cube motion, three capture times and reviewed images
for each locally supported native backend. See [Native image regression](renderer_image_regression.md) for execution,
reference review, artifacts and package isolation.

## What it does

1. The editor runs `sources/editor/tests/Scene3DParityScenario.json` headless: it builds a scene (a sun with shadows, a point light, a
   sky and ambient light, a floor, a cube, a metal sphere, an emissive capsule, a particle effect, a terrain and a post-process profile
   with FXAA, vignette, fog, ambient occlusion and the default auto exposure), saves it, reopens it, plays 240 fixed steps and
   captures the Play view at 960 by 540.
2. `kb_cooker` cooks the project into a pack, with the runtime modules, and the packaged player (`kb_game`) is copied next to it with
   nothing else: no source assets and no editor.
3. The player runs the pack headless at 960 by 540 and writes a screenshot of frame 240.
4. `scripts/compare_images.py` compares the two pictures: the mean channel difference must stay under 2.0 (of 255) and fewer than 0.2%
   of the pixels may differ by more than 12 in any channel.

The measured difference at frame 240 is a mean of 0.007 with 0.01% of pixels over the tolerance.

## What the check found

- With **fixed exposure** the two pictures agree at once (mean 0.006 at frame 200). Shadows, lighting, materials, sky, fog, ambient
  occlusion, vignette, FXAA, the particle effect and the terrain are drawn the same by both.
- With **auto exposure** (the default), headless editor rendering previously used elapsed wall time while scene simulation and the
  packaged player used a fixed timestep. Waiting 2400 frames masked that clock mismatch and could exceed the CI timeout. At frame
  240, the mean difference was 7.102 before synchronizing the clocks and 0.007 afterward, with unchanged comparison thresholds.
  Runtime steps now advance renderer effects by the simulation timestep; capture redraws do not advance their clock. Interactive
  editor paints still use elapsed wall time. The test keeps auto exposure enabled and compares both images after 240 fixed steps.
- The scene uses FXAA. Temporal anti-aliasing (jitter and history) was not compared.

## Limits

- Windows x64 and Direct3D 11 on the machine that ran it. Other backends and platforms are not compared.
- Animation: the scene has no animated mesh (the editor scenario has no skeletal fixture); a skinned character is not compared.
- The terrain is authored with the Terrain Editor plugin, which is switched off before the cook (it is an editor plugin, and the
  cooker asks for a runtime module of every plugin that is on); the terrain asset itself is cooked and drawn.
- The particle effect is the default effect. Comparison limits cover the entire image; no particle region is masked.
- It compares pictures, not performance or memory.
