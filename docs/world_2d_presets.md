# Ready-made 2D players

`kb_cli entity add --create 'Platformer Player'` and `--create 'Top Down Player'` put a playable character into the
scene of a project that has a 2D map ([docs/world_2d_camera.md](world_2d_camera.md) describes the map camera):

```
kb_cli map2d new --project <dir> --name World --orientation wall          # a Map Platformer
kb_cli entity add --project <dir> --create 'Platformer Player' [--name Hero] [--position 2,3,0]
kb_cli run --project <dir> --frames 120
```

What the preset makes, in one command:

- An entity (named `Player` unless `--name` is given) with a **CharacterController** (a capsule; gravity on for the
  platformer, off for the top-down player, which stays at its level) and a **behaviour** that runs the project's copy
  of `Assets/Scripts/Platformer2DPlayer.lua` or `Assets/Scripts/TopDown2DPlayer.lua`. The Lua file is an ordinary asset of
  the project: edit it freely. It is written when missing and never overwritten.
- **Input**: when the project has no input context yet, `Assets/Input/Move`, `Jump` and `PlayerInput` are written and
  set as the project's input context. Move is WASD, the arrow keys, the pad's left stick and D-pad; Jump is Space and the
  pad's A button. A project that already has an input context keeps it; it then needs a `Move` (Axis2D) and a
  `Jump` (Bool) action.
- **Camera**: every map of the scene gets the player as its camera's follow target, smoothed and kept inside the map
  (`kb_cli map2d set --camera-follow none` and the other `--camera-*` options change it; add `--camera-pixels-per-unit 16`
  for pixel perfect).

## The platformer script

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

## The top-down script

Flat maps (`Map Top Down`): x runs across, z up the screen. `TopDown2DPlayer.lua` reads `Move` and moves with
`Physics.CharacterMove`, so solid tiles stop it: `speed` (5.0) and `eightWay` (true). A diagonal is normalized to the speed
of one axis; with `eightWay` false only the stronger axis counts (four directions).

## On the 2D physics (Box2D)

`--physics 2d` makes the same player on the 2D physics ([docs/world_2d_physics.md](world_2d_physics.md)) instead of the 3D one:
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

- The editor's Create menu does not list these items yet (it would need the project folder to write the script, and an
  editor build); use the command line.
- One-way platforms, slopes and moving platforms work in the character controller but are not exercised by the preset
  scripts' tests.
- The scripts assume the player's map is not rotated or scaled.
