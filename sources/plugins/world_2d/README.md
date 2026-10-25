# World 2D Map Editor

Create tile maps for a view from above or a side-view platformer. The main toolbar
contains painting, erasing, filling, selection, brush size and Undo/Redo. Less
frequent tools live under **More**. **?** reopens the short visual tutorial.

## First map

1. Choose **Create > 2D Object > Map Top Down** or **Map Platformer** in the
   Hierarchy. This creates a Grid entity, an orthographic Map Camera and four
   layers: Background, Terrain, Objects and Foreground. Terrain is selected,
   with collision enabled. The other layers start without collision.
   **World 2D Map** in Add Component also supports a single map layer, including
   on an entity that already has a Mesh Renderer or Collider. Those components
   remain independent when editing, playing, copying or removing the map.
2. Click **Add graphics...**, or drop PNG/BMP images into the map window. Images
   are copied into the project's `Assets/2D Map Sprites/Imported` folder. Existing
   files and the source images are preserved.
3. Check the tile size and click **Paint map**. The common sizes are one
   click away; **...** accepts width, height, margin and spacing in pixels.
   The sheet preview shows the resulting slices, including rectangular sprites.
4. Pick a tile on the left and paint. Search and categories help with larger
   palettes. Use **Fill** for an enclosed area and **Select** to move or copy it.
5. For a platformer, choose **More > Side view (platformer)**. For a map viewed
   from above, keep **View from above**. The map camera follows the choice.
6. Press **Ctrl+S** to save the map and scene together. Saving the scene also
   saves every modified map. Play flushes map changes before starting and uses
   the map camera.

The **2D Map** tab docks alongside the Scene and Game tabs. Select a layer in
the Hierarchy to paint it. Its Inspector provides visibility, editing lock and
collision checkboxes. **More > Layer order** controls drawing order. The toolbar
wraps when the panel is narrow, leaving room for the palette and canvas.

Map changes appear in the scene while painting. Closing the editor or changing
scenes offers Save, Don't Save and Cancel for an unsaved map. Don't Save restores
the saved map, its generated collision and the camera orientation. Resizing,
painting collision and switching views use the scene's shared Undo/Redo history;
undoing a smaller map also restores the tiles outside its reduced bounds.
Map dimensions and view changes apply to all layers under the same Grid as one
undoable operation. Painting is disabled for locked layers and during Play.

## Automatic edges (autoborder)

Auto edges is **on by default**. Double-click a floor tile or ground brush to open
the visual 5 x 5 board. Assign the ground, outer edges, corners and inner corners
from the sprite sheet, then save the ground. Ground variants and their weights
remain available. Painting that ground automatically joins its edges to nearby
tiles; erasing and moving tiles also updates the surrounding edges.

**Edit selected graphic...** opens either the ground's edge board or the sprite
sheet settings. In the sheet settings, connected transparent sprites can form
one object, such as a tree. A selected rectangle can also be saved as an object
and placed with a single click.

**More > Extra tools > Auto edges** (or **A**) toggles automatic edging.
**Ctrl+B** rebuilds edges in the selection, or on the current floor when nothing
is selected. **More > View > Redo all edges of the map** updates every floor.

## Walls and platforms

Choose **More > Extra tools > Paint walls / solid platforms**, or press **C**.
Paint the tiles that should block movement. Amber outlines show the actual collision shapes
on these tiles while the collision tool is active. **Ctrl + paint** clears collision
without changing graphics; press **C** again to return to painting graphics.

Adjacent solid tiles merge into static boxes under the map's generated
**Map Collision** group. Top-down maps use walls rising from the ground;
side-view maps use boxes covering the painted platform tiles. Editing a shared
map asset updates its instances in the open scene. During Play, each entity
gets its own map state and generated collision. Runtime edits do not modify the
source asset or other instances. The generated groups are managed by the map
system; add custom colliders, triggers and gameplay objects as separate entities.

Right-click a selection and choose **Tile properties > Collision** for slopes,
one-way platforms, triggers or a custom outline. Click the outline's corners,
then press Enter; Backspace removes a corner and Escape cancels. Use **Surface
friction and bounce** for ice or elastic surfaces. **Use this collision for every
matching graphic** makes it a graphic default; individual cells can override it.
One-way platforms allow upward jumps and support bodies and character controllers
on descent. They use the map's upward axis and remain upward-facing when a graphic
is flipped. Concave outlines are triangulated and extruded into collision meshes.

## Graphics, objects and layer effects

Right-click a cell or selection for rotation, mirroring, tint, opacity, graphic offsets,
animation and typed tile data. In the animation editor, click sheet graphics to add
frames in any order, including repeated frames. Select a frame to move it left or
right or remove it. Play/Pause previews the draft; speed presets and a numeric field
control playback. Apply updates matching graphics as one undoable change; Cancel
leaves the map unchanged. No animation clears the sequence when applied. The map canvas, Scene preview
and player use the same frame and UV rules. Animation refreshes only chunks whose
visible frame changed. Each map instance owns its playback clock, so a new
Play session starts its animation from the beginning.

**Save selection as a stamp** stores a reusable map pattern in the palette. Named
multi-cell objects remain available. Project prefabs also appear in the palette;
placing one stores a prefab reference and creates ordinary ECS entities at the
cell's footpoint in the Scene preview and at runtime. Generated children stay out
of the Hierarchy and saved scenes. The cooker follows their prefab dependencies.

**More > Extra tools > Scatter density** scatters the selected graphic or object
with one undoable stroke. Right-click also offers replacement of a graphic across
the map. **More > Make this map independent** gives an instance its own source
asset before editing a formerly shared map.

**More > Sort graphics by their feet** enables per-row depth ordering.
**Background scrolling** sets horizontal and vertical parallax: 1 stays in the
world, 0 stays with the camera, 0.5 gives a distant background. Parallax is driven
by the primary runtime camera. Keep decorative backgrounds on layers with collision
disabled.

**Lights affect this layer** uses the engine's material lighting. A graphic marked
as light-blocking contributes a separate shadow-only mesh. Scene lights and their
shadow settings control illumination; unlit layers retain their original colors.

**More > Paint in Scene View** uses the active map palette and tools directly on
the selected layer's plane, including transformed Grid entities. Finish a stroke
with mouse release; Ctrl+Z uses the same history as the map canvas.

Numeric settings use separate labeled fields with step buttons and useful presets
(for example Ice and Bouncy surfaces, distant backgrounds and scatter coverage).
Fields accept either a decimal point or a comma. Invalid values remain in the form
with an explanation; Apply is atomic and Cancel preserves the map. Tab moves between
fields, Enter applies, and Escape cancels. Sprite slicing has separate width, height,
margin and spacing fields, including for rectangular sprites.

## Useful controls

| Action | Control |
| --- | --- |
| Paint | Left mouse button or drag |
| Erase while painting | Ctrl + left mouse button |
| Paint a rectangle | Shift + drag |
| Move around the map | Middle mouse drag or arrow keys |
| Zoom | Mouse wheel |
| Select / paint | Space |
| Copy, cut, paste selection | Ctrl+C, Ctrl+X, Ctrl+V |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y |
| Save map and scene | Ctrl+S |
| Find graphics | Ctrl+F |
| Change floor | Page Up / Page Down |
| Reload graphics and brushes | F5 |

**More** also contains map size, floor display, grid, framing guide and live scene
preview. In the Inspector, **Client Box** sets the framing guide and camera height;
the camera's visible width follows the game viewport's aspect ratio. **Reset map
camera** restores its generated placement after manual camera adjustments.

## Files and automation

Maps use `.kbmap2d`; ground brushes, objects and sheet settings use `.kbtileset`.
Textures and generated sprite materials are normal project assets. The game
cooker includes the logical map data and its dependencies, so packaged games
can rebuild both graphics and collision.

The ECS components are `Grid`, `Tilemap`, `TilemapRenderer` and `TilemapCollider`.
Hierarchy parents organize entities; they do not introduce a separate node
runtime. The map system divides each floor into 16 x 16 chunks, rebuilding only
changed chunks. Generated mesh assets and collider entities are released when
their map is removed or the system stops. Evicted mesh payloads are rebuilt on
demand. Older maps stored in Mesh Renderer components are upgraded on load.

`Map2DRuntime::Map`, `Tile`, `SetTile`, `PaintGround` and `EraseTile` provide C++
runtime access. Writes update logical state immediately. Embedded ground rules
rebuild neighboring automatic edges, including across chunk boundaries. Geometry
and collision refresh after FixedTick and before physics; Tick changes refresh
before rendering. Source assets remain unchanged.

Lua, native scripts and Visual Graphs share the `Map2D` function library:
`Get`, `Set`, `Erase`, `PaintGround`, `Get/SetBool`, `Get/SetInteger`,
`Get/SetNumber`, `Get/SetText` and `NextStep`. Coordinates are zero-based and
floor defaults to zero. `NextStep` finds a complete route before returning its
next cell. Top-down maps use four-way movement; platformers add falls and sampled
jump arcs, controlled by jump height and distance. This is a cell path query:
character movement, clearance and jump timing remain gameplay/controller decisions.

`kb_cli map2d` exposes map creation, painting, filling, brushes, objects, resize,
orientation, collision (`block` / `unblock`) and camera setup. After a CLI edit
that changes the map's layout or collision, run `map2d view --entity <Name>` with
the same project and scene arguments to update its scene camera and colliders.

The `kb_world2d_tests` target checks file round-trips, autoborder, editing,
Undo/Redo, import, mesh output, camera, merged collision, component serialization,
prefabs and independent runtime chunks. On Windows,
`kb_editor_map2d_headless` exercises the production editor with screenshots,
including both views, import, collision, resize recovery, shared history,
multi-layer creation, repeated Play/Stop, layer checkboxes and native docking.
It also validates numeric forms, comma decimals, cancellation, Undo/Redo, arbitrary
animation frame order, native text input, and viewport ray painting on transformed
maps in both orientations. It renders through the production Play camera and checks
the captured tile colors and animation timing. `kb_editor_map2d_lighting` compares
illuminated captures with and without a light-blocking graphic.
`kb_editor_map2d_packaged` then cooks that authored scene, stages the player and
provider modules, launches the deployed game and checks 120 frames for missing
resources or dropped draws. Its artifacts include the pack, a game screenshot
and frame statistics under the build directory's `SelfTest` folder.
