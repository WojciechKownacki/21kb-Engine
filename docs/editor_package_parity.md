# Editor and packaged player: the same picture

`kb_editor_scene3d_parity_packaged` checks that a 3D scene drawn by the editor's Play view and by the packaged player looks the
same. It is one representative scene, not a promise for every scene or every graphics backend (see Limits).

## What it does

1. The editor runs `sources/editor/tests/Scene3DParityScenario.json` headless: it builds a scene (a sun with shadows, a point light, a
   sky and ambient light, a floor, a cube, a metal sphere, an emissive capsule, a particle effect, a terrain and a post-process profile
   with FXAA, vignette, fog, ambient occlusion and the default auto exposure), saves it, reopens it, plays 2400 fixed steps and
   captures the Play view at 960 by 540.
2. `kb_cooker` cooks the project into a pack, with the runtime modules, and the packaged player (`kb_game`) is copied next to it with
   nothing else: no source assets and no editor.
3. The player runs the pack headless at 960 by 540 and writes a screenshot of frame 2400.
4. `scripts/compare_images.py` compares the two pictures: the mean channel difference must stay under 2.0 (of 255) and fewer than 0.2%
   of the pixels may differ by more than 12 in any channel.

The measured difference of the committed scene is a mean of 0.06 with 0.01% of pixels over the tolerance (the particle glow).

## What the check found

- With **fixed exposure** the two pictures agree at once (mean 0.006 at frame 200). Shadows, lighting, materials, sky, fog, ambient
  occlusion, vignette, FXAA, the particle effect and the terrain are drawn the same by both.
- With **auto exposure** (the default) the pictures differ until the exposure has settled: the player reads the scene's brightness
  back from the GPU every 60 frames, so the exposure approaches its value in steps. Measured against the editor at the same frame
  count (with earlier variants of this scene): mean difference 45 at frame 60, 34 at frame 200, 16 at frame 700, 0.8 at frame 2400 and 0.06 with the committed scene. A
  comparison that takes both pictures early would call a correct package wrong; the test settles first, and a scene that must look
  the same at once should use manual exposure.
- The scene uses FXAA. Temporal anti-aliasing (jitter and history) was not compared.

## Limits

- Windows x64 and Direct3D 11 on the machine that ran it. Other backends and platforms are not compared.
- Animation: the scene has no animated mesh (the editor scenario has no skeletal fixture); a skinned character is not compared.
- The terrain is authored with the Terrain Editor plugin, which is switched off before the cook (it is an editor plugin, and the
  cooker asks for a runtime module of every plugin that is on); the terrain asset itself is cooked and drawn.
- The particle effect is the default effect; its motion is time-driven, so it is the only part of the picture allowed to differ.
- It compares pictures, not performance or memory.
