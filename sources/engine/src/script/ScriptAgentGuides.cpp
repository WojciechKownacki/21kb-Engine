#include "engine/script/ScriptAgentGuides.hpp"

#include "engine/script/ScriptNameSuggestion.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace kb::script {
namespace {

constexpr std::string_view kMaterials = R"md(# Materials: kinds, blend modes, phones

Pick the kind first (`material-graph setting --name Domain`, or `Domain = ...` in graph text), then the blend mode.

| Domain | For | Outputs it uses |
|---|---|---|
| surface | anything on a mesh: walls, skin, water, glass, glow | BaseColor, Metallic, Roughness, Emissive, Normal, Alpha, WorldPositionOffset (wind, wobble) |
| userInterface | a UI Image (`component set --type Image --set materialAssetId=<material>`) | Emissive is the colour, Alpha the opacity; BaseColor is refused |
| postProcess | the whole screen after drawing: tints, night vision, vignette; on an object with a Mesh Renderer and no mesh | Emissive is the result; reads SceneColor, so it is heavy on phones |
| deferredDecal | paint on the surfaces inside a box (a Cube with the material) | BaseColor, Normal, Roughness; no vertex outputs |

Shading model (`Shading Model`): defaultLit (normal), unlit (flat colour or glow, no lighting: toon, hologram, UI-like), clearCoat (car paint),
subsurface (skin, wax), cloth, hair, eye, singleLayerWater, thinTranslucent. Others are refused by name.

Blend mode (`Blend Mode`): opaque (default, cheapest), masked (cut-outs: the Alpha output against AlphaClipThreshold; leaves, dissolve, fences),
translucent (glass, water: Alpha is the opacity), additive (glow, fire, hologram: adds light), modulate (darkens: smoke, shadows).
Add `Two Sided = true` for leaves, glass, flags and anything seen from behind.

Other kinds: terrain layers and particle cards take ordinary surface materials (`guide terrain`, `guide ui-audio-particles`); the sky is the
Environment component, not a material (`recipe-light`); a reusable piece of graph is a material function (`recipe-material-function`).

Phones and older Mali GPUs (the limits `material-graph show` checks, "Warning: too heavy for a phone"):
- Under 120 instructions per pixel and 6 texture samples (a material may use 15 samplers at most).
- No SceneColor, SceneDepth, DepthFade: they copy the screen. Post Process materials always read it; use them sparingly.
- Noise with 1-2 levels and the value or gradient function; Voronoi and many levels are the usual cause of a heavy material.
- No Parallax Occlusion Mapping (use Bump Offset). Prefer opaque or masked; a big translucent surface costs overdraw.
- Hair, eye and subsurface shading cost more than defaultLit.

Techniques the graph does well (ready ones: `guide` with `search`): outline and highlight through walls (`recipe-outline`), toon and rim light, fresnel glow, dissolve, hologram and glitch, emission and
pulsing, scrolling UV, wind and wobble (vertex offset, set Max World Position Offset Displacement), snow on top by the normal, wet surfaces, terrain
by height and slope (world position and normal, noise to break the borders; ten textured layers: `recipe-terrain-layers`), lava, water (`recipe-water`), glass, ice. Needs the scene behind (translucent or
post-process only): foam and depth colour (DepthFade), refraction (SceneColor), screen distortion.

Not in the graph, so do not try: stencil,
more than the main light in a material, reading other objects' positions (grass that bends from the player: set a parameter from a script),
ray marching. Graph text cannot write a material function call (`material-function use` adds one); textures need images in the project
(triplanar: WorldAlignedTexture; many terrain layers: one 2D array texture with TextureSample2DArray, or at most 9 samplers).
)md";

constexpr std::string_view kObjectsAndMaterials = R"md(# Objects and materials

Each line is one `kb_cli` command (through the kb-engine `kb_cli` tool: the words after `kb_cli`
as `args`). Commands change the startup scene unless you add `--scene <path>`; commands sent
together are safe, edits of one scene simply wait for each other.

One command makes a shape with its colour (a material named after it) and, if wanted, physics:

```
kb_cli entity add --create Sphere --name RedBall --position 0,5,0 --color "#E03C31" --body Dynamic
```

- `--create`: Cube, Sphere, Capsule, Cylinder, Cone, Plane, Quad, Directional Light, Point
  Light, Spot Light, Area Light, Camera, Environment, Particle System, Audio Source, Audio
  Listener, UI items (Canvas, Text, Button, ...). Shapes come with a Mesh Renderer and a Collider.
- `--position x,y,z`, `--rotation x,y,z` (degrees; forward is +Z, `90,0,0` faces straight
  down), `--scale x,y,z`, `--parent <Name>`, `--look-at x,y,z|<Name>` (aims forward at a point
  or an object: cameras, spot lights).
- `--color` makes `/Game/Materials/<Name>.kbmat`; `--material /Game/...kbmat` uses an existing
  one instead. `--body Static|Dynamic|Kinematic` adds a Rigidbody.
- More material fields: `kb_cli material-new --name Gold --set "Base Color=#D4AF37" --set Metallic=1 --set Roughness=0.3`,
  later `material-set --material /Game/Materials/Gold.kbmat --set ...`. Fields: Base Color,
  Roughness, Metallic, Emissive Color, Emissive Strength, Normal Scale, Occlusion Strength,
  Alpha Cutoff, and textures (Base Color Texture, Normal Texture, ... = `/Game/Textures/x.png`).
- Move, turn or aim later: `kb_cli entity set --name "Main Camera" --position 0,4,-8 --look-at RedBall`.
- `--parent` makes positions, rotation and scale relative to the parent: a child of a scaled object is
  scaled too. Build an object from parts under an unscaled empty entity (`entity add --name Table`), each
  part with its own `--scale`; `entity set --name Leg1 --parent none` moves one back to the root.
- Change anything later: `kb_cli component set --entity RedBall --type Transform --set localPosition=0,3,0`
  (any component: `--type MeshRenderer --set material=/Game/Materials/Gold.kbmat`,
  `--type Light --set intensity=8 --set range=10`).
- Read the scene: `kb_cli scene-list` gives every object with its position
  (`@ x,y,z`); `kb_cli entity show --name RedBall` gives one object's transform and
  every component value.
- A camera from above: `kb_cli entity add --create Camera --name TopCamera --position 0,12,0 --look-at 0,0,0`,
  then `component set --entity TopCamera --type Camera --set primary=true` and the old camera
  `primary=false`.
- Remove or rename: `entity remove --name RedBall`, `entity rename --name RedBall --to Ball`.
- Reuse an object: `kb_cli prefab new --from-entity Crate` saves `/Game/Prefabs/Crate.kbprefab`;
  `prefab place --prefab /Game/Prefabs/Crate.kbprefab --name Crate2 --position 3,1,0` adds a copy,
  and a script spawns one with `World.InstantiatePrefab("/Game/Prefabs/Crate.kbprefab", nil, nil, x, y, z)`.
)md";

constexpr std::string_view kPhysics = R"md(# Physics

A ball that falls and collides, in one command (shapes already have a Collider):

```
kb_cli entity add --create Sphere --name Ball --position 0,5,0 --color "#E03C31" --body Dynamic
kb_cli run --frames 120
```

`run` ends with a `moved:` line per dynamic body (`Ball 0,5,0 -> 0,0.5,0`): the proof it fell.

- Rigidbody: `bodyType` Static, Dynamic or Kinematic; `mass`, `gravityScale`, `useGravity`,
  `linearDamping`, `angularDamping`, `lockRotation`, `useContinuousCollision`,
  `linearVelocity=x,y,z`. Add one later: `component add --entity Crate --type Rigidbody --set bodyType=Dynamic`.
- Collider: `shape` (Box, Sphere, Capsule, Mesh), `boxSize=x,y,z`, `radius`, `height`,
  `center=x,y,z`, `trigger`, `friction`, `restitution` (bounce, 0..1). An entity has one
  Collider: change it with `component set`, not `component add`.
- Something that never moves (a floor) needs no Rigidbody, only its Collider.
- A slope: a Cube tilted about Z, e.g. `--create Cube --name Ramp --position 0,2,0 --rotation 0,0,-20 --scale 8,0.4,3`,
  with the ball above its high end (`-3,4.2,0`). The new scene's floor `Plane` is 10x10; a ball that
  rolls on leaves it, so widen it first: `entity set --name Plane --scale 3,1,3`.
- A stack or a row in one command: `entity add --create Cube --name Crate --position 0,0.5,0 --count 10 --offset 0,1,0 --body Dynamic`
  makes Crate1..Crate10.
- A character that walks: `entity add --create Capsule --name Hero --position 0,1,0`, `component add --entity Hero --type CharacterController`,
  `input-action new --name Move --type Axis2D --key W=+y --key S=-y --key D=+x --key A=-x` and in its script
  `local m = Input.Vector2("Move"); Physics.CharacterMove(self.entity, m.x * 5, m.y * 5)` (X and Z speed). One that
  follows it: `local t = Transform.GetPosition(World.FindByName("Hero"))`, then CharacterMove toward `t` and
  stop (CharacterMove 0, 0) closer than 1.5 m - characters collide, so one that keeps walking pushes the other.
  2D players: `--create "Top Down Player"` or `"Platformer Player"`.
- Scripts: `function OnCollisionEnter(self, event)` for a solid contact and `function OnTriggerEnter(self, event)` for
  a Collider with `trigger=true` that nothing bumps into (the other entity is `event.args.other`; a pickup: guide
  `recipe-pickup`; both also have Stay and Exit); raycasts and forces: `api_reference` topic `Physics`.
)md";

constexpr std::string_view kScripts = R"md(# Behaviour scripts (Lua)

A behaviour is a `.lua` file under `Assets/` attached to an entity. Lifecycle hooks are global
functions named after the event, each receiving `(self, dt)`: Created, Activated, Ready, then
every frame FixedTick, Tick, LateTick, BeforeRender, AfterRender; Deactivated and Destroyed.

```lua
-- @expose speed Float = 6.0

function Tick(self, dt)
    local move = Input.Vector2("Move")
    local x = self:GetProperty("Transform", "localPosition.x")
    self:SetProperty("Transform", "localPosition.x", x + move.x * self:GetVariable("speed") * dt)
end

function DoorOpened(self, event) -- a custom event, receives (self, event)
    Log(event.args.door)
end
```

The engine also calls `function Name(self, event)` when something happens, with its values in `event.args`:
`OnTriggerEnter/Stay/Exit` and `OnCollisionEnter/Stay/Exit` (`other`), `OnAnimationEvent`, `OnAudioMarker`,
`OnParticleSystemFinished`, `OnTimelineEvent`, `OnDamaged`, `OnUIClicked`, ... `api_reference` topic `EventHooks` lists
every one with its arguments, and a topic of a hook name (`OnTriggerEnter`) shows one. A hook with another name is never
called: `validate` warns about it and names the closest. A coin that is collected by touching it: `recipe-pickup`.

```
kb_cli script-new --name Spinner --language lua --folder Assets/Scripts
kb_cli scene-attach --scene Assets/Scenes/Main.21kbscene --node RedCube --script /Game/Scripts/Spinner.lua
kb_cli validate Assets/Scripts/Spinner.lua
```

- Keys and buttons are input actions: `kb_cli input-action new --name Fire --key Space --key MouseLeft`
  (`--type Axis2D --key W=+y --key S=-y --key D=+x --key A=-x` for movement); scripts read
  `Input.Pressed("Fire")` and `Input.Vector2("Move")`; `input-action list` shows what exists.
- An object runs up to 8 scripts: `scene-attach` again with another script adds it, `scene-detach --node <Name> --script <path>` takes
  one off, and the Inspector lists them. Each script has its own `self` and its own variables; an event for the object (`EmitTo`, a
  touch, a click) reaches every script that handles it; they run by tick group and `--execution-order`, then in the order attached.
- C++ scripts (`script-new --language cpp`, `script-build`) are for developers: they need the engine SDK and a C++ toolchain (CMake and
  the Visual Studio C++ tools), and fail at once without them. A game's scripts are Lua: use `--language lua`.
- `-- @expose <name> <Type> [= default]` makes a variable editable in the Inspector; read it with
  `self:GetVariable`. `kb_cli scene-attach ... --set speed=8` gives one node its own value for it.
- Component properties: `self:GetProperty(component, property)` / `self:SetProperty(...)`; each
  component's properties are an `api_reference` topic (for example `Transform`).
- The engine functions (`Log`, `Emit`, `Transform`, `World`, `Math`, ...) do not exist while the file loads: call them inside a hook
  (`Ready`, `Tick`, an event function), never on a line at the top level. There only definitions and `Inspector.speed = 5.0` work.
- Every global has its own `api_reference` topic: `Log`, `Emit`, `EmitTo`, `Import`, `SetShared`, `GetShared`,
  `CallFunction`, `Events`, `Inspector`, `assert` (topic `Globals` lists them).
- Engine functions are grouped (`Input`, `Physics`, `Transform`, `World`, `Audio`, `Gameplay`,
  `Animator`, `Math`, `Timer`, ...): ask `api_reference` for a group, then for the one function
  you call. Every function is `Group.Name(inputs in order)`, e.g. `Transform.Rotate(self.entity, 0, 90 * dt, 0)`
  (degrees), `Transform.SetRotation(e, 0, 45, 0)`, `Physics.AddImpulse(e, 0, 5, 0)`; several
  outputs come back as a table (`Transform.Rotation(e).y`). `validate` flags a function that
  does not exist.
- The sandbox has no `io`, `os`, `require` or `load`; share code with `-- @import` modules and
  `Import("Module.Name")`.
- Write the Lua every platform runs (LuaJIT or Lua 5.4, chosen by the platform): Lua 5.1 with the `bit`
  library and `unpack`. No `//` (use `math.floor(a / b)`), no `&` `|` `~` `<<` `>>` (use `bit.band`,
  `bit.bor`, `bit.bxor`, `bit.lshift`, `bit.rshift`), no `<const>`, no `utf8`, no `math.type`; every number is
  a double, exact up to 2^53. `validate` warns with file and line about each construct outside that subset.
- Ready-made controllers live in `Assets/Samples/` (third person, top down, platformer,
  shooter).

A script is done when `validate` says OK and `kb_cli run --frames 120` reports no
script diagnostics (`run` exits 1 when it reports any). `validate` also fails a global, function, component or property
that does not exist, with the nearest real name, and a call with the wrong number of arguments.
To test logic without a window write `assert(double(4) == 8, "double(4) must be 8")` in `Ready`: a failed assert is a script
error with file and line, so `run` exits 1; `run --json` lists the errors as data.
`kb_cli capture --out shot.png` renders a frame headless to check what a scene looks like;
`kb_cli build --out Build` cooks a folder the game runs from.

## C++ scripts

`kb_cli script-new --name Spinner --language cpp` makes `/Game/Scripts/Spinner.native`
and its source `Source/NativeScripts/Spinner/Spinner.cpp`; `kb_cli script-build`
compiles it (Play and `run` build it too), `scene-attach` attaches the `.native` asset. The same
functions are called by name, inputs by name, results read with `Output`:

```cpp
static void Tick(kb::script::ScriptExecutionContext* context) {
    using kb::script::ScriptFunctionArgument; using kb::script::ScriptValue;
    const ScriptValue self{ context->Self().Id(), kb::script::ScriptValueType::Entity };
    const std::array turn{ ScriptFunctionArgument{ "entity", self }, ScriptFunctionArgument{ "x", ScriptValue{ 0.0F } },
        ScriptFunctionArgument{ "y", ScriptValue{ 90.0F * context->DeltaSeconds() } }, ScriptFunctionArgument{ "z", ScriptValue{ 0.0F } } };
    static_cast<void>(context->CallFunction("Transform.Rotate", turn)); // degrees
    const std::array who{ ScriptFunctionArgument{ "entity", self } };
    const float yaw = context->CallFunction("Transform.Rotation", who).Output("y")->AsFloat();
}
```

Register each hook in `kb_register_native_scripts` with `ScriptLifecycleEvent::Tick` (the
generated file shows `Ready`). A failed call returns `errors` (a missing input names it).
)md";

constexpr std::string_view kCharacters = R"md(# Characters and animation

```
# A character the player walks with (WASD or arrows, Space jumps) and the camera the game shows, in one
# step: its controller script, input and camera come with it. Then give it a model or colour as below.
kb_cli entity add --create 'Third Person Player'     # camera behind it
kb_cli entity add --create 'First Person Player'     # through its eyes, mouse look

# A character from Blender (glTF/GLB or FBX): Skeleton, Skeletal Mesh and one Animation Clip per
# action, named <file>_<action>.
kb_cli import --destination /Game/Characters Hero.glb
kb_cli entity add --name Hero
kb_cli component set --entity Hero --type MeshRenderer --set mesh=/Game/Characters/Hero.kbskeletalmesh

# A state machine: one state per clip, a speed blend, transitions; check it before use.
kb_cli animator new --name Hero --clip /Game/Characters/Hero_Idle.kbanim --clip /Game/Characters/Hero_Run.kbanim
kb_cli animator locomotion --controller /Game/Animation/Hero.kbanimcontroller --parameter Speed --clip 0=/Game/Characters/Hero_Idle.kbanim --clip 4=/Game/Characters/Hero_Run.kbanim
kb_cli animator check --controller /Game/Animation/Hero.kbanimcontroller --character /Game/Characters/Hero.kbskeletalmesh
kb_cli component add --entity Hero --type Animator --set controller=/Game/Animation/Hero.kbanimcontroller

# A sword in the right hand: a socket on a bone, then attach an entity to it.
kb_cli skeleton show --skeleton /Game/Characters/Hero.kbskeletalmesh
kb_cli skeleton socket add --skeleton /Game/Characters/Hero.kbskeletalmesh --name weapon_r --bone hand_r --rotation 0,0,90
kb_cli entity add --name Sword --create Cube
kb_cli entity attach --name Sword --to Hero --socket weapon_r --scale 0.05,0.8,0.05
```

- Clips of another rig (common rig naming schemes) play too: bones are matched by name and role;
  `animator check --character` shows how many bones follow each clip.
- Scripts drive animation with `Animator.<Function>(inputs in order)`; the functions are
  `api_reference` topic `Animator`.
)md";

constexpr std::string_view kCombatAndAi = R"md(# Combat and AI

```
# An ability and a Combatant (health, mana, resistances, team, senses, 8 ability slots).
kb_cli ability new --name Fireball --set damageType=Fire --set amount=40 --set range=12 --set radius=3 --set manaCost=10 --set animatorTrigger=Cast
kb_cli component add --entity Hero --type Combatant --set team=1 --set maximumHealth=200 --set maximumMana=50 --set ability1=/Game/Abilities/Fireball.kbability

# A whole AI in one line: Combatant + AI Brain from a preset (Melee, Ranged, Guard, Coward, Wanderer).
kb_cli entity add --name Goblin --create Capsule --position 0,1,8
kb_cli ai make --entity Goblin --preset Melee --health 60
kb_cli ai rule --entity Goblin --rule 1 --when HealthBelowPercent=20 --do Flee
kb_cli ai show --entity Goblin
```

- Teams 1 and 2 are enemies; 0 is nobody's.
- The AI Brain is up to 8 rules, "when <condition> do <action>", checked from the top; `--rule N`
  replaces rule N, and rules after an `Always` rule never run. Conditions: Always SeesEnemy
  KnowsEnemy EnemyCloserThan EnemyFartherThan HealthBelowPercent ManaAbovePercent AbilityReady
  FarFromHome. Actions: Idle Wander ReturnHome Chase UseAbility KeepDistance Flee. It walks the
  navigation mesh with a NavAgent and drives the Animator (float `Speed`, triggers `Attack`,
  `Hit`, `Die`, bool `Dead`).
- Scripts call the gameplay functions directly, inputs in order or one table by name:
  `Gameplay.ApplyDamage({ target = enemy, amount = 10 })`; the functions are
  `api_reference` topic `Gameplay`. A combatant's script receives `OnDamaged`, `OnHealed`,
  `OnDeath`, `OnRevived`, `OnAbilityUsed`, `OnEnemySpotted`, `OnEnemyLost` with details in
  `event.args`.
)md";

constexpr std::string_view kMaps2D = R"md(# 2D maps and platformers

A World 2D Map is a tile map: grounds with automatic borders, sprites as items, floors -50..50
(0 is the ground floor).

```
# The map entity, its Mesh Renderer and a Map Camera; --orientation wall hangs it like a picture.
kb_cli map2d new --name World --size 64x64 --view 15x11
# Sprite sheets (PNG, cells row by row from 0) go into Assets/2D Map Sprites/<Category>.
kb_cli map2d ground new --name Grass --texture "/Game/2D Map Sprites/Ground/terrain.png" --island 16 --hole 19 --z 30
kb_cli map2d ground new --name Dirt --texture "/Game/2D Map Sprites/Ground/terrain.png" --tiles 0,1 --weights 3,1 --z 10
kb_cli map2d object new --name Oak --texture "/Game/2D Map Sprites/Nature/trees.png" --cell 0 --size 2x2
kb_cli map2d fill --map /Game/Maps/World.kbmap2d --ground Dirt --at 0,0
kb_cli map2d paint --map /Game/Maps/World.kbmap2d --ground Grass --at 10,10 --to 20,14
kb_cli map2d paint --map /Game/Maps/World.kbmap2d --object Oak --at 14,12
kb_cli map2d info --map /Game/Maps/World.kbmap2d
```

- A higher ground `--z` draws its edges over lower grounds; `--at x,y,z` paints another floor.
- Tile (x, y) of a flat map lies at X = x + 0.5 - width/2, Z = height/2 - y - 0.5 relative to its
  entity; floor z is z * 3 m up.
- 2D players: `kb_cli entity add --create "Platformer Player"` or `"Top Down Player"`.

A 2D platformer without a tile map: the player preset, then platforms in the X/Y plane with 2D
physics, each one command (its Collider 2D follows `--scale`, so leave its size at 1x1):

```
kb_cli entity add --create "Platformer Player" --position 0,2,0
kb_cli entity add --create Cube --name Ground --position 10,-0.5,0 --scale 40,1,1 --color "#4C8C3A" --body2d Static
kb_cli entity add --create Cube --name Platform1 --position 6,1.5,0 --scale 4,0.5,1 --body2d Static
```

The preset's camera follows the player; `run` prints where the player moved.
)md";

constexpr std::string_view kVerify = R"md(# Checking your work

Always finish with these, and fix what they report:

```
kb_cli validate Assets/Scripts/MyScript.lua   # syntax and sandbox load of a script
kb_cli run --frames 120                        # the startup scene headless
kb_cli scene-list --scene Assets/Scenes/Main.21kbscene   # what the scene holds
```

- `run` prints script logs, events and errors; a change is done when it ends with `0 diagnostics` (any error: exit code 1).
  `run --json` returns the errors as a list with `file`, `line`, `message` and `traceback`.
- A script tests itself with `assert(condition, "message")` in `Ready` or `Tick`: a failed assert is a script error with
  its line, so the run fails.
- `run` ends with `moved:` lines: where each dynamic body, character and scripted object went.
- A key press for a script: `run --press Space@30` holds Space at frame 30 (`--press E@10+20` for
  20 frames); the `moved:` and log lines show what it did.
- `run --report input` or `--report particles` prints a versioned report for those systems.
- Scenes, projects and assets are binary: change them only through `kb_cli` or the kb-engine
  tools, never by editing or parsing the files.
)md";

constexpr std::string_view kTerrain = R"md(# Terrain

A terrain is a heightmap file (`.kbterrain`) drawn by an entity's Mesh Renderer: up to 4 material
layers painted on it, holes cut through it. `kb_cli terrain` edits the file with the Terrain
Editor's own brushes, generators and erosion, so the editor opens what you made. Positions `x,z`
are world units from the terrain's centre (`terrain info` prints the extent); heights are world units.

```
# A whole world in one command: mountains, cut by rivers (also plains, hills, valley, island, plateau; `--character 0..1` is gentle to dramatic).
kb_cli terrain new --name World --size 1024 --resolution 513 --preset mountains --seed 7
# Or a flat terrain, shaped by hand.
kb_cli terrain new --name Hills --size 256 --resolution 257
kb_cli terrain brush --terrain /Game/Terrains/Hills.kbterrain --mode raise --x 0 --z 0 --radius 30 --strength 4
kb_cli terrain brush --terrain /Game/Terrains/Hills.kbterrain --mode raise --path "-80,-60;0,0;80,60" --radius 12 --strength 2
kb_cli terrain brush --terrain /Game/Terrains/Hills.kbterrain --mode noise --x 0 --z 0 --radius 90 --strength 3 --noise-scale 24
kb_cli terrain brush --terrain /Game/Terrains/Hills.kbterrain --mode ramp --x -60 --z 0 --to 60,0 --radius 6
kb_cli terrain generate --terrain /Game/Terrains/Hills.kbterrain --kind ridged --seed 3 --amplitude 80 --scale 120
kb_cli terrain erode --terrain /Game/Terrains/Hills.kbterrain --mode hydraulic --iterations 150
kb_cli terrain layer add --terrain /Game/Terrains/Hills.kbterrain --material /Game/Materials/Grass.kbmat
kb_cli terrain layer add --terrain /Game/Terrains/Hills.kbterrain --material /Game/Materials/Rock.kbmat
kb_cli terrain paint --terrain /Game/Terrains/Hills.kbterrain --layer 1 --auto slope=35..90
kb_cli terrain preview --terrain /Game/Terrains/Hills.kbterrain --out Exports/hills.png --kind height
kb_cli terrain info --terrain /Game/Terrains/Hills.kbterrain
```

- Brush modes: `raise lower smooth flatten noise terrace cut-hole fill-hole ramp stamp`. Strength
  is per stamp; a `--path` stamps every `--spacing` (a quarter of the radius by default), so a stroke
  does not depend on how many points describe it. `--invert yes` is the editor's Ctrl, `--symmetry
  x|z|xz` mirrors the stroke across the centre planes, `flatten --height sample` flattens to the
  height under the start, `stamp --image height.png` presses a greyscale picture into the ground.
- `generate` makes heights from noise (`fbm`, `ridged`, `island`, `plateau`, `valley`; same seed, same ground,
  on any number of cores), `--add yes` layers it over what is there. `erode` runs water (`hydraulic`:
  channels and fans) or slumping (`thermal`: slopes settle to a talus angle). They run on every
  core; `--threads <n>` caps that and `--max-seconds <n>` stops a long run and saves nothing.
- `apply --ops` runs many strokes in one save. `ops.json` is a list of objects with the same words
  as the options: `[{"op":"brush","mode":"raise","x":0,"z":0,"radius":20}, {"op":"paint","layer":1,
  "path":"0,0;40,0"}, {"op":"auto-paint","layer":1,"slope":"35..90"}]`. A wrong member fails the
  whole batch and leaves the file as it was.
- A stroke that changes nothing (outside the terrain, no effect) is an error, so a wrong position
  is noticed. `terrain sample --x --z` reads height, slope and layer weights back; `terrain info`
  prints `heightHash`, equal for equal heights, to compare two runs; `terrain preview` draws the
  height, slope or layers to a PNG you can look at.
- `import` turns a 16-bit PNG or RAW heightmap into a terrain, `export` writes one (it prints the
  `--min`/`--max` that import needs to restore the heights). Resolution is 2^n+1 (17 to 2049).
- `kb_cli help terrain` lists every option.
)md";

constexpr std::string_view kMenusAndButtons = R"md(# Menus, buttons and UI events

A menu is a Canvas with widgets under it; a button does something only when a script answers it.

```
kb_cli entity add --create Canvas --name Menu
kb_cli entity add --create Button --name PlayButton --parent Menu
kb_cli component set --entity Menu/PlayButton --type Text --set "content=Play"
kb_cli component set --entity Menu/PlayButton --type Selectable --set eventName=PlayClicked
kb_cli script-new --name MenuController --language lua
kb_cli scene-attach --scene Assets/Scenes/Main.21kbscene --node PlayButton --script /Game/Scripts/MenuController.lua
kb_cli validate Assets/Scenes/Main.21kbscene Assets/Scripts/MenuController.lua
```

```lua
function PlayClicked(self, event)        -- the button's eventName: a function of that name
    Log("play pressed at " .. event.args.pointerX .. "," .. event.args.pointerY)
end

function OnUIClicked(self, event)        -- every click on the widget, with the name in event.args.action
    Log("clicked " .. event.args.action)
end
```

- A click sends two things to the widget's script: `OnUIClicked(self, event)`, and, when the
  Selectable has an `eventName`, an event of that name (a plain identifier: `PlayClicked`, not
  `Menu.Play`, unless you read it from `event.args.action`). Set `eventTarget` on the Selectable to
  send the named event to another object, for example the script on the menu root.
- Every widget callback (OnUIHoverEntered, OnUIPressed, OnUIChanged, OnUIDragged, ...) and its `event.args`
  (`entity`, `pointerX`, `pointerY`, `value`, `text`, `action`, `other`): `api_reference` topic `UIEvents`.
- Long lists: on a Scroll View set `virtualizeList`, `listItemCount` and `listItemHeight`; its first child is the
  row template, and the view keeps rows only for the items in view. Fill each row in `OnUIItemBound(self, event)`
  (`event.args.other` is the row, `event.args.value` the item index) on the script of the view.
- Change widgets from the script: `self:SetProperty("Text", "content", "Score: 3")`, or another
  object with `World.FindByName` and `World.SetPropertyFloat`.
- `validate` warns about what loads fine but never works: a widget outside a Canvas
  (`ui-no-canvas`), a Text without a font (`ui-text-no-font`), an `eventName` no script answers
  (`ui-event-no-handler`) and a Button that reacts to nothing (`ui-button-no-reaction`). `run` and the
  cook print the same warnings.
- `entity add --create Button` (or Text, Slider, ...) puts the widget under a Canvas, creating one when the scene has none, and gives
  every Text the bundled font; `component set ... --set eventTarget=Menu/Root` takes an object by its path.
- Whole screens: `kb_cli entity add --create "Main Menu"` (also `Pause`, `HUD`, `Settings`) builds the title, buttons,
  health bar or sliders at once; the buttons carry event names (`PlayClicked`, `SettingsClicked`, `QuitClicked`, `ResumeClicked`,
  `MainMenuClicked`, `BackClicked`) for your script to answer.
- A button that opens another scene calls `Scene.Load("/Game/Scenes/Level2.21kbscene")` in its function; that scene must be in the build
  (`project-settings scenes --add Level2`), or the built game cannot open it.
- Try the menu without a window: `kb_cli run --frames 12 --click Menu/PlayButton@5` presses the widget on frame 5 and says
  what was under the pointer and which script answered; `--report ui` lists every widget with its rectangle and its answering scripts.
)md";

constexpr std::string_view kUiAudioParticles = R"md(# UI, sound and particles

```
# A "Score: 0" label on screen: a Canvas with a Text under it.
kb_cli entity add --create Canvas --name HUD
kb_cli entity add --create Text --name Score --parent HUD
kb_cli component set --entity HUD/Score --type Text --set "content=Score: 0" --set fontSize=32

# A sound on start: a .wav under Assets/ (copied there or `kb_cli import`).
kb_cli entity add --create "Audio Source" --name Music
kb_cli component set --entity Music --type AudioSource --set clipAssetId=/Game/Audio/Music.wav --set autoplay=true --set loop=true

# Particles: a particle effect asset, then a Particle System that plays it.
kb_cli vfx-new --name Sparks
kb_cli entity add --create "Particle System" --name Sparks --position 0,1,0
kb_cli component set --entity Sparks --type "Particle Effect" --set effectAssetId=/Game/Particles/Sparks.kbvfx
```

- UI items for `--create`: Canvas, Text, Image, Button, Toggle, Slider, Progress Bar, Input
  Field, Dropdown, Scroll View, Horizontal Box, Vertical Box, Grid; put them under a Canvas.
- Scripts change text with `self:SetProperty("Text", "content", "Score: " .. score)`.
- A button that runs a script, and what `validate` says about UI: guide `menus-and-buttons`.
- Check sound and particles: `kb_cli run --frames 60 --report audio` (or
  `--report particles`).
- Microphone: `Microphone.Start()` (or `Microphone.Start("Device name")`; `Microphone.Devices()` and
  `Microphone.Device(i)` list the inputs), then `Microphone.Level()` and `Microphone.Peak()` each frame, and
  `Microphone.RecordToClip(2.0)` for a clip of the last two seconds, which `Audio.Play` takes. `Microphone.Stop()` ends it.
  A project that records turns `Microphone` on in Project Settings > Audio; an Android package then declares the
  permission and the first `Start` asks for it (it answers "being requested" until the user decides).
)md";

constexpr std::string_view kWaterAndLook = R"md(# Water, sky, fog and post-processing

```
# Water: a translucent material with the water shading model on a plane without a collider.
kb_cli material-new --name Water --set "Base Color=0.1,0.3,0.6,0.5" --set Roughness=0.05
kb_cli material-graph setting --material /Game/Materials/Water.kbmat --name "Blend Mode" --value translucent
kb_cli material-graph setting --material /Game/Materials/Water.kbmat --name "Shading Model" --value singleLayerWater
kb_cli entity add --create Plane --name Water --scale 3,1,3 --material /Game/Materials/Water.kbmat
kb_cli component remove --entity Water --type Collider

# Fog, bloom and vignette for the scene, and the sky colour.
kb_cli post-process-new --name Look --set fogEnabled=1 --set fogDensity=0.03 --set bloomEnabled=1 --set vignetteEnabled=1
kb_cli scene-post-process --profile /Game/PostProcess/Look.kbppfx
kb_cli component set --entity Environment --type WorldBackdrop --set skyTint=0.4,0.6,1
```

- Any translucent material: the alpha of Base Color plus `Blend Mode` translucent.
- `kb_cli help material-graph` and `kb_cli help post-process-new` list every setting.
)md";

constexpr std::string_view kRecipeMaterial = R"md(# Recipe: a colour or material on an object

Not sure of the object's name? `kb_cli scene-list` lists the objects.

```
[
["material-new","--name","Green","--set","Base Color=#2ECC40"],
["component","set","--entity","Crate","--type","MeshRenderer","--set","material=/Game/Materials/Green.kbmat"]
]
```

Done when the second command prints `updated MeshRenderer`.

- Colours: red #E03C31, green #2ECC40, blue #0074D9, yellow #FFDC00, orange #FF851B, white #FFFFFF, black #111111.
- Metal or glow: add `--set Metallic=1 --set Roughness=0.3` or `--set "Emissive Color=#FF851B"` to `material-new`.
- A new coloured object in one command: `kb_cli entity add --create Cube --name Crate --color "#2ECC40"`.
- A look in words (shiny gold, oak, frosted glass, neon, lava, toon, dissolve): `guide` with `search` "<the customer's words>" finds a ready material;
  `recipe-material-look` puts it on the object, previews it and writes a graph of your own.
)md";

constexpr std::string_view kRecipeComponent = R"md(# Recipe: physics, or another component, on an object

```
[["component","add","--entity","Crate","--type","Rigidbody","--set","bodyType=Dynamic"]]
```

Check, as a call of its own:

```
[["run","--frames","120"]]
```

Done when `run` ends with `0 diagnostics` and a `moved:` line for the object (`Crate 0,3,0 -> 0,0.5,0` means it fell and landed).

- Shapes already have a Collider; a floor or wall that never moves needs no Rigidbody.
- Any other component: `kb_cli schema --component <Name>` lists its properties, then
  `component add --entity Crate --type <Component> --set property=value`. An object has one of
  each component: when `add` says it is there, change it with `component set`.
- Heavier: `component set --entity Crate --type Rigidbody --set mass=50`. Bouncy:
  `component set --entity Crate --type Collider --set restitution=0.6`.
- Hidden when the game starts: `component add --entity Crate --type Visibility` (a script shows it again; `component remove` makes it visible).
- A joint holds on to another object: `component set --entity Door --type Joint --set connectedEntity=Wall`.
- Several at once: `entity add --create Cube --name Crate --count 5 --offset 0,1,0 --body Dynamic` makes Crate1..Crate5.
)md";

constexpr std::string_view kRecipePrefab = R"md(# Recipe: a prefab, with a component

Add the component first: a prefab saves the object as it is.

```
[
["component","add","--entity","Crate","--type","Rigidbody","--set","bodyType=Dynamic"],
["prefab","new","--from-entity","Crate","--name","CratePrefab"]
]
```

Done when `prefab new` prints `saved 'Crate' as prefab /Game/Prefabs/CratePrefab.kbprefab`.

- Another copy in the scene: `kb_cli prefab place --prefab /Game/Prefabs/CratePrefab.kbprefab --name Crate2 --position 3,0,0`.
- A script spawns copies with `World.InstantiatePrefab("/Game/Prefabs/CratePrefab.kbprefab", nil, nil, x, y, z)`.
- `kb_cli schema --component <Name>` lists the properties of any component.
)md";

constexpr std::string_view kRecipeScript = R"md(# Recipe: a Lua script on an object

```
[
["script-new","--name","Spin","--language","lua","--code","function Tick(self, dt)\n    Transform.Rotate(self.entity, 0, 20 * dt, 0)\nend"],
["scene-attach","--node","Crate","--script","/Game/Scripts/Spin.lua"]
]
```

Check, as a call of its own:

```
[["validate","Assets/Scripts/Spin.lua"],["run","--frames","120"]]
```

`--code` is the whole script, so there is nothing to edit afterwards. Done when `validate` says `OK`
and `run` ends with `0 diagnostics`. The object's name comes from `kb_cli scene-list`.

- `Tick(self, dt)` runs every frame (`dt` in seconds), `Ready(self, dt)` once at the start; `self.entity` is the object.
- `Transform.Rotate(self.entity, x, y, z)` turns it by degrees: `0, 20 * dt, 0` is a slow turn around the upright axis.
- Another engine function: `api_reference` with `search` (a word) or `topic` (`Transform`, `Physics`, `Input`).
- Changing a script that exists: edit its `.lua` file directly, then `validate` again.
- A second script on the same object: `scene-attach` again with the other script (up to 8). Each script has its own `self`, and an event for the object reaches all of them. `scene-detach --node Crate --script <path>` takes one off.
- Keys: `kb_cli input-action new --name Fire --key Space`, read with `Input.Pressed("Fire")`; more in guide `scripts`.
- React to touching something: `function OnTriggerEnter(self, event)`, guide `recipe-pickup`; every hook: topic `EventHooks`.
)md";

constexpr std::string_view kRecipePickup = R"md(# Recipe: coins the player collects by touching them, with a counter on the screen

Look at the objects first, as a call of its own: it names the object the person plays.

```
[["scene-list"]]
```

Put that name in `--player` (the scene has none: add `--with-player yes` to `pickup new`, which adds a walking `Player` first):

```
[["pickup","new","--name","Coin","--count","3","--position","0,1,3","--player","Player"]]
```

Check, as a call of its own:

```
[["run","--frames","150","--press","W@5+90"]]
```

`pickup new` makes the coins (Coin, Coin2, Coin3 in a row along Z, each
a trigger Collider), the counter text on the screen, the scripts and their attachment, and checks the scripts: all of it or
nothing. Done when `run` prints `[log] Coin count: 1` and `moved: Coin was destroyed`, then `0 diagnostics`: `--press W@5+90`
holds W, so the player walks forward (+Z) into the coins. That is the whole job: no prefab, picture or second check is needed.

- One coin: leave out `--count`. Another object or word: `--name Gem --label Gems`.
- Why it works: a Collider with `trigger=true` blocks nothing. When something that moves (the player) overlaps it, the engine
  calls `OnTriggerEnter(self, event)` in the coin's script; `event.args.other` is the other object. `OnCollisionEnter` is
  the same for solid contact. Every hook with its arguments: `api_reference` topic `EventHooks`.
- Changing what a script does: edit `Assets/Scripts/Coin.lua` or `CoinCounter.lua`, then `validate` and `run` again.
- Keep the coins on the floor: a player that walks off the 10x10 `Plane` falls.
)md";

constexpr std::string_view kRecipeLight = R"md(# Recipe: light, sun, sky and time of day

A scene from a template has a `Sun` (a directional light) and an `Environment` (the sky); `kb_cli scene-list`
shows the names if they differ. The sun's `rotation` x is its height: 90 straight above, 10 near the horizon.

Evening, warm and orange:

```
[
["component","set","--entity","Sun","--type","Light","--set","color=1,0.62,0.34","--set","intensity=2.2"],
["entity","set","--name","Sun","--rotation","18,-35,0"],
["component","set","--entity","Environment","--type","WorldBackdrop","--set","horizonColor=0.72,0.34,0.19","--set","zenithColor=0.23,0.18,0.28","--set","skyTint=1,0.64,0.38","--set","groundColor=0.2,0.12,0.08"],
["component","set","--entity","Environment","--type","Ambient Radiance","--set","horizonColor=0.72,0.34,0.19","--set","zenithColor=0.23,0.18,0.28"]
]
```

Done when every command prints `updated ...` or `set 'Sun'`.

- Midday: Light `color=1,0.96,0.9` `intensity=3`, Sun rotation `60,-30,0`; sky `horizonColor=0.6,0.75,0.95` `zenithColor=0.2,0.4,0.85`.
- Night: Light `color=0.4,0.5,0.9` `intensity=0.3`, Sun rotation `35,-30,0`; sky `horizonColor=0.03,0.04,0.1` `zenithColor=0.01,0.01,0.05`, the same two colours on `Ambient Radiance`.
- Brighter or dimmer: Light `intensity`. A lamp: `kb_cli entity add --create "Point Light" --name Lamp --position 0,3,0`, then `component set --entity Lamp --type Light --set intensity=8 --set color=1,0.8,0.5`.
- Fog, bloom, water: guide `water-and-look`. To show the person: `kb_cli capture`.
)md";

constexpr std::string_view kRecipeSettings = R"md(# Recipe: project settings, plugins, scenes in the build, tags and builds

```
[["project-settings","--set","Identity.GameName=Dragon Hills","--set","Identity.Name=Dragon Hills"]]
```

Done when it prints the settings it changed. Without `--set` it lists all of them (about 70 lines): do that only when the key is unknown.

A second scene (a level, a menu) the game can open: make it and put it in the build, in one call:

```
[["scene-new","--name","Level2"],["project-settings","scenes","--add","Level2"]]
```

Done when it lists `Level2` under `scenes in the build`. Only the scenes in that list are in the built game, also one that a button or a script opens with
`Scene.Load("/Game/Scenes/Level2.21kbscene")`; `build` warns about one that is left out. `["project-settings","scenes"]` lists them.

- Which scene the game starts in: `["project-settings","scenes","--startup","Level2"]`. Order and the rest: `--order Main,Level2`, `--remove Level2`.
- Tags for objects (a kind of enemy, a pickup): `["project-settings","tags","--add","Boss"]`; `--remove Boss`; without options it lists them.
- Names: `Identity.GameName` (the game's name) and `Identity.Name` (the project's); set both for "name my game".
- Window: `Display.WindowWidth`, `Display.WindowHeight`, `Display.WindowMode` (`Windowed` or `BorderlessFullscreen`), `Display.VSync` (1 or 0).
- Gravity: `Physics.GravityY=-9.81`. Volume: `Audio.MasterVolume`.
- Android: `Android.ApplicationId`, `Android.Label`, `Android.VersionCode`.
- Plugins: `kb_cli plugins` lists them; `--enable <id>` or `--disable <id>` switches one.
- A game for a platform: `kb_cli editor build --target windows|android|web|webgpu|linux` (`--profile release` for a store build).
  `Saved/Agent/BuildResult.txt` says `Running`, then `Succeeded <folder>` or `Failed <reason>`.
)md";

constexpr std::string_view kRecipeUiButton = R"md(# Recipe: a button on the screen

```
[
["entity","add","--create","Button","--name","PlayButton"],
["component","set","--entity","PlayButton","--type","Text","--set","content=Play"]
]
```

The button goes under a Canvas, made when the scene has none. Done when both commands print `added` or `updated`.
It shows, but a click does nothing yet: say so, unless the person told you what it should do. To make it do something:

```
[
["component","set","--entity","PlayButton","--type","Selectable","--set","eventName=PlayClicked"],
["script-new","--name","MenuController","--language","lua","--code","function PlayClicked(self, event)\n    Log('Play pressed')\nend"],
["scene-attach","--node","PlayButton","--script","/Game/Scripts/MenuController.lua"]
]
```

Check, as a call of its own:

```
[["validate","Assets/Scenes/Main.21kbscene","Assets/Scripts/MenuController.lua"]]
```

Done when `validate` lists no warnings. Put the real action in the function (a scene change, a score, a sound); guide `menus-and-buttons` has every callback.
A scene it opens must be in the build: guide `recipe-settings`.

- A whole screen at once: `kb_cli entity add --create "Main Menu"` (also `Pause`, `HUD`, `Settings`).
- Text on screen: `entity add --create Text --name Score`, then `component set --entity Score --type Text --set "content=Score: 0"`.
- Try a click without a window: `kb_cli run --frames 12 --click Canvas/PlayButton@5`.
)md";

constexpr std::string_view kRecipePlayer = R"md(# Recipe: a character the person plays

```
[["entity","add","--create","Third Person Player"]]
```

Check, as a call of its own:

```
[["run","--frames","60"]]
```

One command adds the character with its controller script, input and the camera. Done when it prints
`added a walking character 'Player' ...` and `run` ends with `0 diagnostics`. Tell the person: WASD or the arrows
walk, Space jumps, the camera follows from behind.

- From its eyes with mouse look: `"First Person Player"`. In 2D: `"Platformer Player"` or `"Top Down Player"`.
- Where it starts: add `--position 0,1,0`. A model, a colour or animations: guide `characters-and-animation`.
- Do not add a second camera or controller by hand: the preset has them.
)md";

constexpr std::string_view kRecipeImage = R"md(# Recipe: an icon, a picture, a texture or a sprite sheet

The engine draws it: no outside tool, no file to download.

```
[
["image","new","--name","Heart","--preset","heart","--style","neon","--color","#FF2D6F"],
["entity","add","--create","Image","--name","HeartIcon"],
["component","set","--entity","HeartIcon","--type","Image","--set","imageAssetId=/Game/Images/Heart.png"]
]
```

Done when `image new` prints `imported as` and `component set` prints `updated Image`. The Image goes under a Canvas
(made when the scene has none). A name that exists is kept: pick a new `--name`, or add `--replace` to draw it again.

- Style words: neon, glowing, cyberpunk, synthwave -> `--style neon` with a bright `--color` (pink #FF2D6F, cyan #00E5FF,
  green #39FF14, yellow #FFE600, purple #B026FF); flat, cartoon, clean, minimal -> `flat`; soft, glossy, shiny, rounded 3D
  button -> `soft`; pixel, retro, 8-bit -> `pixel`. "Dark" is a dark `--color` (a panel `#1E2233`, low-poly `#2A2F3A`).
  `--color2` is a gradient end, the glow of a neon or the core of a flame.
- `--preset`: heart, star, coin, circle, ring, square, arrow, gem (icons); button, panel (backgrounds, wide); glow (a soft
  dot); noise, ground, lowpoly (textures that tile; "dark low-poly" is `lowpoly --color "#2A2F3A"`); sky (a gradient); flame.
- `--size 128` makes a smaller file (16 to 1024, default 256). The file is Assets/Images/<Name>.png.
- A texture on a 3D object: `kb_cli material-new --name Ground --set "Base Color Texture=/Game/Images/Ground.png"`, then
  `component set --entity Floor --type MeshRenderer --set material=/Game/Materials/Ground.kbmat`.
- Fire in 2D: `image new --name Fire --preset flame` (8 frames in a row), `entity add --create "Sprite 2D" --name Fire`, then
  `component set --entity Fire --type Sprite2D --set imageAssetId=/Game/Images/Fire.png --set cellWidth=256 --set cellHeight=256
  --set frameCount=8 --set framesPerSecond=12` (the cell is the `--size`).
- Show the person: `kb_cli capture`.
)md";

constexpr std::string_view kRecipeSound = R"md(# Recipe: a sound effect or a music loop

The engine makes it with its own synthesiser: no outside tool, no file to download.

```
[["sound","new","--name","Jump","--preset","jump"]]
```

Done when it prints `imported as`. The file is Assets/Audio/<Name>.wav, used as `/Game/Audio/Jump.wav`. A name that
exists is kept: pick a new `--name`, or add `--replace`.

- Presets: jump, coin (also pickup), hit, explosion, click, laser, powerup, footstep; wind (ambient) and music loop.
- `--pitch 5` moves it up five semitones (negative is lower; 12 is an octave); `--length 0.5` sets the seconds.
- Play it from a script: `Audio.Play("/Game/Audio/Jump.wav")`, for example in `function Tick(self, dt)` when
  `Input.Pressed("Jump")`; guide `scripts` has the keys.
- Play it in the scene, on start or as a loop, in one call (the sound, the object that plays it, and the link):

```
[
["sound","new","--name","Wind","--preset","wind"],
["entity","add","--create","Audio Source","--name","Ambience"],
["component","set","--entity","Ambience","--type","AudioSource","--set","clipAssetId=/Game/Audio/Wind.wav","--set","autoplay=true","--set","loop=true"]
]
```

- Check it: `kb_cli run --frames 60 --report audio` ends with `"succeeded":true`.
)md";

constexpr std::string_view kRecipeTimeline = R"md(# Recipe: an intro, a cutscene, a camera flight

```
[["timeline","fly","--name","Intro","--to","Player","--seconds","3"]]
```

Check, as a call of its own:

```
[["run","--frames","240"]]
```

`fly` makes the timeline, a camera flight from where `Main Camera` is to a place behind and above the object
`--to`, looking at it, and a script that plays it when the scene starts. Done when it prints `plays when the scene
starts` and `run` ends with `0 diagnostics`. The object's name comes from `kb_cli scene-list`.

- Another camera or object: `--entity "Camera"`. Closer or higher: `--distance 3 --height 1`. Longer: `--seconds 6`.
- Your own path: `timeline new --name Tour --duration 6`, then one `timeline key` per point
  (`--timeline /Game/Cinematics/Tour.kbtimeline --entity "Main Camera" --time 0 --position 0,3,-10`,
  `--time 3 --position 8,3,0 --look-at Player`), then `timeline play --timeline /Game/Cinematics/Tour.kbtimeline`;
  all of them as the commands of one call.
  A key leaves what it does not give as the object has it now; the object moves smoothly between keys.
- A moment in it that a script hears: `timeline event --timeline <path> --time 3 --event IntroDone`, and in the script of
  that object `function Created(self) Events.Subscribe("OnTimelineEvent", function(event) Log(event.args.name) end) end`.
- `timeline show --timeline <path>` lists the keys. Play it again with `--loop yes` on `timeline play`.
)md";

constexpr std::string_view kRecipeUiTheme = R"md(# Recipe: the look of the interface

```
[
["ui-theme","new","--name","Dark","--preset","dark","--button","#FF8C1A"],
["ui-theme","apply","--theme","/Game/UI/Dark.kbtheme"]
]
```

A theme is one shared style: panels and texts take the base colours, buttons take their own. Done when `apply` prints
`applied ... to N controls`. Controls added later: run `apply` again. To see it: `kb_cli capture`.

- `--preset dark` (dark panels, light text) or `light`. Colours as `#RRGGBB`: orange #FF8C1A, red #E03C31, green #2ECC40,
  blue #0074D9, purple #B026FF, white #F0F0F0, black #111111. The text on a button is chosen to be readable; `--button-text` sets it.
- Change a theme later: `kb_cli ui-theme set --theme /Game/UI/Dark.kbtheme --background "#101018" --font-size 20 --corner-radius 12`.
  Tokens: `--background`, `--text`, `--border`, `--border-width`, `--corner-radius`, `--font-size`, `--button`, `--button-text`.
- Only one menu: `apply ... --entity Menu` (that object and what is below it). `ui-theme show --theme <path>` lists the style.
- Make the controls first (guide `recipe-ui-button`), then the theme.
)md";

constexpr std::string_view kRecipeTranslate = R"md(# Recipe: the texts of the game in other languages

```
[
["localization","collect","--language","pl"],
["localization","set","--language","en","--set","ui.title=Dragon Hills","--set","ui.playbutton=Play"],
["localization","set","--language","de","--set","ui.title=Drachenhuegel","--set","ui.playbutton=Spielen"],
["localization","missing"]
]
```

`collect` turns the texts written on the scene's controls into keys of one catalog (the language they are written in: `--language`
is its code, such as pl, en, de) and prints each `key = "text"`; give every key a text in each other language with `set`
(one `--set key=text` per key). Done when `missing` says `every text is translated`.

- Changing the default language, the one the game starts in: `kb_cli localization default --language en`.
- `localization show` lists every key with its texts. `add-language --language fr` starts a language with the default texts.
- A text from a script: `Localization.Translate("ui.title")`. Switch while playing: `Localization.SetLanguage("de")`, for
  example from a button script (guide `recipe-ui-button`); a Text with a `localizationKey` changes at once.
- A new text of your own: `localization set --language en --set hud.score="Score"`, then on the control
  `component set --entity Score --type Text --set localizationKey=hud.score`.
)md";

constexpr std::string_view kRecipeAnimation = R"md(# Recipe: an object that moves by itself (bob, spin, a door, a pulse)

Look at the objects first, as a call of its own: it prints their names.

```
[["scene-list"]]
```

Then the clip, with the name of the object that moves in place of `OBJECT_NAME` (copy it exactly, as the scene spells it):

```
[["animation-clip","new","--name","Float","--preset","bob,spin","--entity","OBJECT_NAME"]]
```

Check, as a call of its own:

```
[["run","--frames","240"]]
```

`new` with a preset makes the animation clip, a controller for it
and an Animator on the object: it moves from the start of the scene, again and again. Done when it prints `plays on '<name>'` and
`run` ends with `0 diagnostics`.

- Presets: `bob` (up and down), `spin` (a full turn around the upright axis), `open-close` (a door swinging open and shut around its
  own centre), `pulse` (grows and shrinks). Several at once, joined by a comma: `bob,spin`.
- `--seconds 4` is the length of one cycle; `--amount` the size of ONE preset (bob: units, open-close and spin: degrees, pulse: 0.2 is a
  fifth bigger); `--loop no` plays it once. A name that exists is kept: pick a new `--name`, or add `--replace`.
- The keys are relative to where the object is when `new` runs: place the object first, then make the clip.
- An object has one Animator, so a second clip on it replaces the first: put the motions in one clip (`bob,spin`).
- Your own motion: `animation-clip new --name Tour --entity Crate` (starts where it is), then a key per moment
  (`animation-clip key --clip Tour --time 1 --position 0,3,0`, `--time 2 --rotation 0,90,0`; what a key does not give stays as before),
  then `animation-clip play --clip Tour --entity Crate`; all of them as the commands of one call. `animation-clip show --clip Tour` lists the keys.
- To see it: `kb_cli capture --frames 30` and again with `--frames 90`.
)md";

constexpr std::string_view kRecipeMaterialLook = R"md(# Recipe: a ready-made material look (gold, wood, glass, neon, lava, toon, dissolve ...)

1. Find it from the customer's own words: `guide` with `search` "shiny gold armour". It returns one to three presets, each with its command. `guide` topic `material-presets` lists them all by family.
2. Make it and put it on the object in one command, with the object's name in place of `OBJECT_NAME` (`scene-list` lists them):

```
[["material-new","--preset","gold","--name","ArmorGold","--entity","OBJECT_NAME"]]
```

   A prefab asset: `"--prefab","/Game/Prefabs/Knight.kbprefab"` (`"--entity","Helmet"` keeps it to that part). Another colour or value:
   `"--set","Color=#C41E3A"`, with the parameter names the search printed (`Color`, `Roughness`, `Speed`, `Intensity`, `Tiling`).
3. Look at it, as a call of its own (it needs the player beside kb_cli): `kb_cli` with `["material-preview","--material","/Game/Materials/ArmorGold.kbmatinst"]`
   returns a picture. Done when it prints its size and `blank: no`. Several materials in one call are drawn side by side.
4. Phones: the command prints `Warning: too heavy for a phone` when the material is; tell the person and pick a lighter preset.

No preset fits: write the whole graph in one call, `material-new --name Mine --graph "<graph text>"` (or `material-graph text --material <path> --text ...`
for an existing one). `material-graph show --material <path>` prints a graph in the same form. One node per line, `Name = Kind value(Pin=source, ...)`,
links into the result as `Output.Pin = source`; a number, `#RRGGBB` or `"x y z"` may stand for a source (this text as the `--graph` value, with a line break
between the lines, makes a scratched gold):

    Domain = surface
    Gold = ParameterColor #C9A227
    Scratches = Noise "value false 1 1 2 2 0 1"(value=Multiply(UV, 30))
    Output.BaseColor = Lerp(Gold, #3A3A3A, Step(0.8, Scratches))
    Output.Metallic = 1
    Output.Roughness = Lerp(0.25, 0.7, Step(0.8, Scratches))

An error names the line, node and pin, with the pins and types that would fit; fix that line and send the text again (nothing was written).
`material-graph nodes` lists every node kind by group, `material-graph node --kind Lerp` shows one with an example, `guide` topic `materials`
tells the domains, blend modes and phone limits. A parameter (`ParameterColor`, `ParameterScalar`) can be changed later without a new graph:
`material-instance-new --parent <material> --set Gold=#FFD700`.
)md";

constexpr std::string_view kRecipeOutline = R"md(# Recipe: an outline round a character, or a glow where it stands behind a wall

A thick, even line round an object (a cartoon look, a selected unit, a loot item) and, if wanted, the part of it hidden behind walls
lit in a colour. The engine draws the object a second time as a hull pushed out along its normals (the same number of pixels at any
distance, on a computer and on a phone; skinned characters too), so any graph material that has these parameters gets it:

- `OutlineWidth` - pixels of the view; 0 is no outline.
- `OutlineColor` - the colour of the line.
- `HiddenStrength` and `HiddenColor` - 0 is off; above 0 the parts of the object behind other surfaces glow in `HiddenColor` (strongest at their edges).

The presets already have them: `outline-black`, `outline-white`, `outline-gold`, `outline-enemy` (a line), `xray-ally`, `xray-enemy`, `xray-item` (a line and the glow
through walls), and every `toon-*` look. `guide` with `search` "outline" or "obrys" finds them. One command puts one on an object (`OBJECT_NAME`; `scene-list` lists them):

```
[["material-new","--preset","outline-black","--name","HeroLine","--entity","OBJECT_NAME","--set","Color=#C8A27A"]]
```

`Color` is the object's colour; to keep its colour picture instead, `--set Albedo=/Game/Images/Hero.png` (the picture of the character, tinted by `Color`). Another line: `--set OutlineColor=#FF2A2A --set OutlineWidth=4`;
a glow through walls on the same material: `--set HiddenStrength=0.9 --set HiddenColor=#3D8BFF`.

Check it as a call of its own (it needs the player beside kb_cli): `kb_cli` with `["capture","--frames","30"]`; done when the line is a solid band round the object.
- The hull grows along the vertex normals: a mesh with hard edges (a cube) shows gaps at its corners; smooth the normals or use a rounder mesh.
- The glow through walls shows the hidden part as a lit edge with a faint fill; the parts of the object that its own body hides glow faintly too.
- Only opaque and masked materials get a line; a translucent one does not.
)md";

constexpr std::string_view kRecipeWater = R"md(# Recipe: a lake, a sea, a river, a pool - realistic water

`guide` with `search` "lake", "water", "woda" or "jezioro" finds the presets: `water-ocean`, `water-lake`, `water-pool`, `water-river`, `water-swamp`, `water-toxic`,
`water-stylized`. One material, two looks, chosen by the class of the device:

- On a computer it is full: the colour of the ground fading with the thickness of the water (Clarity), refraction of the scene behind, waves that
  move the reflection, sun and sky reflection (fresnel), and foam where the water meets the shore or objects.
- On a phone or tablet the same material draws as lit translucent water with a simpler wave: no copy of the screen, no foam. Nothing to set.

A flat plane is the water; the preset goes on it in the same call:

```
[["entity","add","--create","Plane","--name","Lake","--scale","40,1,40","--position","0,2,0"],["material-new","--preset","water-lake","--name","LakeWater","--entity","Lake"]]
```

Parameters (`--set Name=value`): `Color` (shallow water), `DeepColor`, `Clarity` (metres of water before the ground is lost: 1 murky, 12 a clear pool), `WaveScale`,
`WaveStrength`, `Speed`, `FoamColor`, `FoamDistance` (the width of the foam edge), `Opacity` and `Roughness` (the phone look). Keep the water above a ground, so a
shore exists and the depth reads.

Look at both, each as a call of its own: `kb_cli` with `["material-preview","--material","/Game/Materials/LakeWater.kbmatinst"]` and the same with `"--device","phone"`.
)md";

constexpr std::string_view kRecipeTerrainLayers = R"md(# Recipe: a mountain, a hill, a landscape with 8 to 10 textured layers

`guide` with `search` "mountain", "terrain", "gora" or "teren" finds the preset `terrain-layers` (and `terrain-layers-10`). The ground picks its layers from where it is:
by height (layer 0 the lowest, each next one higher, blended where two meet, the harder one showing through the softer), and the last layer on steep slopes (cliffs).
Only the two neighbouring layers and the cliff are read per pixel, so ten layers cost what three do, which fits a phone; on a computer a second look at each
layer, turned and scaled, breaks up the repeating of the pattern.

1. The layers as one array texture, in order from the lowest ground to the cliff (here 8: sand, mud, grass, forest floor, dirt, rock, scree, then the cliff). `ground`
   draws a tiling texture from two colours (`image new` lists the other presets):

```
[["image","array","--name","TerrainLayers","--size","256","--layers","ground:#D9C28F:#B89B64,ground:#7A5C3A:#5A4128,ground:#4E8A2E:#2F5A1C,ground:#3C6B26:#27471A,ground:#6B5A3A:#4D4028,ground:#7A7873:#55534F,ground:#B8B4AE:#8E8A84,ground:#5C5A56:#3A3835"]]
```

2. The ground (here a generated mountain; an existing terrain or mesh works too) and the material on it. `material-new` with `--entity` reads the lowest and the highest
   height of the ground itself (`HeightMin`, `HeightMax`: the layers spread over the real range of the ground, so the top one shows on the peaks):

```
[["terrain","new","--name","Land","--terrain","/Game/Terrains/Land.kbterrain","--preset","mountains","--size","128"],["material-new","--preset","terrain-layers","--name","Mountain","--entity","Land","--set","Layers=/Game/Images/TerrainLayers.ktx","--set","LayerCount=8"]]
```

   Ten layers: draw 10 and use `terrain-layers-10` (`LayerCount=10`). Other parameters: `Tiling` (how often a texture repeats; smaller is bigger rock), `SlopeStart` (0.2
   cliffs early, 0.6 only the steepest), `BlendDepth` and `HeightInfluence` (how sharply the harder layer shows through the softer one).
3. The scene's camera stands at the middle of the land, under its surface: lift it over the land first (`terrain info` gives the top; for a land of 128 units): `["entity","set","--name","Main Camera","--position","0,<top + 25>,-110","--look-at","0,<top / 2>,0"]`.
4. Look at it as a call of its own: `kb_cli` with `["capture","--frames","30"]`; done when the lowest ground, the middle and the top differ and the cliffs are one rock.
)md";

constexpr std::string_view kRecipeMaterialFunction = R"md(# Recipe: a reusable material effect (a material function)

Look at the objects first, as a call of its own:

```
[["scene-list"]]
```

Then, with the names of the objects whose material gets the effect in place of `OBJECT_NAME` and `OTHER_OBJECT_NAME`:

```
[
["material-function","new","--name","GlowEdge","--preset","fresnel-glow"],
["material-function","use","--entity","OBJECT_NAME","--function","GlowEdge","--set","Color=#FF8C1A","--set","Power=3"],
["material-function","use","--entity","OTHER_OBJECT_NAME","--function","GlowEdge","--set","Color=#00E5FF"]
]
```

One function, written once, called from any number of materials (or name a material: `--material /Game/Materials/<Name>.kbmat`). Done when `use` prints `calls ... Glow -> emissive`. The material keeps
its colour. An object without a material of its own: `kb_cli entity add --create Sphere --name Ball --color "#2E86DE"` makes one.

- Presets: `fresnel-glow` (a glow on the edge: inputs Color, Power (higher is a thinner edge), Intensity; output Glow goes to Emissive),
  `panner` (a texture coordinate that scrolls: Speed), `tiling` (a texture coordinate repeated: Tiling); the last two give UV, wired to
  Customized UV. `kb_cli material-function show --function GlowEdge` lists the inputs with their defaults.
- A name that exists is kept: pick a new `--name`, or add `--replace`. Check a material: `kb_cli material-graph show --material <path>` ends
  with `Compiles: yes`. Colours as `#RRGGBB`, numbers as 3.
- To see it: `kb_cli capture --frames 40`.
)md";

constexpr std::string_view kRecipeSave = R"md(# Recipe: save the game, keep a value for the next run

`Save.Read(slot)` loads a slot into the open save document, `Save.SetInt(key, value)` changes it, `Save.Write(slot)` keeps it. `Save.GetInt(key)` returns a table: `.value` is the number (0 when the key is new). This script counts how often the scene ran, on an object that exists (`scene-list` names them):

```
[
["script-new","--name","RunCounter","--language","lua","--code","function Created(self)\n    Save.Read(\"game\")\n    local runs = Save.GetInt(\"runs\").value + 1\n    Save.SetInt(\"runs\", runs)\n    Save.Write(\"game\")\n    Log(\"runs \" .. runs)\nend"],
["scene-attach","--node","Plane","--script","/Game/Scripts/RunCounter.lua"]
]
```

Check, as two calls of their own, one after the other:

```
[["run","--frames","5"]]
```

Done when the first `run` logs `runs 1` and the second `runs 2`: the slot kept the value between runs. `run` keeps its slots in `Saved/Saves` of the project; `run --fresh-saves` starts with none.

- Other types: `Save.SetBool`, `SetFloat`, `SetString` and their `Get...` (each returns `found` and `value`); `Save.Has(key)`, `Save.Remove(key)`, `Save.Delete(slot)`. A slot name is letters, digits, `_` or `-`.
- A high score: `if score > Save.GetInt("best").value then Save.SetInt("best", score) end`, then `Save.Write("game")`.
- The built game keeps its slots in the player's own folder, not in the project.
)md";

constexpr std::string_view kRecipeNavmesh = R"md(# Recipe: a character that walks to a place around obstacles (navigation mesh)

Obstacles and floor need a Collider. Bake the walkable area after they exist, then give the walker a Character Controller and a Nav Agent (the controller sits on an empty parent; the shape is a child without a Collider):

```
[
["entity","add","--create","Cube","--name","Wall","--position","0,1,2","--scale","6,2,1"],
["navmesh","bake"],
["entity","add","--name","Guard","--position","-4,1,0"],
["entity","add","--name","Body","--parent","Guard","--create","Capsule"],
["component","remove","--entity","Guard/Body","--type","Collider"],
["component","add","--entity","Guard","--type","CharacterController"],
["component","add","--entity","Guard","--type","NavAgent","--set","destination.x=3","--set","destination.y=0","--set","destination.z=4","--set","enabled=true"]
]
```

Check, as a call of its own:

```
[["run","--frames","700"]]
```

Done when the `moved:` line of `Guard` ends near the destination (`-4.00,1.00,0.00 -> 2.98,1.01,3.90`: within a metre of 3,4) with `0 diagnostics`; walking round the Wall takes 600 frames or so.

- `destination` must lie on the floor (the new scene's floor is 10x10 around 0,0); a point off the mesh stops at its edge.
- `maxSpeed` (3.5 m/s) and `stoppingDistance` are `NavAgent` properties. Bake again after the obstacles change: `navmesh bake`.
- Click-to-walk and the camera: scene template `TopDown`. A script moves the agent with `self:SetProperty("NavAgent", "destination.x", x)` and `Navigation.NearestPoint(x, y, z)`.
- Fighting AI (chase, flee): guide `combat-and-ai`.
)md";

constexpr std::string_view kRecipeCollision = R"md(# Recipe: what collides with what (physics layers)

Name layers, say which pairs ignore each other, and put each Collider on a layer. A Collider's `layer` is one bit: layer 1 is 2, layer 2 is 4, layer 3 is 8 (a new Collider is on all layers). Here a ghost falls through a shelf and lands on the floor:

```
[
["physics-layers","--name","1=Ghost","--name","2=Shelf","--ignore","Ghost,Shelf"],
["entity","add","--create","Cube","--name","Shelf","--position","0,2,0","--scale","4,0.3,4"],
["component","set","--entity","Shelf","--type","Collider","--set","layer=4"],
["entity","add","--create","Sphere","--name","Ghost","--position","0,5,0","--body","Dynamic"],
["component","set","--entity","Ghost","--type","Collider","--set","layer=2"]
]
```

Check, as a call of its own:

```
[["run","--frames","180"]]
```

Done when `moved: Ghost 0.00,5.00,0.00 -> -0.00,0.51,-0.00` (it passed the shelf). To make them touch again: `physics-layers --collide Ghost,Shelf`, and the ghost stops on the shelf (`y` about 2.65).

- `physics-layers` alone lists the layers; `--ignore A,B` and `--collide A,B` edit the matrix, `--name 3=Enemy` names a layer (0-30).
- Touching is an event: `function OnCollisionEnter(self, event)` for a solid contact, `function OnTriggerEnter(self, event)` for a Collider with `trigger=true` (the other object is `event.args.other`). Guide `physics`, `recipe-pickup`.
- Rays only hit some layers: `api_reference` topic `Physics`, the `layerMask` argument.
)md";

constexpr std::string_view kRecipeJoint = R"md(# Recipe: a hinge, a pendulum, a spring (joints)

A Joint on a body holds it to another object (`connectedEntity`, a static one such as a hook) or to the world (leave it out). `anchor` is the point on this body, in its own space, where it is held. A ball on a hinge swings from a hook:

```
[
["entity","add","--create","Cube","--name","Hook","--position","0,6,0","--scale","0.3,0.3,0.3","--body","Static"],
["entity","add","--create","Sphere","--name","Bob","--position","3,6,0","--body","Dynamic"],
["component","add","--entity","Bob","--type","Joint","--set","type=Hinge","--set","connectedEntity=Hook","--set","axis.x=0","--set","axis.y=0","--set","axis.z=1","--set","anchor.x=-3"]
]
```

Check, as a call of its own:

```
[["run","--frames","120"]]
```

Done when `moved: Bob 3.00,6.00,0.00 -> -2.99,5.79,0.00`: it swung to the other side of the hook.

- `type`: Fixed (welded), Hinge (turns about `axis`), Distance (a rope), Point (a ball joint), Slider (a door or lift along `axis`).
- Limits: `enableLimit=true`, `minLimit`, `maxLimit` (degrees for a hinge, metres for a slider). Motor: `motor.targetVelocity`, `motor.force`. Soft: `spring.spring`, `spring.damper`. `breakForce` snaps it.
- `kb_cli schema --component Joint` lists every property. Change a joint that exists with `component set`.
)md";

constexpr std::string_view kAudioMixer = R"md(# Audio mixer: buses, volumes and effects

Sounds play on buses; a mixer asset names the buses and their volumes, and a scene uses one mixer.

```
[
["sound","new","--name","Theme","--preset","music"],
["audio-mixer","new","--name","Main","--bus","Music","--bus","Effects","--set","Music.volume=0.6"],
["audio-mixer","assign","--mixer","/Game/Audio/Main.kbmixer"],
["entity","add","--name","Speaker","--position","0,1,0"],
["component","add","--entity","Speaker","--type","AudioSource","--set","clipAssetId=/Game/Audio/Theme.wav","--set","loop=true","--set","autoplay=true","--set","outputBus=Music"]
]
```

Check, as a call of its own: `[["run","--frames","30","--report","audio"]]`. Done when the report has `"succeeded":true`, both buses `"initialized":true` and a `peak` above 0 (the loop is heard on Music).

- Change a bus: `audio-mixer set --mixer /Game/Audio/Main.kbmixer --set Music.volume=0.3 --set Effects.mute=true`; a bus under another: `--bus Voices --set Voices.parent=Effects`; `audio-mixer show --mixer <path>` lists them.
- Effects: `--effect-bus Music --add-effect Echo=delay` (lowpass, highpass, equalizer, delay, reverb, compressor, limiter), `--set-effect Echo.<parameter>=value`. Snapshots (a quiet menu mix): `--add-snapshot Menu --snapshot Menu --set-snapshot Music=0.2`; `assign --snapshot Menu`.
- In a script: `Audio.SetBusVolume("Music", 0.3)`, `Audio.ClearBusVolume("Music")`; one sound on a bus: `Audio.Play(clip, volume, pitch, mute, loop, spatial, ..., outputBus)`. `kb_cli help audio-mixer` shows every option.
)md";

constexpr std::string_view kGameplayGraphs = R"md(# Gameplay graphs (.kbgraph): logic as nodes

A graph is a behaviour asset made of nodes and typed connections. A new one starts with a Tick event (node 1); each `graph add` makes the next node (2, 3, ...). This graph sets the object's height to 3 every frame:

```
[
["graph","new","--graph","/Game/Float.kbgraph"],
["graph","add","--graph","/Game/Float.kbgraph","--entry","NativeBinding:SetProperty:Self.SetProperty.Float"],
["graph","add","--graph","/Game/Float.kbgraph","--entry","Literal:String","--value","Transform"],
["graph","add","--graph","/Game/Float.kbgraph","--entry","Literal:String","--value","localPosition.y"],
["graph","add","--graph","/Game/Float.kbgraph","--entry","Literal:Float","--value","3"],
["graph","connect","--graph","/Game/Float.kbgraph","--from","1","--out","then","--to","2","--in","exec"],
["graph","connect","--graph","/Game/Float.kbgraph","--from","3","--out","value","--to","2","--in","component"],
["graph","connect","--graph","/Game/Float.kbgraph","--from","4","--out","value","--to","2","--in","property"],
["graph","connect","--graph","/Game/Float.kbgraph","--from","5","--out","value","--to","2","--in","value"],
["graph","check","--graph","/Game/Float.kbgraph"],
["entity","add","--create","Cube","--name","Floater","--position","0,1,0"],
["scene-attach","--node","Floater","--script","/Game/Float.kbgraph"]
]
```

Check, as a call of its own: `[["run","--frames","30"]]`. Done when `check` says `"valid":true` and `run` ends with `moved: Floater 0.00,1.00,0.00 -> 0.00,3.00,0.00` and `0 diagnostics`.

- Find a node: `graph catalog --filter <word>` lists ids with their pins (`Function.Transform.Translate`, `Self.SetProperty.Float`, `Branch`, `Sequence`). An id is the `--entry`; a function node's pins are its inputs and outputs by name.
- A value node: `Literal:Float`, `Literal:Int`, `Literal:String`, `Literal:Bool`; change one with `graph literal --node <id> --value <text>`. Other events: `graph event --node 1 --value Created`. `graph show` prints the whole graph; `graph remove`, `move` and `disconnect --edge <index>` edit it.
- A rejected edit leaves the file as it was; `check` also tests the types of every connection. For logic in text, a Lua script is simpler: guide `recipe-script`.
)md";

constexpr std::string_view kCsg = R"md(# CSG: a room, a wall with a hole, from boxes added and cut away

A `.kbcsg` source lists brushes in order: the first is a `union` that makes the solid, later ones `union`, `subtract` or `intersect`. `csg build` turns it into a mesh and a collision mesh; give each source its own `--output` folder (the files are always called `Geometry`).

```
[
["csg","new","--name","Hall"],
["csg","set","--source","/Game/Geometry/Hall.kbcsg","--data","{\"schema\":1,\"brushes\":[{\"operation\":\"union\",\"kind\":\"box\",\"position\":[0,0,0],\"rotation\":[0,0,0],\"scale\":[1,1,1],\"size\":[6,3,6]},{\"operation\":\"subtract\",\"kind\":\"box\",\"position\":[0,0,0],\"rotation\":[0,0,0],\"scale\":[1,1,1],\"size\":[5,3,5]}]}"],
["csg","build","--source","/Game/Geometry/Hall.kbcsg","--output","/Game/Geometry/Hall"],
["entity","add","--name","Hall","--position","0,1.5,0"],
["component","add","--entity","Hall","--type","MeshRenderer","--set","mesh=/Game/Geometry/Hall/Geometry.21kb"],
["component","add","--entity","Hall","--type","Collider","--set","shape=Mesh"],
["component","set","--entity","Hall","--type","Collider","--set","mesh=/Game/Geometry/Hall/Geometry.kbcollision"]
]
```

Done when `csg build` prints `mesh:`, `collision:`, `volume:` (33 for this hall) and `triangles:`, and the last command prints `updated Collider`. Look at it: `capture --view overview`, and `run --frames 60` ends with `0 diagnostics`.

- Every brush has `kind` (`box`), `position`, `rotation` (degrees), `scale` and `size`, all of them written out; `csg show --source <path>` prints a valid example. A brush that is not closed or a wrong field is refused with the reason, and the file stays as it was.
- Change the shape later: `csg set` again, `csg build` again to the same `--output`; the entity keeps pointing at it.
)md";

constexpr std::string_view kWorldPartition = R"md(# World partition: a world too big to load at once

A world is split into cells of a set size; only the cells near a streaming source (an object with a Stream Focus) are loaded. Convert the objects of the startup scene, build the cells, and place the world in a scene that the game starts with:

```
[
["entity","add","--create","Cube","--name","Tower","--position","-60,1,0"],
["entity","add","--create","Cube","--name","Hut","--position","60,1,0"],
["world","migrate","--out","/Game/Worlds/Main.21kbworld","--cell-size","32"],
["world","build","--world","/Game/Worlds/Main.21kbworld"],
["scene-new","--name","Level","--template","Empty","--default","yes"],
["entity","add","--scene","Level","--name","Streamer","--position","0,2,0"],
["component","add","--scene","Level","--entity","Streamer","--type","StreamFocus","--set","outerRadius=96","--set","innerRadius=64"],
["entity","add","--scene","Level","--name","World"],
["component","add","--scene","Level","--entity","World","--type","ContentInstance","--set","kind=PartitionedWorld","--set","assetId=/Game/Worlds/Main.21kbworld"]
]
```

Check, as a call of its own: `[["run","--scene","Level","--frames","60","--report","world-memory"]]`. Done when `world build` says `built N cells`, and the report has `"worlds":1` and `"resident_units"` above 0.

- `migrate` copies the objects of a scene into a world (the scene itself is not changed, so the old objects stay in it: do not load both). Set `kind` before `assetId`; `outerRadius` before `innerRadius` (the unload radius is larger).
- Move the streamer (a script moving the player, or the Stream Focus on the player) and cells load and unload; `run --report world-memory` shows what is resident.
- Rebuild after changing the world: `world build`. `world chunks` lists the pack chunks a build makes per region. Data layers, HLOD and navigation per cell: docs/world_partition.md.
)md";

constexpr std::string_view kPacksAndMods = R"md(# Packs and mods: a build, a pack, content added by someone else

For a person at a terminal in the project folder (the signing key must stay outside the project, which is why an agent in the editor cannot do it). `build` cooks the game into `Game.kbpack` and a folder that runs; a mod is a pack of what the game does not already have, signed with the author's key.

```
kb_cli build --project . --out Saved/Build/game
kb_cli scene-new --project . --name Crates
kb_cli entity add --project . --scene Crates --create Cube --name ModCrate --color "#33AAFF"
kb_cli project-settings --project . scenes --add Crates
kb_cli build --project . --out Saved/Build/cook
kb_cli keys generate --out ../author.key
kb_cli mod build --game Saved/Build/game/Game.kbpack --cook Saved/Build/cook/Game.kbpack --id demo.crates --version 1.0.0 --entry /Game/Scenes/Crates.21kbscene --key ../author.key --output Saved/Build/demo.crates
kb_cli mod trust --mods Saved/Build/game/Mods --add <the public key `keys generate` printed>
kb_cli mod install --mods Saved/Build/game/Mods --game Saved/Build/game/Game.kbpack Saved/Build/demo.crates
kb_cli mod verify --mods Saved/Build/game/Mods --game Saved/Build/game/Game.kbpack
```

Done when `mod build` says `3 asset(s) added` and sealed, and `mod verify` ends with `1 would load, 0 refused`. The first build is the game as shipped; the second is the same project with the mod's content, and the mod holds only the difference.

- A mod adds assets: it cannot replace a game asset, change settings or bring native code. It is cooked against one build of the game and refused for another.
- `mod list|disable|enable|remove --mods <folder> [<id>]`; a script lists what loaded with `CallFunction("Mods.Count", {})`. Without `mod trust`, a signed mod is refused as `UntrustedSigner`.
- Inspect or split a pack: `pack info <file.kbpack>`, `pack split`, `pack patch` (updates), `pack sign|verify`. A game for other platforms: `build --platform android|web|linux`. Details: docs/modding.md.
)md";

constexpr std::array<ScriptAgentGuide, 41> kGuides{ {
    { "recipe-material", "Recipe: colour or material", "A colour or material on an object: two commands.", kRecipeMaterial },
    { "recipe-component", "Recipe: physics or a component", "Physics (a falling body) or any component on an object, and how to check it.", kRecipeComponent },
    { "recipe-prefab", "Recipe: prefab", "Save an object, with its components, as a prefab and place copies.", kRecipePrefab },
    { "recipe-script", "Recipe: Lua script", "A Lua script on an object (a spin, a movement): create, attach, validate, run.", kRecipeScript },
    { "recipe-pickup", "Recipe: a coin or pickup", "A coin the player collects by touching it: a trigger Collider, OnTriggerEnter, a counter on the screen; also how scripts react to touching.", kRecipePickup },
    { "recipe-light", "Recipe: light and time of day", "The sun, the sky and the ambient light for morning, evening or night.", kRecipeLight },
    { "recipe-settings", "Recipe: settings, plugins, builds", "The game's name, window, start scene, scenes in the build, tags, gravity, plugins and a build for a platform.", kRecipeSettings },
    { "recipe-ui-button", "Recipe: a button", "A button with a label on the screen, and what to add so a click does something.", kRecipeUiButton },
    { "recipe-player", "Recipe: a playable character", "A character the person walks with, with its camera, in one command.", kRecipePlayer },
    { "recipe-image", "Recipe: an icon, picture or texture", "An icon, UI picture, texture or sprite sheet drawn by the engine, in neon, flat, soft or pixel style.", kRecipeImage },
    { "recipe-sound", "Recipe: a sound effect or music loop", "A jump, coin, hit, explosion or other sound effect, an ambient or music loop, made by the engine.", kRecipeSound },
    { "recipe-timeline", "Recipe: an intro or cutscene", "A camera flight or any object moving over time as a timeline that plays when the scene starts, with events.", kRecipeTimeline },
    { "recipe-ui-theme", "Recipe: the look of the interface", "A shared UI theme: dark or light, colours, orange buttons, rounded corners, applied to the controls of a scene.", kRecipeUiTheme },
    { "recipe-translate", "Recipe: texts in other languages", "The texts of the game in several languages: keys on the controls, translations, the default language, switching in play.", kRecipeTranslate },
    { "recipe-animation", "Recipe: an object that moves by itself", "A coin that bobs, a door that opens and closes, a pulse: an animation clip that plays from the start.", kRecipeAnimation },
    { "recipe-material-look", "Recipe: a ready-made material look", "A look in the customer's words (gold, wood, glass, neon, lava, toon, dissolve, water, hologram): find a preset, put it on an object, preview it, or write a whole graph in one call.", kRecipeMaterialLook },
    { "recipe-outline", "Recipe: an outline or a glow through walls", "An outline (obrys, kontur) round a character or object, thick and even, cartoon style, and a highlight of it visible through a wall (xray).", kRecipeOutline },
    { "recipe-water", "Recipe: a lake, sea or river", "Realistic water (woda, jezioro, morze, rzeka) for a lake, sea, river or pool: refraction, depth, foam and waves on a computer, a simple version on a phone.", kRecipeWater },
    { "recipe-terrain-layers", "Recipe: a mountain with many textured layers", "A mountain, hill or landscape (gora, teren, krajobraz) with 8 to 10 textured layers by height and slope, blended, that fits a phone.", kRecipeTerrainLayers },
    { "recipe-material-function", "Recipe: a reusable material effect", "A material function (glowing edge, scrolling or tiling texture coordinate) written once and used on several materials.", kRecipeMaterialFunction },
    { "recipe-save", "Recipe: save the game", "A value kept between runs of the game (a score, a counter) in a save slot.", kRecipeSave },
    { "recipe-navmesh", "Recipe: a character that walks to a place", "A character that walks to a point around obstacles on a baked navigation mesh.", kRecipeNavmesh },
    { "recipe-collision", "Recipe: what collides with what", "Physics layers: objects that pass through each other, and the collision events.", kRecipeCollision },
    { "recipe-joint", "Recipe: a hinge, pendulum or spring", "A joint that holds a body to another: hinge, rope, slider, spring, limits, motor.", kRecipeJoint },
    { "materials", "Materials: kinds, blend modes, phones", "Surface, UI, post-process and decal materials, shading and blend modes, two-sided, and what is too heavy for a phone.", kMaterials },
    { "objects-and-materials", "Objects and materials", "Shapes, lights and cameras in a scene, their position, materials and colours.", kObjectsAndMaterials },
    { "physics", "Physics", "Rigidbodies, colliders, triggers, character controllers and collision events.", kPhysics },
    { "scripts", "Behaviour scripts", "Write a Lua behaviour, attach it to an entity, expose variables, call the engine.", kScripts },
    { "characters-and-animation", "Characters and animation", "Import a character, state machines, blends, sockets and weapons.", kCharacters },
    { "combat-and-ai", "Combat and AI", "Abilities, health and teams, AI brains from presets and rules.", kCombatAndAi },
    { "maps-2d", "2D maps and platformers", "Tile maps with grounds, borders, objects and floors; 2D players and platforms with 2D physics.", kMaps2D },
    { "terrain", "Terrain", "A heightmap ground: brushes, ramps, noise, stamps, material layers painted by hand or by slope, heightmap import and export.", kTerrain },
    { "menus-and-buttons", "Menus, buttons and UI events", "A menu or HUD with buttons that run a script: widgets, OnUIClicked, event names, what validate warns about.", kMenusAndButtons },
    { "ui-audio-particles", "UI, sound and particles", "On-screen text and buttons, sounds that play, particle effects.", kUiAudioParticles },
    { "water-and-look", "Water, sky, fog and post-processing", "Translucent water, sky colour, fog, bloom and other post effects.", kWaterAndLook },
    { "audio-mixer", "Audio mixer", "Buses, volumes, effects and snapshots: put a sound on a bus and check it plays.", kAudioMixer },
    { "gameplay-graphs", "Gameplay graphs", "Logic made of nodes and connections (.kbgraph): add, connect, check, attach.", kGameplayGraphs },
    { "csg", "CSG shapes", "A room or a wall with a hole built from boxes added and cut away, with its collision.", kCsg },
    { "world-partition", "World partition", "A world too big to load at once: cells that stream around the player.", kWorldPartition },
    { "packs-and-mods", "Packs and mods", "Build a game pack, make a signed mod from a project, install and verify it (for a terminal).", kPacksAndMods },
    { "verify", "Checking your work", "Validate scripts and run scenes headless before you finish.", kVerify },
} };

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower{ text };
    std::ranges::transform(lower, lower.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}

} // namespace

std::span<const ScriptAgentGuide> ScriptAgentGuides::All() noexcept {
    return kGuides;
}

// The summary of a guide cut to a line of an index: at a word, without a trailing comma or colon.
[[nodiscard]] static std::string ShortSummary(std::string_view summary) {
    constexpr std::size_t kLongest = 64U;
    std::string text{ summary };
    if (text.size() > kLongest) {
        const std::size_t space = text.rfind(' ', kLongest);
        text.resize(space == std::string::npos ? kLongest : space);
    }
    while (!text.empty() && (text.back() == ',' || text.back() == ':' || text.back() == ';' || text.back() == ' ' || text.back() == '.')) text.pop_back();
    return text;
}

std::string ScriptAgentGuides::Index() {
    std::string out = "# Guides\n\n";
    out += "One per line; `guide` with the name as `topic` returns it. The recipe-* guides are for jobs (AGENTS.md says which fits).\n\n";
    for (const ScriptAgentGuide& guide : kGuides) {
        out += "- `" + std::string{ guide.name } + "` - " + ShortSummary(guide.summary) + "\n";
    }
    return out;
}

std::string ScriptAgentGuides::Suggest(std::string_view topic) {
    const std::string wanted = Lower(topic);
    // The words of the topic that say something: "recipe-coin" is looking for "coin", not for every recipe.
    std::vector<std::string> words;
    for (std::size_t start = 0U; start < wanted.size();) {
        const std::size_t end = wanted.find_first_of("-_ ", start);
        const std::string word = wanted.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (word.size() >= 3U && word != "recipe" && word != "guide") words.push_back(word);
        start = end == std::string::npos ? wanted.size() : end + 1U;
    }
    std::vector<std::string> names;
    std::vector<const ScriptAgentGuide*> found;
    for (const ScriptAgentGuide& guide : kGuides) {
        names.emplace_back(guide.name);
        // A word of the topic in the name or the summary: "coin" finds recipe-pickup, "light" recipe-light.
        if (std::ranges::any_of(words, [&](const std::string& word) { return Lower(guide.name).find(word) != std::string::npos || Lower(guide.summary).find(word) != std::string::npos; })) found.push_back(&guide);
    }
    if (found.empty()) {
        const std::string nearest = ClosestScriptName(wanted, std::span<const std::string>{ names });
        for (const ScriptAgentGuide& guide : kGuides) {
            if (!nearest.empty() && guide.name == nearest) found.push_back(&guide);
        }
    }
    if (found.size() > 3U) found.resize(3U);
    std::string out;
    for (const ScriptAgentGuide* guide : found) {
        out += (out.empty() ? "" : ", ") + std::string{ "`" } + std::string{ guide->name } + "`";
    }
    return out;
}

std::optional<std::string> ScriptAgentGuides::Topic(std::string_view name) {
    std::string wanted = Lower(name);
    if (wanted.size() > 3U && wanted.ends_with(".md")) {
        wanted.resize(wanted.size() - 3U);
    }
    if (wanted.empty() || wanted == "index" || wanted == "readme") {
        return Index();
    }
    for (const ScriptAgentGuide& guide : kGuides) {
        if (guide.name == wanted) {
            return std::string{ guide.body };
        }
    }
    return std::nullopt;
}

} // namespace kb::script
