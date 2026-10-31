# Ready-made 2D players

`kb_cli entity add --create 'Platformer Player'` and `--create 'Top Down Player'` put a playable character into the
scene. The same presets are in Create > 2D Object. They use the shared component authoring API and support a 2D map ([docs/world_2d_camera.md](world_2d_camera.md) describes the map camera):

```
kb_cli map2d new --project <dir> --name World --orientation wall          # a Map Platformer
kb_cli entity add --project <dir> --create 'Platformer Player' [--name Hero] [--position 2,3,0]
kb_cli run --project <dir> --frames 120
```

What the preset makes, in one command:

- An entity (named `Player` unless `--name` is given) with a **CharacterController2D** (a capsule; gravity on for the
  platformer, off for the top-down player, which stays at its level) and a **behaviour** that runs the project's copy
  of `Assets/Scripts/Platformer2DBox2DPlayer.lua` or `Assets/Scripts/TopDown2DBox2DPlayer.lua`. The Lua file is an ordinary asset of
  the project: edit it freely. It is written when missing and never overwritten.
- **Input**: when the project has no input context yet, `Assets/Input/Move`, `Jump` and `PlayerInput` are written and
  set as the project's input context. Move is WASD, the arrow keys, the pad's left stick and D-pad; Jump is Space and the
  pad's A button. A project that already has an input context keeps it; it then needs a `Move` (Axis2D) and a
  `Jump` (Bool) action.
- **Visual**: a Sprite2D with 32 pixels per unit, oriented for the selected mode; assign its image in the inspector or CLI.
- **Camera**: an orthographic child camera follows the player even without a map, becoming primary only when the scene has no primary camera. Every map of the scene gets the player as its camera's follow target, smoothed and kept inside the map
  (`kb_cli map2d set --camera-follow none` and the other `--camera-*` options change it; add `--camera-pixels-per-unit 16`
  for pixel perfect).

## Legacy 3D physics scripts (`--physics 3d`)

The explicit `--physics 3d` option retains the original CharacterController and scripts described below.

### The platformer script

Wall maps (`Map Platformer`): x runs across, y up. `Platformer2DPlayer.lua` reads `Move` (left and right) and `Jump`:

| `@expose` | Default | Meaning |
|---|---|---|
| `speed` | 6.0 | Walking speed in m/s |
| `jumpSpeed` | 9.0 | Upward speed at take-off (about 4 m high with a full jump) |
| `coyoteTime` | 0.1 | Seconds after walking off an edge in which Jump still works (0 to 0.5) |
| `jumpBuffer` | 0.12 | Seconds before landing in which a Jump press is remembered |
| `releaseGravity` | 3.0 | Gravity multiplier while rising with Jump released: a tap is a short hop |
| `fallGravity` | 1.5 | Gravity multiplier while falling |

Gravity, ground contact, walls, steps and slopes come from the CharacterController. A CharacterController jumps from the ground
only, as it always did. The platformer script asks the engine for coyote time for its own character with
`Physics.SetCharacterJumpGrace(entity, seconds)` (0 to 0.5 seconds, 0 switches it off again; anything else is refused with a
diagnostic): a jump asked for within that time of leaving the ground succeeds once, so a jump in the air, or a second one, does
not. Any script can use the function on any character.

### The top-down script

Flat maps (`Map Top Down`): x runs across, z up the screen. `TopDown2DPlayer.lua` reads `Move` and moves with
`Physics.CharacterMove`, so solid tiles stop it: `speed` (5.0) and `eightWay` (true). A diagonal is normalized to the speed
of one axis; with `eightWay` false only the stronger axis counts (four directions).

## On the 2D physics (Box2D)

The default (also selected explicitly with `--physics 2d`) makes the player on the 2D physics ([docs/world_2d_physics.md](world_2d_physics.md)) instead of the 3D one:
a **Character Controller 2D** (side view for the platformer, top down without gravity for the other) and the scripts
`Assets/Scripts/Platformer2DBox2DPlayer.lua` (`speed`, `jumpSpeed`, `jumpBuffer`) or `Assets/Scripts/TopDown2DBox2DPlayer.lua`
(`speed`, `eightWay`), which drive it with `CallFunction("Physics2D.MoveCharacter", ...)`, `Physics2D.IsGrounded` and
`Physics2D.JumpCharacter`. The input and the map cameras are the same. The tiles of the map stop it, because the 2D physics
makes the collision of a map into 2D shapes. The 2D platformer script has no coyote time and no variable jump height.

## Map bounds and runtime chunks

Maps support up to 4096 cells on each horizontal axis and floors from -50 to 50.
Cell coordinates start at zero. An import containing an out-of-range cell or oversized
dimensions is rejected as a whole. Painting entirely outside the map changes nothing;
a brush overlapping an edge applies only to cells inside the map. Resizing can be undone
to restore removed cells.

The map asset stores authored cells sparsely. Runtime geometry is grouped into 16 by 16
cell chunks per floor; an edit rebuilds affected chunks, including neighbouring borders.
These geometry chunks do not imply that the map asset itself is loaded on demand.

## Bounded top-down path queries

`kb_cli map2d path --project <dir> --map /Game/Maps/World.kbmap2d --from 1,1 --to 20,12`
returns JSON with cell-center waypoints, status, visited cells, elapsed milliseconds and
accumulated cost. Orthogonal steps cost 10; diagonal steps cost 14. Use `--diagonal safe`
to forbid corner cutting or `--diagonal free` to allow it. The default uses orthogonal
steps. `--clearance 0..8` requires a square footprint around each waypoint; the starting
cell is exempt so an agent can leave a tight space. `--floor` selects one floor.

`--cost-property <name>` reads a cell property or its graphic default as a multiplier,
clamped to 1..1000; missing or non-finite values use 1. Accumulated costs use 64 bits.
The command does not edit the map. It exits with 0 for a path, 2 for an unreachable or
blocked endpoint, 3 for an exhausted search budget and 1 for invalid input or I/O errors.

The default limits are 65,536 visited cells and 25 milliseconds. Override them with
`--max-visited 1..1048576` and `--timeout-ms` (positive, at most 60,000). JSON distinguishes
`time` and `visited` budget exhaustion from `no-path`: exhaustion does not prove that
a route is impossible. Time is checked between search expansions, so this is a cooperative
search budget, excluding asset loading and JSON output, rather than a process deadline.
The C++ API exposes the same timing and cost diagnostics; its default time limit is disabled.

`Map2D.NextStep` also accepts optional `clearance`, `diagonal` (0 orthogonal, 1 safe,
2 free), `costProperty`, `maximumVisited` and `maximumMilliseconds` arguments. Its
existing defaults and four outputs remain unchanged, with zero disabling the time limit.
`Map2D.NextStepDetailed` takes the same arguments and adds outputs `budgetExceeded`,
`cost` and `elapsedMilliseconds` so scripts can detect incomplete
queries. Queries use the live map, including cells changed by script since loading.

## Not covered

- The scripts assume the player's map is not rotated or scaled.

## Reproducible sample workflows

`ctest --test-dir <build> -R kb_editor_player2d_workflow_package --output-on-failure`
creates a small platformer and a top-down project through the editor authoring API.
Both use Sprite2D, native 2D collision, the preset controller, a following camera,
a goal script and a HUD. The test drives keyboard input to clear or walk around an
obstacle, reaches the goal, round-trips a player prefab, and repeats the input through
the CLI and a cooked Windows player with project source files unavailable.
The printed output directory retains both projects, command logs and captures.

## Map camera settings

The map editor's additional tools menu includes **Play camera...**: target entity name,
zoom, pixel density, smoothing, horizontal/vertical drag margins and map bounds.
These settings are saved in the map and participate in Undo/Redo. A blank target
keeps a fixed camera; bounds and zoom still apply. Both orientations use the same settings.

`kb_cli map2d set --project <project> --map <map> --camera-zoom 2` doubles the requested
magnification. `--camera-pixels-per-unit 16` quantizes magnification to whole screen
pixels per texel and snaps camera motion to the resulting pixel grid. Set density to
zero for continuous zoom. Resize changes the visible area while retaining square pixels.

`kb_editor_camera2d_workflow_package` checks the settings through the headless editor,
CLI round-trip and cooked pixel readback in both orientations, with fractional camera
motion and multiple viewport sizes/aspect ratios. `kb_editor_map2d_visual_package`
checks atlas spacing, edge sampling and transformed graphics in cooked output.

## Platformer navigation and surfaces

Create **Platformer Navigator** from the 2D Object menu or with
`kb_cli entity add --create "Platformer Navigator"`. It uses the native 2D controller
and a project-owned Lua behaviour. Set its exposed map name, target cell, movement
speed and jump limits. The target row is the empty cell immediately above its floor.
The navigator walks and jumps using the physics API, holds its landing point while
in flight, then replans against the live map. It reports moving, reached or unreachable
in the script log. `Map2D.PlatformerWaypoint` is also available to custom behaviours.
Use wall maps in their unrotated, unit-scale plane; jump limits must fit the character's
jump speed and gravity. The controller's collision remains authoritative.

The platformer player uses the same controller for slopes, one-way tile platforms and
translating platforms. A grounded character follows platform translation; a jump detaches it.
`kb_editor_platformer_navigation_package` verifies an initially unreachable goal, a live
map edit and a physical gap crossing without teleporting. `kb_editor_platformer_surfaces_package`
replays keyboard input on a slope, through a one-way platform and on a moving platform.
Both tests run through GUI authoring, CLI and cooked output with source files unavailable.

## Tile data and prefab streaming

The map editor's **Property schema...** command declares a name, type (`bool`, `int`,
`float`, `string`) and default. Cell overrides take precedence over graphic defaults,
then schema defaults. Existing undeclared fields remain supported. Number editing uses
whole numbers for declared integer fields. **Rename from** migrates overrides and
increments the schema revision; integer/float conversions must preserve every value.
An incompatible conversion leaves the entire map unchanged. Undo/Redo includes the
schema and migrated cell values.

The CLI equivalent is `kb_cli map2d schema --project <project> --map <map> --name rank
--type float --default 2.5 --rename-from level`. `Map2D.GetBool`, `GetInteger`,
`GetNumber` and `GetText` return the same effective values in the packaged runtime.

Prefab cells instantiate once per cell, preserving the prefab child hierarchy.
`Map2D.SetPrefabRegion` accepts `entity`, inclusive `minX/minY/maxX/maxY` cell bounds
and `budget` (1..4096, default 256). It limits prefab instances, leaving authored map
cells, graphics and collision available. An inverted region unloads all prefab cells;
expanding it restores them from their saved prefab references. Creation and removal
share the per-frame budget; call `Map2D.PrefabCount` to observe progress. Scene shutdown
releases all remaining owned instances. `kb_editor_tile_data_package` repeats schema
editing and streaming through the headless editor, CLI and cooked player.
