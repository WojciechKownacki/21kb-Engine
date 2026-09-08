# UI authoring verification ? 2026-09-08

The audit runs the real `kb_editor.exe` through `--selftest-scenario`. It does not launch the interactive editor. `HeadlessUIComponentCatalogScenario.json` has a separate assertion for each registered component. Lua menu coverage is additional, not a substitute for editor control tests.

The component cases locate the live Inspector controls, change and restore accepted editable values, inspect the scene layout, and save Inspector and GPU captures. Color and enum edits use the editor command handlers after hit testing; scalar, text and checkbox edits use pointer/text controllers. This checks authoring and rendering, not every possible gameplay interaction or layout combination.

The graphics cases import PNG, JPEG, BMP and a two-frame GIF. For Image, Raw Image and Sprite, they search and click the actual hidden native texture picker, accept the choice, check its thumbnail, assign it, and read red/green pixels from the 2D and 3D scene captures. The 3D case is a screen-space overlay above mesh geometry, not a world-space widget. PNG also checks alpha. They reopen the picker to check retained selection, cancellation and Clear, then undo the edits and creation. GIF coverage verifies its first frame; there is no animated-GIF playback contract.

Fixed during this audit:

- Texture thumbnails now retain the source aspect ratio and composite alpha over a checkerboard. The existing scaled preview cache remains the owner of cached thumbnails.
- The texture picker preserves its current selection and provides Clear in the existing search/action row.
- Headless rendering registers the same scene-release callbacks as the application, so scene reloads cannot leave the renderer referring to a destroyed scene.
- Renderer scene release is safe before its first frame and after shutdown; opening a scene before rendering is covered by the anchors scenario and the targeted renderer test.
- Graphics capture does not create an unrelated secondary viewport. The dedicated secondary-viewport test retains that separate check.

| Component | Properties changed and restored |
| --- | ---: |
| Rect Transform | 14 |
| Canvas | 2 |
| Canvas Scaler | 5 |
| Canvas Group | 4 |
| Horizontal Layout | 11 |
| Vertical Layout | 11 |
| Grid Layout | 10 |
| Wrap Layout | 8 |
| Overlay Layout | 6 |
| Layout Element | 8 |
| Content Size Fitter | 2 |
| Aspect Ratio Fitter | 2 |
| Sprite | 5 |
| Image | 14 |
| Raw Image | 8 |
| Text | 11 |
| Border | 17 |
| Mask | 1 |
| Shadow | 7 |
| Outline | 5 |
| Background Blur | 1 |
| Selectable | 25 |
| Button | 1 |
| Toggle | 1 |
| Slider | 4 |
| Scrollbar | 3 |
| Scroll View | 6 |
| Input Field | 3 |
| Dropdown | 1 |
| Progress Bar | 2 |
| Widget Switcher | 1 |

Total: 31 component cases and 199 property edits, each restored through the editor. Asset fields have the separate native-picker cases. Entity references require valid target entities and are not counted as generic scalar edits.

| Priority | Confirmed gap | Existing owner/evidence | Complete behavior required before exposing a new control |
| --- | --- | --- | --- |
| 1 | Dropdown cycles to the next child instead of opening a list | `sources/engine/src/scene/SceneUI.cpp`, dropdown branch in the interaction path; `UIDropdown` stores only `selectedIndex` | Open/close popup, select any item, click outside/Escape to close, keyboard/gamepad navigation, clipping, selection event and editor-managed options |
| 2 | Explicit navigation references appear as numeric entity IDs | `UISelectable` navigation references and the Entity branch in `InspectorUIComponentModel` | Pick a hierarchy object, display its name, clear the link, validate deletion and cross-scene references, Undo/Redo |
| 3 | No native image size action in the UI Inspector | `PaintUIComponentSections`; image properties expose UV, scaling and tint | Set width/height from the selected texture while preserving anchors/pivot, clear errors for missing assets, one Undo command |
| 4 | No visual timeline dedicated to widget properties | UI authoring/inspection owners; `MenuController.lua` animates properties from Tick; general Timeline assets open in the Script Editor | Key position/scale/color/opacity, scrub and preview, easing, loops and event playback, save/load and Undo |
| 5 | GIF imports as a still texture | `RenderTextureAssetLoader` decodes one image through the image decoder; UI image components have no frame timing data | Frame durations, looping, disposal/transparency, deterministic playback and pause, bounded resource lifetime and packaging support |
| 6 | Canvas has no world-space render mode | `UICanvas` exposes sorting order and pixel-perfect settings; `SceneUI` builds pixel-space layouts | Canvas plane in world coordinates, camera projection, depth/occlusion, resolution/density controls, ray-based interaction and 2D editing of its contents |

Existing mechanisms worth retaining: UI presets, Canvas scaling and device preview, anchor presets and direct 2D handles, Image Contain/Cover and nine-slice, palette colors, layout containers, masking, effects, event names and scene prefabs. New authoring controls should use these owners rather than introduce parallel systems.

Validation targets: `kb_editor`, `kb_renderer_tests`; targeted renderer CTest `kb_renderer_editor_ui_view`; CTest `kb_editor_ui_component_catalog_headless`, `kb_editor_headless_automation_scenario`, `kb_editor_user_widget_headless`, `kb_editor_ui_anchors_headless`, `kb_editor_particle_authoring_headless`. Reports and screenshots are emitted under `build/SelfTest/<task>/automation/`; generated files are not committed.

Verification outcome: all six targeted CTests passed in their latest relevant runs. The catalog recorded 31 component cases, 199 changed/restored properties, 12 graphics cases and 55 GPU captures. Restoring the old thumbnail implementation deliberately produced `FAIL: Image-AuditPNG: thumbnail aspect ratio or alpha incorrect`; the fixed implementation was restored and rebuilt.
