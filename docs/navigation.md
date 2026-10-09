# Navigation

How 21kb turns scene geometry into navigation meshes, how those meshes reach a running game
(a scene's mesh, or tiles streaming with the cells of a partitioned world), and how agents move on
them as a crowd.

| Piece | Code |
| --- | --- |
| Generation (Recast), build settings, geometry | `engine/navigation/NavMeshBuild.hpp`, `NavGeometryCollector.hpp` |
| The navigation mesh asset (`.21kbnavmesh`) | `engine/navigation/NavMeshAsset.hpp` |
| Runtime meshes, obstacles, links (Detour) | `src/private/navigation/NavMeshRuntime.hpp` |
| Crowd (DetourCrowd) | `src/private/navigation/NavMeshCrowd.hpp` |
| Scene API | `engine/scene/SceneNavigation.hpp` (`scene.Navigation()`) |
| Off-mesh links | `kb::scene::NavLink` component (`engine/scene/Navigation.hpp`) |
| Baking with the renderer's mesh loaders | `kb/render/world/NavMeshBakeTool.hpp`, `RenderNavGeometrySource.hpp` |
| Script API | `Navigation.*` (`engine/script/ScriptNavigationApi.hpp`) |

Generation and pathfinding use Recast & Detour 1.6.0 (zlib license), fetched by CMake from the
official release tarball at a pinned SHA-256 (see `third_party/THIRD_PARTY_LICENSES.md`).

## Concepts

| Term | Meaning |
| --- | --- |
| Agent profile | One agent size: name, radius, height, max climb (step height) and max slope. A mesh holds tiles for up to 8 profiles. An agent uses the smallest profile at least as wide as its radius, else the widest. |
| Tile | A square of the global tile grid, `cellSize * tileCells` metres wide. Tile `(x, z)` covers `[x * size, (x + 1) * size)` on X and the same on Z, so tiles baked separately (world cells) fit together. |
| Layer | One walkable storey of a tile: a height grid with an area and neighbour connections per column. A bridge over a road gives two layers. Tiles store layers, not polygons: polygons are built from layers when a tile is added and again whenever an obstacle or link touching it changes. |
| Area | One of 32 navigation areas (0 is ordinary ground). Areas have costs per scene and agents carry an area mask. |
| Obstacle | A `NavObstacle`: carving obstacles cut holes into the tiles they touch (grown by each profile's radius); obstacles with an area repaint the polygons under them; others are steered around by agents. |
| Off-mesh link | A `NavLink`: a jump, ladder or walk between two points the polygons do not join. |

## Build settings

`kb::navigation::NavMeshBuildSettings`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `profiles` | one `Humanoid`: radius 0.4, height 1.8, climb 0.4, slope 45 | Agent sizes, 1 to 8, unique names |
| `cellSize`, `cellHeight` | 0.25 m, 0.2 m | Voxel size geometry is rasterised into |
| `tileCells` | 64 | Tile side in voxels (16 to 252); tiles are 16 m with the defaults |
| `edgeMaxError` | 1.3 | How far polygon edges may stray from the voxel outline, in voxels |
| `renderMeshes`, `colliders` | true, true | Which geometry is baked |

## What is baked

The static geometry of the scene (or of the world's base layer, always-loaded objects included):

- **Mesh renderers**: the finest level of detail of the mesh (static meshes), and terrain
  (`.kbterrain`) as its exact heightfield, holes included. Hidden mesh renderers are skipped.
- **Colliders**: boxes, spheres and capsules, and mesh colliders (CollisionMesh assets). Triggers
  are skipped.

A node is static unless it, or a node above it, has a dynamic or kinematic rigidbody; nodes with
a `NavAgent`, `NavObstacle` or character controller are never baked. Surfaces facing up or down
within a profile's slope are walkable, so single-sided geometry works whichever way it was
authored. Data layer objects of a world are not baked: switch them with obstacles and links.

## Baking a scene

- **Editor:** World > **Bake Navigation Mesh**. The editor saves the scene, bakes
  `<scene>.21kbnavmesh` beside it, and, if the scene does not place it yet, adds a
  **Navigation Mesh** object with a ContentInstance of kind **Navigation Mesh** pointing at it.
  World > **Show or Hide Navigation Mesh** toggles the outline of the baked polygons in the scene
  view.
- **kb_cli:**
  `kb_cli navmesh bake --project <dir> --scene Assets/Scenes/Level.21kbscene [--out <file>] [--cell-size m] [--cell-height m] [--tile-cells n] [--agent name:radius:height:climb:slope]...`.
  Settings not given are those of the mesh being replaced, else the defaults; the editor bakes with
  the settings of the existing mesh too. `kb_cli navmesh info <file.21kbnavmesh>` prints the
  settings and tile counts per profile.

Both resolve meshes with the cooker's runtime loaders over the project's content root mounted as
`/Game`, so the editor and kb_cli write identical bytes (checked by the editor's headless
navigation scenario). Tiles are built in parallel on the engine's worker pool
(`kb::ecs::WorkerPool`): the caller's when it passes a running one, else one the bake starts with
one worker per core, at most 8. The tiles are the same bytes whatever the number of workers.

A scene uses its mesh through the ContentInstance: while the scene plays, the navigation system
loads the asset on the asset worker and adds its tiles. The ContentInstance's transform does not
move the tiles; they are in world coordinates. The cooker packages the mesh because the scene's
`.meta` records the ContentInstance's asset.

## Partitioned worlds

A world opts in with a `navigation` section in its world file (written by the editor's Bake
Navigation Mesh, which then builds the world):

```json
"navigation": {"enabled": true, "cellSize": 0.25, "cellHeight": 0.2, "tileCells": 64, "edgeMaxError": 1.3, "renderMeshes": true, "colliders": true,
  "agents": [
    {"name": "Humanoid", "radius": 0.4, "height": 1.8, "maxClimb": 0.4, "maxSlope": 45}
  ]}
```

Every world build (editor, `kb_cli world build`, kb_cooker) then bakes the whole world's static
geometry once and writes one navigation mesh per cell, holding the tiles whose centre lies in the
cell: `Forest.cells/nav/r_<x>_<z>/n_<x>_<z>.21kbnavmesh`. Geometry from neighbouring cells is part
of every tile's border, so tiles join seamlessly whichever cell placed the object. The cell index
lists the meshes (`navMeshes`) and depends on them, so packaging carries them, and
`kb_cli world chunks` puts each region's navigation folder in the region's chunk pack.

At runtime a cell's tiles stream with the same radii as its objects: they are requested when a
streaming source's load radius reaches the cell and removed when no unload radius does.
`WorldPartitionRuntime::IsNavMeshLoaded`, `Stats().loadedNavMeshes` and the `NavMeshLoaded` /
`NavMeshUnloaded` streaming events report them. Agents whose paths were partial because the
destination lay in an unloaded cell plan again when new tiles arrive.

## The asset format

`.21kbnavmesh` is binary, little endian: the magic `21KBNAVM`, a version, the build settings,
then the tiles sorted by profile and coordinate, each with its layers; a layer's three grids are
one zstd frame. The reader refuses anything a rebuild could trip over: truncation, trailing
bytes, grid sizes that do not match the tile, unknown area codes and neighbour connections that
lead out of a tile's grid. `fuzz/targets/navmesh.cpp` fuzzes the reader together with the polygon
rebuild.

## Runtime

```cpp
kb::scene::SceneNavigation navigation = scene.Navigation();
const std::uint64_t handle = navigation.AddNavMesh(asset);           // also done by ContentInstance and world streaming
kb::scene::NavPathResult path = navigation.FindPath(start, end);     // world positions (DVec3)
kb::scene::NavRaycastResult ray = navigation.Raycast(start, end);
std::optional<kb::math::DVec3> point = navigation.NearestPoint(position);
navigation.SetAreaCost(3U, 5.0F);
navigation.RebakeTiles(min, max);                                    // geometry changed while running
navigation.SetOrigin(farAwayPlace);                                  // Detour's float space for a far world
```

- Several meshes can be added; where two hold the same tile, the first added is used until it is
  removed. Meshes must share the layout (cell sizes, tile size, profiles).
- **Obstacles** are read from the scene whenever agents step or a query runs: only the tiles a
  changed obstacle touched before or touches now are rebuilt from their layers.
- **Links** (`NavLink`: start and end offsets in the owner's space, radius, kind, area,
  bidirectional) belong to the tile their start lies in, which is rebuilt when a link changes.
- **Geometry built, moved or destroyed while the game runs**: `RebakeTiles(min, max)` rasterises
  the tiles over that box again from the scene's current static geometry, on the scene's worker
  pool, and uses them in place of the baked ones; `RestoreBakedTiles()` goes back.
- `SetOrigin(position)` sets the navigation origin: Detour works in floats relative to it, and agent
  destinations and `AgentPath` corners are in its space (world position minus the origin). Set it
  near where a far-away world is played (docs/large_worlds.md); the default is the world origin.
- `NavMeshTriangles(profile)` returns the polygons for debug drawing.

The Detour meshes are the scene's only navigation data: without them agents stand still and report
a `Failed` path.

## Crowds

With polygon meshes present, the navigation system moves every enabled `NavAgent` with DetourCrowd
(`dtCrowd`, compiled from the same Recast & Detour release) at the scene's fixed step. A crowd walks
one Detour mesh, so there is one crowd per agent profile; `NavMeshCrowd` keeps the agents and the
crowds in step:

- Each agent is a crowd member with its `NavAgent` values: radius, height, maximum speed and
  acceleration; its neighbour range is `neighbourRangeRadii` radii.
- Destinations are handed to the crowd with at most `maxPathRequestsPerStep` per step; waiting
  agents report `Pending`. The crowd plans the path corridor (a quick search first, the rest of a
  long path through its path queue), string-pulls it into corners, shortens it when a later corner
  comes into view and re-optimises it locally. A destination off the mesh gives `Failed`; an
  unreachable one a `Partial` path to the closest point. An agent stops within its
  `stoppingDistance` of the end of its path.
- The crowd keeps agents apart (separation), samples velocities against neighbours and walls
  (obstacle avoidance), resolves remaining overlaps and constrains moves to the mesh, so agents
  follow its height.
- **Areas:** the crowd filters polygons by their flag bits. Every area mask in use gets one of 16
  filter slots, and the polygons whose area is in a slot's mask carry that slot's flag, so masks
  and the scene's area costs apply exactly.
- **Non-carving obstacles** without an area join every crowd as members that never move: agents
  avoid them like other agents and are pushed out of them.
- **Off-mesh links** are crossed by the crowd; the agent is shown arcing over a jump, climbing a
  ladder at its low end before stepping across, or walking straight.
- **Level of detail:** agents within `nearDistance` of a focus (`SetCrowdFocuses`, else every
  enabled Stream Focus; without any focus every agent is near) avoid, separate and shorten their
  paths every step; the others only follow their corridors, shortening them every
  `farPathOptimizationInterval` steps.
- Agents join the crowds in entity id order and the crowds run on one thread, so equal input
  replays to equal positions. Agents with a character controller hand their velocity to the
  physics character and follow where it moved them; while crossing a link their transform is
  placed directly.

`ConfigureCrowd(NavigationCrowdSettings)` sets the budgets; `CrowdStats()` reports near and far
agents, path requests, waiting agents, agents on links, sampled velocities and the step's wall time.

## Script API

| Function | Returns |
| --- | --- |
| `Navigation.FindPath(startX, startY, startZ, endX, endY, endZ[, profile, areaMask])` | `found`, `complete`, `length`, `corners`, `endX/Y/Z` |
| `Navigation.PathCorner(index)` | `found`, `x`, `y`, `z` of a corner of the last path found in this scene |
| `Navigation.Raycast(startX, startY, startZ, endX, endY, endZ[, profile, areaMask])` | `valid`, `hit`, `fraction`, `x`, `y`, `z`, `normalX`, `normalZ` |
| `Navigation.NearestPoint(x, y, z[, profile, areaMask])` | `found`, `x`, `y`, `z` |
| `Navigation.SetAreaCost(area, cost)` | `applied` |

Lua accepts positional arguments or one table of named ones. `NavLink` properties (`start.x` ...,
`kind`, `area`, `bidirectional`, `enabled`) are editable through the component API like other
components.

## Measurements

On the development machine (Release, one thread for the crowd):

- Baking a 200 x 200 m arena with 25 pillars (cell 0.3 m, 96-cell tiles, 64 tiles) on the
  pool the bake starts: about 16 ms (about 73 ms on one thread).
- Crowd step, agents walking across that arena through each other: 1000 agents about 2.6 ms,
  5000 agents about 21 ms, 5000 agents with level of detail around one focus about 16 ms.
  Under load from other builds on the same machine the figures doubled.

`kb_engine_navigation-crowd-bench` checks that 1000 agents step within 8 ms and that level of
detail makes a large crowd cheaper.

## Limits

- The crowd runs on one thread; LOD is the lever for very large crowds.
- A crowd walks one Detour mesh, so agents avoid and push only agents of their own profile.
- At most 16 different agent area masks are in use at a time (the crowd's filter slots); an agent
  whose mask finds no free slot reports a `Failed` path. A crowd holds at most 65 535 agents and
  non-carving obstacles.
- The crowd's own budgets are fixed by DetourCrowd: a 20-node quick search per destination, 8 paths
  in its queue with 100 search nodes per step, at most 6 neighbours per agent and corridors of up to
  256 polygons.
- Polygons have no detail mesh: heights inside a polygon are interpolated from its corners.
- A layer spans at most 255 voxels of height; Recast splits taller walkable regions into layers.
- A layer holds at most 255 walkable regions; a layer of a tile more cluttered than that builds
  no polygons. Smaller tiles (`tileCells`) avoid it.
- `RebakeTiles` reads static geometry from the scene's objects (colliders, terrain; meshes need
  a source with the renderer's loaders) and does not change the asset on disk.
- Data layer objects of a world are not baked into its navigation meshes.

## Tests

| Test | Covers |
| --- | --- |
| `kb_engine_tests navigation-mesh` | Settings validation; the asset format (round trip, truncation, refusal of escaping connections and unknown areas); bakes on one worker, four and a bake-owned pool byte-identical; walkable area of a floor; slopes (30 vs 60 degrees) and steps (0.3 vs 0.6 m); carving obstacles (blocked, around, rotated, removed) with an agent walking around; geometry rebuilt at runtime; area costs and masks; two agent sizes through a doorway; jump and ladder links (one-way, disabled) crossed by agents; far from the world origin; a 1200-agent crowd replaying exactly; crowd level of detail; NavAgent, NavObstacle and NavLink save/load; scripts; a scene's ContentInstance placing its mesh; a world's tiles streaming in and out with its cells. |
| `kb_engine_tests navigation-runtime` | Agents on baked meshes: accelerating within their speed, stopping at their stopping distance, re-planning for a new destination, standing still while the scene is paused; turning corners without leaving the mesh; going around a carving obstacle and walking straight again once it is disabled; a partial path to the closest point when an obstacle closes the way; steering around a non-carving obstacle; two agents passing each other and replaying exactly, and still passing when created in the other order; no mesh means a failed path until one is added; walking a mesh 10 000 km out as precisely as at the origin, as a root and as a child. |
| `kb_engine_navigation-crowd-bench` | Crowd step budget for 1000 agents and the level-of-detail gain at 5000. |
| `kb_cli_tests` | `kb_cli navmesh bake` and `info` (colliders, imported meshes, profiles, kept settings) and `world build` with navigation and region chunks. |
| `kb_editor_navigation_headless` | Editor bake of a scene with a floor collider, an imported mesh and a jump link; identical bytes to the kb_cli bake; the outline drawn in the scene view; an agent crossing the link in Play mode. |
| `kb_fuzz_navmesh_corpus` (`KB_BUILD_FUZZERS`) | The navigation mesh reader and polygon rebuild over the seed corpus. |
