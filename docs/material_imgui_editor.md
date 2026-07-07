# Material ImGui Editor

The `1.1-material-imgui` branch introduces an experimental Dear ImGui based
node-editor path for the Material Editor.

## Dependency

- `thedmd/imgui-node-editor`
- Local path: `third_party/imgui-node-editor`
- License: MIT
- License notice: `third_party/imgui-node-editor/LICENSE`
- Dear ImGui is consumed from `third_party/imgui` under its MIT license.

The dependency is added as a git submodule. The root material graph document
remains the engine-owned `RenderMaterialGraphDocument`; the ImGui node editor
is a view/controller layer over that data, not a new material format.

## Current Integration

- `kb_imgui_node_editor` builds Dear ImGui plus `imgui-node-editor` sources.
- `MaterialImguiNodeEditorModel` exports graph nodes, pins and links as
  `ax::NodeEditor::NodeId`, `PinId` and `LinkId`.
- `MaterialImguiNodeEditorRenderer` owns an `ax::NodeEditor::EditorContext`,
  submits nodes/pins/links to the ImGui node editor, applies the dark material
  graph style, and reports accepted link-create/link-delete interactions.
- Pin colors and node header colors are derived from existing material graph
  types, so the visual language stays consistent with the current material
  compiler/runtime.
- `KBMAT-IMGUI-0001` locks the mapping with a TextureSample -> NormalUnpack ->
  MaterialOutput fixture.

## Next Step

The remaining work is the runtime UI host: start an ImGui frame in the Win32
editor surface and map renderer frame results back into `MaterialEditorState`
commands.
