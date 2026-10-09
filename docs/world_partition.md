# World Partition

A partitioned world is a map that never has to be loaded as a whole. It is divided into a grid of
square streaming cells; while the game runs, the cells around the player (or any other streaming
source) are loaded and the cells far away are unloaded, so a world can be far larger than what
fits in memory at once. Distant cells can be drawn as merged, simplified HLOD proxy meshes, and
named data layers ("night", "quest_x_done") switch whole groups of objects on and off at runtime.

On disk every placed object lives in a file of its own, so several people can edit one world at
the same time without touching the same file.

## Concepts

| Term | Meaning |
| --- | --- |
| World file | `Forest.21kbworld`, a small JSON text file: the cell size, the object directory, the data layers, the HLOD settings and the world's tag list. It never lists the objects. |
| Object file | `Forest.objects/<guid>.21kbobject`, one per placed object: the object's root and everything below it, plus its data layer, its always-loaded flag, its root position and the objects it links to. |
| Cell | A square column of the horizontal X/Z plane. Cell `(x, z)` covers `[x * size, (x + 1) * size)` on X and the same on Z. Cell coordinates are 64-bit integers. |
| Unit | What streams: the objects of one data layer inside one cell. Always-loaded objects form a persistent unit per layer. |
| Cell index | `Forest.cells/Forest.21kbcells`, written by the world build: every unit with its cell scene, object and entity counts and memory estimate, every HLOD proxy, the data layers and the cell size. The runtime streams from it alone. |
| Streaming source | A point cells stream around: every entity with an enabled Stream Focus component whose load mask includes World Fragment, plus sources added from C++. |
| Data layer | A named group of objects that is active or inactive for the whole scene. Objects in no named layer belong to the base layer, which is always active. |
| HLOD proxy | A merged, simplified mesh standing in for an unloaded cell. |

An object belongs to the cell that contains its root's position. Objects linked to each other
(a joint, a region portal, a lens echo or a UI reference that points at a node of another object)
always travel together: they are built into the cell of the one with the lowest guid and they are
loaded for editing together. Linked objects must share a data layer.

## Files on disk

```
Assets/Worlds/
  Forest.21kbworld                  world file (commit it)
  Forest.objects/                   one file per object (commit it)
    3f2a...e91c.21kbobject
    ...
  Forest.cells/                     build output (rebuilt by the editor and by kb_cooker)
    Forest.21kbcells                cell index
    base/r_0_0/c_0_0.21kbscene      base layer of cell (0, 0), in region (0, 0), with its .meta
    base/persistent.21kbscene       always-loaded objects of the base layer
    layer.night/r_0_-1/c_3_-1.21kbscene   "night" layer of cell (3, -1)
    hlod/r_0_0/h_0_0.obj            HLOD proxy of cell (0, 0)
    nav/r_0_0/n_0_0.21kbnavmesh     navigation tiles of cell (0, 0), when the world enables navigation
```

The world file is plain text with one entry per line, for example:

```json
{
  "schema": "21kb.world/v1",
  "guid": "6a1f0c2e9d3b47e8a5c4f0b1d2e3a4b5",
  "name": "Forest",
  "cellSize": 128,
  "objects": "Forest.objects",
  "dataLayers": [
    {"name": "night", "initiallyActive": false},
    {"name": "quest_bridge_built", "initiallyActive": false}
  ],
  "hlod": {"enabled": true, "range": 1024, "triangleRatio": 0.25},
  "tags": ["Player", "Enemy", "Monster", "AI", "NPC", "Collision"]
}
```

| Field | Rule |
| --- | --- |
| `cellSize` | Metres per cell side, 1 to 1,000,000. |
| `regionCells` | Side of a region in cells, 1 to 1,048,576 (default 16). The build puts the cells and proxies of each region in `r_<x>_<z>` folders, so a region can ship in a pack chunk of its own. |
| `objects` | Directory of the object files, relative to the world file; it may not leave the world's folder. |
| `dataLayers` | Up to 64 layers. Names are 1 to 64 letters, digits, `_`, `-` or `.`. `initiallyActive` is the state a layer starts in when the world first streams. |
| `hlod.enabled` | Build HLOD proxies and show them at runtime. |
| `hlod.range` | Proxies are shown for unloaded cells closer than this many metres to a streaming source; `0` never shows them. |
| `hlod.triangleRatio` | Fraction of the merged triangles the simplifier aims to keep, greater than 0 and at most 1. |
| `navigation` | Optional. With `"enabled": true` every build bakes one navigation mesh per cell; the section also holds the bake settings and agent profiles ([navigation.md](navigation.md)). |

Because a world file never names its objects, adding, moving or deleting objects only adds,
changes or removes object files. Two people working on different objects never edit the same
file; two people editing the same object conflict on that one file only.

The `.cells` directory is generated. It is rebuilt from scratch by every build, so it can be left
out of version control.

## Creating a world

- **From a scene in the editor.** Save the scene, then choose **World > Convert Scene to World**.
  The editor writes `<scene>.21kbworld` next to the scene, one object file per root object of the
  scene, and opens the world. The scene file itself is not changed. Converting an empty scene gives
  an empty world to build up from scratch.
- **From the command line.**
  `kb_cli world migrate --project <dir> --scene Assets/Scenes/Level.21kbscene --out Assets/Worlds/Level.21kbworld --cell-size 128`
- **By hand.** Write a world file like the one above; the object directory may start empty.

Conversion is deterministic: converting the same scene twice produces identical files. Links
between objects are kept. Objects that hold scene-wide lighting or backdrop (a directional light, a
World Backdrop or an Ambient Radiance component) are marked always loaded. The scene's audio mixer
and occlusion settings are not part of a world; they stay with the scene that places the world.

Single-file scenes keep working exactly as before. Partitioning is opt-in per world.

## Editing a world

Open the world like a scene: **File > Open Scene...** (the dialog lists `.21kbworld` files too) or
double-click it in Project Files. Opening loads only the always-loaded objects; the rest of the
world is loaded region by region from the **World** menu:

| Command | Effect |
| --- | --- |
| Load Cells Near Camera | Loads the 5 x 5 cells around the scene camera. |
| Load All Cells / Unload All Cells | Loads every object, or unloads every object that is not always loaded. |
| Build World and HLODs | Saves pending edits, then builds the cells, the HLOD proxies and the cell index. |
| Show or Hide Cell Grid | Toggles the cell outlines in the scene view: loaded cells green, cells holding unloaded objects grey (the 512 cells nearest the camera). |
| Convert Scene to World | See above. |
| Add Data Layer... | Declares a new data layer; it starts active. Edit `initiallyActive` in the world file to start it inactive. |
| Cycle Selection Data Layer | Moves the selected object (its root) to the next declared layer, ending with the base layer. |
| Toggle Selection Always Loaded | Makes the selected object always loaded, or streamed with its cell again. |

Editing works as in any scene. New root objects become new world objects when the world is
saved; deleting a root deletes its object file; parenting one object under another merges it into
that object. Unloading a region keeps the unsaved edits of its objects, and the next save writes
them. Unloading cells clears the undo history, because the undo steps may refer to the removed
objects. Saving writes only the object files whose content changed, so an unchanged object
never shows up in a diff. While a world is open, **Save As** is not available.

Entering and leaving Play mode keeps every object bound to its file. Play mode simulates the
objects that are currently loaded; to see streaming, play a scene that places the world (below).

## Placing a world in a game

A world streams inside a scene:

1. Build the world (**World > Build World and HLODs**, or `kb_cli world build`).
2. Open the scene the game starts with, add an object with a **Content Instance** component, set
   its source type to **Partitioned World** and its asset to the `.21kbworld` file.
3. Give the player (or the camera) a **Stream Focus** component. Its inner radius is the load
   radius: cells closer than it are loaded. Its outer radius is the unload radius: loaded cells stay
   until they are farther than it, so a source moving back and forth across a border does not
   reload cells. The default radii (24 m / 48 m) suit small fragments; worlds usually want radii of
   a few cells. Several sources may be active; a cell is loaded when any source wants it.

While the scene plays, the cells stream through the scene's asynchronous loader: decoding runs on
worker threads and entities are created and activated in bounded batches during the update, so
the cost of loading a cell is spread over frames instead of landing in one. Without any streaming source only the always-loaded objects
are loaded. Cell content is created at the root of the scene in world coordinates, independent of
the transform of the object that places the world.

### Budgets

Two budgets bound the work:

- `SceneLoadedContent::ConfigureStreaming` limits the entity operations and milliseconds per frame
  spent creating, activating and destroying cell content, and how many loads run at once.
- `kb::world::WorldPartitionRuntime::ConfigureBudget` limits the new cell requests per frame, the
  cells loading at once, the time spent issuing requests, and the memory of resident cells.

The memory budget counts every unit that is loading, loaded or still unloading, using the cell
index's estimate (the serialized cell size plus a fixed cost per created entity). Cells are
admitted nearest and highest priority first; a cell that would exceed the budget is skipped while
smaller ones may still fit. When the budget is lowered, each world unloads its least important
loaded cells until they fit again. Always-loaded units are charged but never refused or evicted.

### Data layers at runtime

Data layers are shared by every world of a scene. From scripts:

```lua
Scene.SetDataLayerActive("night", true)     -- the night layer's cells stream in
if Scene.IsDataLayerActive("quest_bridge_built") then ... end
```

From C++, `kb::world::WorldPartitionRuntime{ scene }.SetDataLayerActive("night", true)`. A layer
that was never set takes the `initiallyActive` value of the first world that declares it.

### HLOD proxies

At runtime a proxy is shown for each built cell whose base layer is not loaded and which lies
within `hlod.range` of a streaming source. It is hidden as soon as the cell's base layer finishes
loading, and when the cell moves more than one cell beyond the range. Proxies are drawn without
shadows. A host without mesh loaders (for example a headless tool) streams the cells without
proxies.

### C++ interface

`kb::world::WorldPartitionRuntime` (`engine/world/WorldPartitionRuntime.hpp`) also offers:

- `AddSource` / `UpdateSource` / `RemoveSource` for streaming sources that are not entities, such
  as a free camera;
- `Worlds()` with each placed world's state, its error when it cannot stream (for example "has
  not been built") and the last cell that failed to load;
- `CellState`, `PersistentState`, `IsHlodVisible` and `LoadedCells` per world;
- `Stats()` for loaded, loading, unloading and failed units, visible proxies, requests issued this
  frame, resident bytes and time spent;
- `DrainEvents()` with every load and unload request, completion, failure and proxy swap in order.

Given the same sequence of source positions, layer switches and budgets, and the same completion
order of the loads, the runtime makes the same decisions in the same order.

## Building and packaging

The build turns the object files into cell scenes (`.21kbscene` with `.meta`), HLOD proxies and the
cell index:

- **Editor:** World > Build World and HLODs.
- **kb_cli:** `kb_cli world build --project <dir> --world Assets/Worlds/Forest.21kbworld`. Without
  `--project` the project is found in the nearest folder above the world that holds a
  `.21kbproject` file.
- **kb_cooker:** builds every world under the content root before it collects the assets to
  package.

All three run one build, `kb::render::BuildWorldWithHlod` (the world's `WorldCellBuilder` with the
renderer's mesh loaders and HLOD baker), over the project's content root mounted as `/Game`, so a
world built from the editor, the command line or a cook is byte-identical: cell scenes, cell index
and HLOD proxies. A kb_cli configured without the renderer (`KB_BUILD_RENDERER=OFF`) has no mesh
geometry and builds the cells and the index only, and says so.

When the world enables navigation, the build also bakes the static geometry of the base layer
into navigation tiles and writes one navigation mesh per cell; the cell index lists them and they
stream with their cells ([navigation.md](navigation.md)).

The build fails with a message naming the object when an object uses an undeclared data layer,
links to a node no object of the world contains, is linked to an object in another layer, or lies
outside the addressable world.

An HLOD proxy merges every visible mesh of the cell's base layer into the cell's local space
(origin at the cell's minimum corner), groups the triangles by the material each section renders
with, welds duplicate vertices and simplifies each group with meshoptimizer towards
`hlod.triangleRatio`. It is written as a Wavefront OBJ file with one material group per slot. A
proxy has at most eight material slots; the triangles of further materials join the eighth.

`scripts/package_game.py` (and the editor's Build Game panel, which runs it) cooks a snapshot of
the project, so the cooker's world build never modifies the project itself. The packaged game
contains a world's cells because the dependency chain is followed from the default map: the scene
places the world, the world depends on its cell index, and the cell index depends on every cell
scene, every HLOD proxy and the proxies' materials. A world region's or data layer's cells can ship in a
chunk pack of their own (`--pack-chunk-cells`, see [content_streaming.md](content_streaming.md)).

### Region chunk packs

A large world need not ship as one pack. `package_game.py --pack-chunk-world-regions` puts every
region of every world into a chunk pack of its own (`Game.Forest.r_0_0.kbpack`, ...) next to the
base pack, listed in the pack set index (see [content_streaming.md](content_streaming.md)). A
region chunk holds that region's cell scenes in every data layer and its HLOD proxies; the world
file, the cell index and the always-loaded units stay in the base pack. Explicit `--pack-chunk`
rules go first: files they claim (a data layer's folder, `/Game/Worlds/Forest.cells/layer.night/`,
for example) stay in their chunk and the region chunks take the rest.

The rules come from `kb_cli world chunks --project <dir> [--exclude <prefix>,...]`, which prints
one `chunk LABEL=PREFIX,...` line per region with built content; the same lines work as
`kb_cli pack split --chunk` and `--pack-chunk` arguments. The player mounts the whole set, and a
cell streams from its region's chunk exactly as from a single pack.

## Large worlds

Cell coordinates are 64-bit integers, and all partition math (cell membership, distances, radii)
is done in double precision. Positions are read from the scene's transforms, so the precision of
an object's placement is the precision of the transform storage. The text formats store cell
coordinates exactly up to 2^53.

## Limits

- Cells partition the horizontal plane only; height does not affect streaming.
- A cell that failed to load is not retried until the world is placed again (its failure is
  reported through `Worlds()`).
- Data layer state is per scene, shared by all worlds in it.
- The editor's cell grid outlines at most 512 cells, nearest to the camera first.

## Tests

| Test | Covers |
| --- | --- |
| `kb_engine_tests world-partition` | Grid math with 64-bit cells, world/index/object file round trips and validation, scene migration (deterministic, scene untouched), region editing with unsaved edits across unloads, save of only changed files, reload rebinding, the build (units, layers, persistent objects, linked objects, stale output removal, error cases), streaming with hysteresis, stream focus sources, data layers (also through the script API), HLOD swapping, memory and request budgets, eviction, determinism of the decisions. |
| `kb_renderer_tests world-hlod` | HLOD merging, material slots, simplification and cell-local placement; the proxy loads as a regular mesh asset after a world build. |
| `kb_game_partitioned_world` (`kb_game_core_tests --partitioned-world`) | kb_cli and kb_cooker build a world byte-identically; kb_cooker packages its cells, index and proxies, and the packaged runtime streams the cells, also from a pack set with one chunk per region. |
| `kb_cli_tests` | `kb_cli world migrate` and `kb_cli world build`, including HLOD proxies and finding the project. |
| `kb_editor_world_partition_headless` | Editor conversion, region load and unload, layer and always-loaded edits, save, build (byte-identical to the kb_cli/kb_cooker build), the cell grid as the GPU draws it (the viewport read back with and without the grid, the added pixels checked for the grid's colour) and streaming of a placed world in Play mode. |
| `scripts/tests/test_package_game.py` | The package job cooks a snapshot carrying the world and its object files, and splits region chunks after explicit chunks. |
