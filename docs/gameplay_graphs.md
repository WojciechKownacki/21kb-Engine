# Gameplay graphs

Gameplay graphs are `.kbgraph` behaviour assets. Create one with **New Gameplay
Graph** in Project Files and open it to show the Gameplay Graph dock panel.
A new graph starts with a Tick event.

Use **Add node** to search the live node catalog. It includes lifecycle and flow
nodes, typed literals, the current entity and registered script API functions.
Choose a lifecycle name when adding another Event node. Binding nodes retain
their registered pin signatures; replace the node to change its function.

Click an output pin and then an input pin to connect them. Execution pins carry
flow; data pins carry typed values. The document rejects incompatible types,
occupied data inputs and cycles. Select a wire and use **Disconnect** to remove
it. Deleting a node also removes its pins and connections.

Drag a node to move it, or drag empty canvas to pan. Fit and zoom controls adjust
the view. One Undo restores an entire drag. **Edit value** changes a literal,
lifecycle event or custom event/comment name. Literal text is checked against
its type; finite numeric values and integer bounds are enforced. Runtime entity
and component handles come from live bindings and cannot be stored as literals.

**Save** publishes the document atomically. Canvas positions are stored as
comments, so they do not alter runtime graph semantics. Undo, Redo and Revert
operate on the working document. Opening another graph refuses to discard
unsaved edits. Closing the panel or changing scenes uses lifecycle prompts.

Select a scene entity and choose **Run event** to execute the selected lifecycle
event in a copied scene. This event preview does not change the authored scene
or publish unsaved graph edits. **Breakpoint** toggles a breakpoint on the
selected execution node. **Step** executes one flow node and evaluates its data
dependencies atomically; produced input/output values appear in the sidebar.
Pure data nodes are inspected through their values rather than separate
breakpoints. The panel highlights visited nodes and reports the paused node.
Editing a graph discards its previous preview.

Use **Attach selected** to assign the graph to the hierarchy selection. Save the
scene, then use regular Play or cook the project to exercise the full behaviour
lifecycle and event bridge. The isolated event preview is a debugger for a
selected event, not a replacement for scene Play.

The CLI uses the same validated authoring document:

```text
kb_cli graph new --project <project> --graph /Game/Motion.kbgraph
kb_cli graph catalog --project <project> --filter Transform.Translate
kb_cli graph add --project <project> --graph /Game/Motion.kbgraph --entry Literal:Float --x 32 --y 240
kb_cli graph literal --project <project> --graph /Game/Motion.kbgraph --node 2 --value 0.01
kb_cli graph connect --project <project> --graph /Game/Motion.kbgraph --from <source> --out <output> --to <target> --in <input>
kb_cli graph check --project <project> --graph /Game/Motion.kbgraph
```

Other operations are `show`, `remove`, `move`, `event`, `symbol` and `disconnect`.
`check` reports node/pin diagnostics and validates registered provider signatures.
Successful operations return JSON; rejected edits leave the file unchanged.
Graph paths must remain inside the project's Assets directory. MCP exposes these
operations through `visual_graph` and its advertised input schema.

`kb_editor_gameplay_graph_workflow_package` exercises graph authoring, visible
pointer routing, grouped Undo/Redo, breakpoints and pin inspection, CLI/MCP and
source-free Windows player startup. Its source and packaged captures and
gameplay positions are compared. Generated artifacts are retained under the
build's `gameplay_graph_workflow_tests` directory.
