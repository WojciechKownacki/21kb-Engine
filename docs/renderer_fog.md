# Fog profiles and image contract

`ScenePostProcessSettings` and `SceneRenderLightingConfig` expose `fogProfile`:

| Profile | Height extinction | Requested volumetric scattering |
| --- | --- | --- |
| `Automatic` | Analytic | Resolves to Web on browser builds, Mobile on mobile builds/OpenGL ES, Desktop otherwise |
| `Desktop` | Analytic | 24 jittered steps over the first 100 metres, current scene lights and shadow maps |
| `Mobile` | Analytic | Falls back to height fog; no light/shadow ray march |
| `Web` | Analytic | Falls back to height fog; no light/shadow ray march |

Mobile and browser builds cap the profile to their target policy, even when the asset requests Desktop.
Desktop builds can select either lower-cost profile for previews. These are **fog quality profiles**:
opaque surfaces and background still use deferred lighting and require its G-buffer capabilities.
They do not remove the G-buffer bandwidth cost or make unsupported render targets available.
`SceneRenderSubmitStats.fogProfile` reports the resolved enum value; `fogMode` is off (0), height (1),
or height plus volumetric scattering (2). The authored volumetric setting remains in the asset so a
quality change does not destroy it.

The profile field is available in the post-process Inspector, its Undo/Redo history, `.kbppfx`
save/load, and `kb_cli post-process-set --set fogProfile=Mobile`. Invalid profile names are rejected.
Existing files default to Automatic. Cooking preserves the authored profile and its parameters.

## Extinction, light and transparency

Height density is `density * exp(-falloff * (height - baseHeight))`. The shared `height_fog.sh`
integrates this along the camera ray. It evaluates the exponential from the denser endpoint,
avoiding overflow multiplied by underflow on long downward rays. Zero falloff gives uniform fog;
zero density changes nothing. The renderer rebases the authored base height with the render origin.
Exponent and optical-depth bounds keep extreme inputs finite. Density/falloff are capped at 10000
for submission; anisotropy is bounded to -0.9 through 0.9.

The volumetric march integrates each segment's extinction as `1 - exp(-density * stepLength)`.
It does not inject arbitrarily more light as a segment becomes opaque. Light intensity, range,
cone, volumetric-scattering weight and shadow visibility determine its scattering contribution.
The Henyey–Greenstein phase uses positive anisotropy for forward scattering.

Ordinary lit forward meshes and graph surfaces, including alpha-blended surfaces, apply height
fog before blending. Their alpha is unchanged. They use this approximation even in the Desktop
profile: no separate volumetric march is performed for each transparent layer. Unlit graph surfaces
remain exempt, matching deferred unlit behavior. Refractive/water graph models sample the already
fogged scene color; this is not a volumetric transmission model. Particles and projected decals
retain their separate shading policies.

Fog owns no temporal history, so a camera cut, off/on transition or viewport resize cannot reuse a
fog texture from an earlier frame. Optional TAA after lighting has its own reset contract.

## Verification entry points

`kb_renderer_tests fog-profiles --backend d3d11|d3d12|vulkan|opengl` compares actual readback images:
Mobile/Web against Desktop height-only fog, transparent and opaque surfaces, camera motion/cuts,
off/on transitions, resize against a freshly allocated target, anisotropy and a high camera.
A closed dark room verifies zero illumination without a light, point-light scattering, and its
reduction behind an occluder after subtracting the directly lit surface contribution.
Images are written under the test working directory's `fog_profile_tests` folder.
`screen-space` retains the Beer–Lambert distance, density and height controls.

`PostProcessValidationWorkflowTests.py` drives profile selection through the real Inspector and CLI.
`RendererImageWorkflowTests.py --capture-only --fog-profile Web` authors a volumetric request with
the Web fallback, saves/reopens, cooks and compares the editor with an isolated package. Capture-only
does not mean approval against the default Scene3D goldens; the package comparisons still enforce
their pixel limits and the missing-geometry negative control.

Desktop execution of Mobile/Web policies verifies their fallback images, not a physical mobile GPU
or browser runtime. ESSL compilation is a separate check; target-device performance and driver
behavior require target execution.
