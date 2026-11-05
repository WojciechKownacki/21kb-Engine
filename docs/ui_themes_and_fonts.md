# Shared UI themes and scalable text

Create a **UI Theme** in the Project Files menu, then select the asset to edit
its base style in the Inspector. The variant menu selects an existing variant;
**New variant** creates one. Variants inherit properties from the base style.
Colors and asset references have pickers. Enter `@inherit` in a text or numeric
field to remove that property from the selected style.

Assign the theme asset and optional variant on each control's Rect Transform.
A theme supplies visual properties of components already present on the control;
it does not add components or replace text content. Editing a themed control's
property creates a local override. Other controls continue following the shared
style. Theme edits save the asset and support Undo/Redo. Referenced fonts and
images are included in asset dependency validation and cooking.

Text defaults to bitmap rendering. Select **Signed Distance Field** in the Text
component to opt into raster SDF rendering. Shaping, wrapping, line metrics and
caret positions still use the requested text size. The distance field uses a
64-pixel raster of the selected font instance, including variation axes, and is
generated from the font payload at runtime in both source projects and packages.
This mode uses a single-channel SDF. For outline-derived RGB MSDF, set
`UIText::fontRenderMode` to `UITextFontRenderMode::MultiChannelSignedDistanceField`
(serialized value `2`). The runtime generates the MSDF from the same font payload
and uses the median of its three channels for text coverage. C++ and Lua share
the rendering backend; see `kb_cli api --topic RuntimeUI` for authoring examples.
The Windows D3D11 editor and source-free package workflow is covered by
`kb_editor_runtime_ui_package`; this does not qualify other hosts.

The renderer falls back to bitmap text when the optional SDF shader is absent,
the field cannot represent a glyph, or an outline/shadow exceeds the field's
padding range. The supported effect range is approximately seven raster pixels,
scaled by the requested font size divided by 64. Antialiasing follows output
pixel density; the authored outline width follows the control's scale.

Scene format 58 stores the rendering mode; format 59 adds the theme binding,
variant and local override list. Older scene versions default to bitmap text and
no theme binding.
