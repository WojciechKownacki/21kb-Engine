# bgfx patches

`third_party/bgfx.cmake` is upstream bgfx.cmake, bgfx, bx and bimg plus these patches, nothing else.

- Update: `python scripts/update_bgfx.py` (newest master; `--bgfx <full commit hash>` and friends pin one).
  It replaces the tree, applies the patches in order and records the versions in
  `third_party/third_party_manifest.json` and `THIRD_PARTY_LICENSES.md`.
- Change bgfx: edit the tree, then add or rewrite a patch here, made with
  `git diff --relative=third_party/bgfx.cmake`. An edit with no patch is lost on the next update.
- After an update: regenerate the renderer shaders (`-DKB_GENERATE_RENDERER_SHADERS=ON`), build and test.
- Drop a patch once upstream has the same fix.

The GLSL compiler honors explicit `-O 0` by preserving function boundaries.
`bgfx_compile_shaders(OPTIMIZATION 0 ...)` exposes that choice to shader targets.
The deferred ESSL shader uses it to avoid a downstream browser compiler crash
with the fully inlined program; other release shaders retain their usual passes.
