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
| Area | One of 32 navigation areas (0 is ordinary ground). The project names them, with a colour and a cost, in Project Settings > Navigation (by default Walkable, Road, Grass, Water and Not walkable); an area's id is its place in that list and scenes store the ids. Every scene starts from the project's costs (a cost of 0 makes an area not walkable), scripts may change them, and agents carry an area mask. |
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

A mesh renderer or collider with **Affects Navigation** off (`affectsNavigation`, on by default) adds
nothing to any bake, full or automatic, like grass or small props a character should ignore.

A node is static unless it, or a node above it, has a dynamic or kinematic rigidbody; nodes with
a `NavAgent`, `NavObstacle` or character controller are never baked. Surfaces facing up or down
within a profile's slope are walkable, so single-sided geometry works whichever way it was
authored. Data layer objects of a world are not baked: switch them with obstacles and links.

**Navigation Bounds** (`NavBounds`, Add Component > Navigation or Create > Navigation Bounds) limit
where a scene is baked: a box (size and centre in the owner's space; its world-space bounding box
counts) of which only the walkable ground inside is kept, and only tiles touching a box are built.
Several boxes bake their union; a scene without any bakes all of its static geometry. The bake
settings live on the Navigation Bounds too: agent radius, height, max step height and max slope,
and, behind Advanced, cell size and tile size (metres, rounded to whole cells of 16 to 252). Every
Navigation Bounds of a scene must hold the same settings, or the bake is refused.

## Baking a scene

- **Editor:** World > **Bake Navigation Mesh**. The editor saves the scene, collects its static
  geometry and bakes it on a worker while the editor stays usable (the Console reports each quarter
  done); then it writes `<scene>.21kbnavmesh` beside the scene and, if the scene does not place it
  yet, adds a **Navigation Mesh** object with a ContentInstance of kind **Navigation Mesh** pointing
  at it. Once a scene has a mesh, edits to its static geometry or its Navigation Bounds update it by
  themselves: half a second after an edit settles, only the tiles the changed objects touch (with
  the border the agents need) are rebuilt in the background, giving the same tiles a full bake
  would; a change of the bake settings, or a first or last Navigation Bounds, bakes the whole scene
  again. World > **Turn Automatic Navigation Update On or Off** switches this off for the session.
  World > **Show or Hide Navigation Mesh** toggles the debug view of the baked mesh in the scene
  view: the walkable polygons filled with the colour of their navigation area (Walkable is green),
  their outlines with the outer edges stronger, and each tile's bounds in
  yellow, all lifted 10 cm above the ground and hidden behind scene geometry in front of them.
  Navigation Bounds boxes are always drawn (also hidden behind geometry) and are selected by a
  click on their lines.
- **kb_cli:**
  `kb_cli navmesh bake --project <dir> --scene Assets/Scenes/Level.21kbscene [--out <file>] [--cell-size m] [--cell-height m] [--tile-cells n] [--agent name:radius:height:climb:slope]...`.
  Settings not given are those of the mesh being replaced, else the defaults; the editor bakes with
  the settings of the existing mesh too. A scene with Navigation Bounds bakes with their settings
  in both. `kb_cli navmesh info <file.21kbnavmesh>` prints the
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
- Destinations are handed to the crowd with at most `maxPathRequestsPerStep` per step, nearer
  levels of detail first and then the longest waiting; waiting agents report `Pending`. The crowd plans the path corridor (a quick search first, the rest of a
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
  Removing or changing a link, or unloading its landing tiles during a crossing,
  cancels the crossing and returns the agent to the supported departure point. If
  no departure surface remains, navigation stops with a failed path. Returning tiles
  or re-enabling the link lets the agent replan. Changes to one agent profile's tiles
  do not invalidate another profile's waiting path.
- **Level of detail:** agents within `nearDistance` of a focus (`SetCrowdFocuses`, else every
  enabled Stream Focus; without any focus every agent is near) avoid, separate and shorten their
  paths every step; agents within `midDistance` separate and shorten their paths but sample no
  velocities; the others only follow their corridors, shortening them every
  `farPathOptimizationInterval` steps. An agent moves to a farther level only 10% beyond its edge.
  The project sets the three values (Project Settings, Navigation, Crowd Detail).
- The crowd's per-member passes (path validity, walls and neighbours, corners, steering, velocity
  sampling, integration, collision push, moving along the mesh) and reading the members back run
  on an engine worker pool (`workerThreads`, 0 for the default count), each worker with its own
  Detour query objects; every member writes only itself and everything that touches several
  members runs on the calling thread. On Android the passes stay on the calling thread and no pool
  is made (`kMinAgentsOnWorkers`): there pieces that land on the slow cores hold a pass up for
  longer than the pass takes on a fast core, at every crowd size measured. Agents join the crowds in entity id order, so equal input
  replays to equal positions on any number of threads. Agents with a character controller hand their velocity to the
  physics character and follow where it moved them; while crossing a link their transform is
  placed directly.

`ConfigureCrowd(NavigationCrowdSettings)` sets the budgets; `CrowdStats()` reports near, mid and
far agents, path requests, waiting agents, agents on links, sampled velocities and the wall time of
the step and of its stages.

## Horde movement

A `NavAgent` with `horde` on (Inspector: **Move as Horde**) leaves the crowd: agents heading for the
same destination (with the same profile and areas) share their guidance instead of each planning a
path (`NavMeshHorde`). The polygons of a profile's mesh are grouped into **clusters** (the polygons
of one tile layer that join through portals; an off-mesh link is a cluster of its own), built once
per navigation mesh together with the **gateways** between neighbouring clusters (the widest
opening of each pair):

- **The route:** from the cluster a destination lies in, a search over the gateways outward gives
  every cluster it reaches its cost to the destination and the gateway to leave by. It is shared by
  every destination in that cluster, grows within the shared budget and only as far as the agents
  that need it stand (plus a few clusters), so a map of any size costs only where agents are.
- **The fields:** inside the destination's cluster agents follow the *goal field* (a search over
  the cluster's polygons from the polygon the destination lies in, giving each polygon its cost
  and the portal to leave by); in every other cluster they follow the *exit field* of the route's
  gateway (the same search from the polygon the gateway leaves from, ending in the gateway's
  portal), which is the same for every destination whose route leaves that way. A field has the
  size of its cluster, so starting one costs only the polygons it visits. All fields and the route
  grow by at most `hordeFieldNodesPerStep` nodes per step together, routes first, then the fields
  agents wait for, and the fields of the two clusters ahead of them. Fields are kept for reuse
  (`hordeFieldCacheSize` idle ones, the least recently used goes first).
- **A moving destination** only makes a new goal field when it enters another polygon (a few dozen
  polygons at most) and a new route when it enters another cluster; every exit field already made
  stays. Agents keep following the old guidance until the new reaches them, so a running target
  costs no spike and no agent waits except for one that has just stepped into a tile whose field is
  not there yet.
- **Changed tiles and costs:** a rebuilt tile (or one of its neighbours at the border) drops only
  the fields over the clusters that changed; changed area costs or blocked areas make new fields
  on the next step while the old ones keep guiding.
- **Steering:** each agent heads for the closest point of its polygon's portal (looking one polygon
  further when it stands at the portal), or straight for the destination in its polygon, and moves
  over the polygons through portals it may enter, sliding along walls, at the height of the
  polygon under it, so storeys, stairs and bridges are kept apart. A link on the way is walked
  straight from end to end, to the landing the route continues from.
- **Separation:** one grid over the agents is built per step; neighbours closer than both radii
  plus `hordeSeparationDistance` push each other apart (squared falloff, weighted by
  `separationWeight`), ignoring agents more than an agent's height above or below. Crowd agents
  and non-carving obstacles stand in the same grid, so horde agents keep off them too; crowd
  agents do not see horde agents. There is no velocity sampling.
- **Level of detail:** with the crowd's focuses and distances, mid agents update every second step
  and far agents every fourth (with the elapsed time) and without separation. An agent that stopped
  within its stopping distance sleeps until its destination moves away.
- The agents step on the crowd's worker pool (on the calling thread on Android), each writing only itself and reading the grid in a fixed order, so equal input replays
  exactly on any number of threads. The crowd's reading of the agents' transforms runs on the same
  pool.

`CrowdStats()` reports the horde agents, how many were updated, asleep, waiting for guidance and
on links, the fields kept (goal and exit fields), the nodes the fields and routes grew by and the
wall time of the horde step and of its fields.

## Script API

| Function | Returns |
| --- | --- |
| `Navigation.FindPath(startX, startY, startZ, endX, endY, endZ[, profile, areaMask])` | `found`, `complete`, `length`, `corners`, `endX/Y/Z` |
| `Navigation.PathCorner(index)` | `found`, `x`, `y`, `z` of a corner of the last path found in this scene |
| `Navigation.Raycast(startX, startY, startZ, endX, endY, endZ[, profile, areaMask])` | `valid`, `hit`, `fraction`, `x`, `y`, `z`, `normalX`, `normalZ` |
| `Navigation.NearestPoint(x, y, z[, profile, areaMask])` | `found`, `x`, `y`, `z` |
| `Navigation.SetAreaCost(area, cost)` | `applied`; in Lua `area` may also be an area's name |
| `Navigation.FindArea(name)` | `found`, `area` (the id of the project's area of that name, -1 for none) |

Lua accepts positional arguments or one table of named ones. `NavLink` properties (`start.x` ...,
`kind`, `area`, `bidirectional`, `enabled`) and `NavAgent` properties (`destination.x` ...,
`horde`, `enabled` ...) are editable through the component API like other components.

## Measurements

On the development machine (Release):

- Baking a 200 x 200 m arena with 25 pillars (cell 0.3 m, 96-cell tiles, 64 tiles) on the
  pool the bake starts: about 16 ms (about 73 ms on one thread).
- Crowd step, agents walking across that arena through each other, on the default worker pool of
  a 24-thread machine: 1000 agents about 1.1 ms, 5000 agents about 3.8 ms, 10 000 agents about
  7.3 ms (about 42 ms on one thread), 20 000 agents about 12 ms. Under load from other builds on
  the same machine the figures doubled.
- `FindPath` across the arena about 5 us, over 14 m about 2 us.
- Horde step on a 240 x 240 m two-storey level with stairs, a bridge and a link, targets moving
  every step (median of 1000 steps, machine shared with other work): 10 000 agents about 1.5 ms
  after 1 target and 2 ms after 10 or 100 (about 2.3 ms on one thread), 50 000 agents about 8 ms;
  targets that cross polygons and tiles every step cost the same; 10 000 agents spread over a
  2 km map after 10 targets about 4 ms; 10 000 horde agents among 500 crowd agents about 1.6 ms
  plus 1.8 ms for the crowd. 10 000 individual crowd agents in the same scenario took about 15 ms.
- On a phone (8 cores: one fast, three medium, four slow; the same level, steps on the calling
  thread): 1000 horde agents 0.16 ms, 5000 agents 0.8 ms, 10 000 agents 1.8 ms after one target
  and 2.2 ms after ten, with no step more than 10% above the median; 1000 individual crowd agents
  1.6 ms, 3000 agents 5.6 ms. With the worker pool the same passes took 4 to 5 times as long
  (10 000 horde agents 7 to 9 ms, 1000 crowd agents 7 ms, 40 000 horde agents 22 ms against 8.6 ms
  on the calling thread) with steps up to twice that.

`kb_engine_navigation-crowd-bench` checks that 1000 agents step within 8 ms, 10 000 agents within
24 ms, and that level of detail makes a large crowd cheaper; it prints the stages of a step.
`kb_engine_navigation-horde-bench` checks that 10 000 horde agents step within 3 ms (median) after
one target and 6 ms after 100, 50 000 within 30 ms, and that 10 000 agents spread over a 2 km map
step within 6 ms without waiting for fields; it also prints targets that cross polygons every step
and a horde among crowd agents.
The same gate checks a local rebake of four 12.8 m tiles containing a floor and a wall
against an 8 ms median budget over nine warmed samples. Larger geometry and wider
rebake regions need their own workload measurements.

## Limits

- Path requests, the path queue and topology optimisation run on the calling thread.
- A crowd walks one Detour mesh, so agents avoid and push only agents of their own profile.
- Horde agents are pushed apart by each other, by crowd agents and by non-carving obstacles; crowd
  agents do not see them. The route over the clusters ignores area masks: an agent whose mask
  forbids an area its route crosses reports a `Failed` path when its field cannot reach the gateway.
  Gateways are the widest opening of a pair of clusters, so routes follow tile edges and run a
  little longer than the shortest way.
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
| `kb_engine_tests navigation-mesh` | Settings validation; the asset format (round trip, truncation, refusal of escaping connections and unknown areas); bakes on one worker, four and a bake-owned pool byte-identical; walkable area of a floor; slopes (30 vs 60 degrees) and steps (0.3 vs 0.6 m); carving obstacles (blocked, around, rotated, removed) with an agent walking around; geometry rebuilt at runtime; area costs and masks; two agent sizes through a doorway; jump and ladder links (one-way, disabled) crossed by agents; far from the world origin; a 1200-agent crowd with near, mid and far agents replaying exactly on one, two and eight threads; crowd level of detail and its transitions; the path request budget serving near agents first and every agent in the end; a horde climbing stairs and a link onto a deck over shared fields, replaying exactly on one, two and eight threads, keeping its field for a destination moved within its polygon and following the old field while a new one grows; far horde agents setting off along the route with a tiny field budget; horde agents keeping off a crowd agent and steering around a non-carving obstacle; NavAgent (horde included), NavObstacle and NavLink save/load; Navigation Bounds limiting the bake (one box, two boxes, none), their settings (a wider agent leaves less ground, disagreeing bounds are refused), save/load and older files; moved, removed and added objects and a moved Navigation Bounds rebuilding only the tiles they touch into the full bake's tiles; a bake on a worker matching the synchronous bake of the saved scene; colliders and meshes with Affects Navigation off left out of full and partial bakes, kept through save/load, on in older files and set by scripts; the project's named areas (defaults, settings round trip, a scene's costs, a Not walkable area that is never crossed, names resolved by scripts); the debug view's triangles, polygon edges and tile bounds; scripts; a scene's ContentInstance placing its mesh; a world's tiles streaming in and out with its cells. |
| `kb_engine_tests navigation-runtime` | Agents on baked meshes: accelerating within their speed, stopping at their stopping distance, re-planning for a new destination, standing still while the scene is paused; turning corners without leaving the mesh; going around a carving obstacle and walking straight again once it is disabled; a partial path to the closest point when an obstacle closes the way; steering around a non-carving obstacle; two agents passing each other and replaying exactly, and still passing when created in the other order; no mesh means a failed path until one is added; walking a mesh 10 000 km out as precisely as at the origin, as a root and as a child. |
| `kb_engine_navigation-crowd-bench` | Crowd step budgets for 1000 and 10 000 agents, the level-of-detail gain at 5000, 20 000 agents over 600 steps, and `FindPath` cost. |
| `kb_engine_navigation-horde-bench` | Horde step of 10 000 and 50 000 agents chasing 1, 10 and 100 moving targets on a two-storey level (stairs, bridge, link) over 2000 steps (p50, p95, p99, slowest), individual crowd agents in the same scenario, targets crossing polygons every step, a horde among crowd agents, and 10 000 agents on a 2 km map. |
| `kb_cli_tests` | `kb_cli navmesh bake` and `info` (colliders, imported meshes, profiles, kept settings) and `world build` with navigation and region chunks. |
| `kb_editor_navigation_headless` | Editor bake of a scene with a floor collider, an imported mesh and a jump link; identical bytes to the kb_cli bake; the filled overlay drawn in the scene view, gone when hidden and hidden behind geometry in front of it; an agent crossing the link in Play mode. |
| `kb_fuzz_navmesh_corpus` (`KB_BUILD_FUZZERS`) | The navigation mesh reader and polygon rebuild over the seed corpus. |
