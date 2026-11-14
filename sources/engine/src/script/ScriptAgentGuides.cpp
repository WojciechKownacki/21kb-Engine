#include "engine/script/ScriptAgentGuides.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace kb::script {
namespace {

constexpr std::string_view kObjectsAndMaterials = R"md(# Objects and materials

Each line is one `kb_cli` command (through the kb-engine `kb_cli` tool: the words after `kb_cli`
as `args`). Commands change the startup scene unless you add `--scene <path>`; commands sent
together are safe, edits of one scene simply wait for each other.

One command makes a shape with its colour (a material named after it) and, if wanted, physics:

```
kb_cli entity add --project . --create Sphere --name RedBall --position 0,5,0 --color "#E03C31" --body Dynamic
```

- `--create`: Cube, Sphere, Capsule, Cylinder, Cone, Plane, Quad, Directional Light, Point
  Light, Spot Light, Area Light, Camera, Environment, Particle System, Audio Source, Audio
  Listener, UI items (Canvas, Text, Button, ...). Shapes come with a Mesh Renderer and a Collider.
- `--position x,y,z`, `--rotation x,y,z` (degrees; forward is +Z, `90,0,0` faces straight
  down), `--scale x,y,z`, `--parent <Name>`, `--look-at x,y,z|<Name>` (aims forward at a point
  or an object: cameras, spot lights).
- `--color` makes `/Game/Materials/<Name>.kbmat`; `--material /Game/...kbmat` uses an existing
  one instead. `--body Static|Dynamic|Kinematic` adds a Rigidbody.
- More material fields: `kb_cli material-new --project . --name Gold --set "Base Color=#D4AF37" --set Metallic=1 --set Roughness=0.3`,
  later `material-set --material /Game/Materials/Gold.kbmat --set ...`. Fields: Base Color,
  Roughness, Metallic, Emissive Color, Emissive Strength, Normal Scale, Occlusion Strength,
  Alpha Cutoff, and textures (Base Color Texture, Normal Texture, ... = `/Game/Textures/x.png`).
- Move, turn or aim later: `kb_cli entity set --project . --name "Main Camera" --position 0,4,-8 --look-at RedBall`.
- `--parent` makes positions, rotation and scale relative to the parent: a child of a scaled object is
  scaled too. Build an object from parts under an unscaled empty entity (`entity add --name Table`), each
  part with its own `--scale`; `entity set --name Leg1 --parent none` moves one back to the root.
- Change anything later: `kb_cli component set --project . --entity RedBall --type Transform --set localPosition=0,3,0`
  (any component: `--type MeshRenderer --set material=/Game/Materials/Gold.kbmat`,
  `--type Light --set intensity=8 --set range=10`).
- Read the scene: `kb_cli scene-list --project .` gives every object with its position
  (`@ x,y,z`); `kb_cli entity show --project . --name RedBall` gives one object's transform and
  every component value.
- A camera from above: `kb_cli entity add --project . --create Camera --name TopCamera --position 0,12,0 --look-at 0,0,0`,
  then `component set --entity TopCamera --type Camera --set primary=true` and the old camera
  `primary=false`.
- Remove or rename: `entity remove --name RedBall`, `entity rename --name RedBall --to Ball`.
- Reuse an object: `kb_cli prefab new --project . --from-entity Crate` saves `/Game/Prefabs/Crate.kbprefab`;
  `prefab place --prefab /Game/Prefabs/Crate.kbprefab --name Crate2 --position 3,0,0` adds a copy,
  and a script spawns one with `World.InstantiatePrefab("/Game/Prefabs/Crate.kbprefab", nil, nil, x, y, z)`.
)md";

constexpr std::string_view kPhysics = R"md(# Physics

A ball that falls and collides, in one command (shapes already have a Collider):

```
kb_cli entity add --project . --create Sphere --name Ball --position 0,5,0 --color "#E03C31" --body Dynamic
kb_cli run --project . --frames 120
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
- Scripts: `function OnCollisionEnter(self, event)` (the other entity is `event.args.other`);
  raycasts and forces: `api_reference` topic `Physics`.
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

```
kb_cli script-new --project . --name Spinner --language lua --folder Assets/Scripts
kb_cli scene-attach --project . --scene Assets/Scenes/Main.21kbscene --node RedCube --script /Game/Scripts/Spinner.lua
kb_cli validate --project . Assets/Scripts/Spinner.lua
```

- Keys and buttons are input actions: `kb_cli input-action new --project . --name Fire --key Space --key MouseLeft`
  (`--type Axis2D --key W=+y --key S=-y --key D=+x --key A=-x` for movement); scripts read
  `Input.Pressed("Fire")` and `Input.Vector2("Move")`; `input-action list` shows what exists.
- `-- @expose <name> <Type> [= default]` makes a variable editable in the Inspector; read it with
  `self:GetVariable`. `kb_cli scene-attach ... --set speed=8` gives one node its own value for it.
- Component properties: `self:GetProperty(component, property)` / `self:SetProperty(...)`; each
  component's properties are an `api_reference` topic (for example `Transform`).
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

A script is done when `validate` says OK and `kb_cli run --project . --frames 120` reports no
script diagnostics.
`kb_cli capture --project . --out shot.png` renders a frame headless to check what a scene looks like;
`kb_cli build --project . --out Build` cooks a folder the game runs from.

## C++ scripts

`kb_cli script-new --project . --name Spinner --language cpp` makes `/Game/Scripts/Spinner.native`
and its source `Source/NativeScripts/Spinner/Spinner.cpp`; `kb_cli script-build --project .`
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
# A character from Blender (glTF/GLB or FBX): Skeleton, Skeletal Mesh and one Animation Clip per
# action, named <file>_<action>.
kb_cli import --project . --destination /Game/Characters Hero.glb
kb_cli entity add --project . --name Hero
kb_cli component set --project . --entity Hero --type MeshRenderer --set mesh=/Game/Characters/Hero.kbskeletalmesh

# A state machine: one state per clip, a speed blend, transitions; check it before use.
kb_cli animator new --project . --name Hero --clip /Game/Characters/Hero_Idle.kbanim --clip /Game/Characters/Hero_Run.kbanim
kb_cli animator locomotion --project . --controller /Game/Animation/Hero.kbanimcontroller --parameter Speed --clip 0=/Game/Characters/Hero_Idle.kbanim --clip 4=/Game/Characters/Hero_Run.kbanim
kb_cli animator check --project . --controller /Game/Animation/Hero.kbanimcontroller --character /Game/Characters/Hero.kbskeletalmesh
kb_cli component add --project . --entity Hero --type Animator --set controller=/Game/Animation/Hero.kbanimcontroller

# A sword in the right hand: a socket on a bone, then attach an entity to it.
kb_cli skeleton show --project . --skeleton /Game/Characters/Hero.kbskeletalmesh
kb_cli skeleton socket add --project . --skeleton /Game/Characters/Hero.kbskeletalmesh --name weapon_r --bone hand_r --rotation 0,0,90
kb_cli entity add --project . --name Sword --create Cube
kb_cli entity attach --project . --name Sword --to Hero --socket weapon_r --scale 0.05,0.8,0.05
```

- Clips of another rig (common rig naming schemes) play too: bones are matched by name and role;
  `animator check --character` shows how many bones follow each clip.
- Scripts drive animation with `Animator.<Function>(inputs in order)`; the functions are
  `api_reference` topic `Animator`.
)md";

constexpr std::string_view kCombatAndAi = R"md(# Combat and AI

```
# An ability and a Combatant (health, mana, resistances, team, senses, 8 ability slots).
kb_cli ability new --project . --name Fireball --set damageType=Fire --set amount=40 --set range=12 --set radius=3 --set manaCost=10 --set animatorTrigger=Cast
kb_cli component add --project . --entity Hero --type Combatant --set team=1 --set maximumHealth=200 --set maximumMana=50 --set ability1=/Game/Abilities/Fireball.kbability

# A whole AI in one line: Combatant + AI Brain from a preset (Melee, Ranged, Guard, Coward, Wanderer).
kb_cli entity add --project . --name Goblin --create Capsule --position 0,1,8
kb_cli ai make --project . --entity Goblin --preset Melee --health 60
kb_cli ai rule --project . --entity Goblin --rule 1 --when HealthBelowPercent=20 --do Flee
kb_cli ai show --project . --entity Goblin
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
kb_cli map2d new --project . --name World --size 64x64 --view 15x11
# Sprite sheets (PNG, cells row by row from 0) go into Assets/2D Map Sprites/<Category>.
kb_cli map2d ground new --project . --name Grass --texture "/Game/2D Map Sprites/Ground/terrain.png" --island 16 --hole 19 --z 30
kb_cli map2d ground new --project . --name Dirt --texture "/Game/2D Map Sprites/Ground/terrain.png" --tiles 0,1 --weights 3,1 --z 10
kb_cli map2d object new --project . --name Oak --texture "/Game/2D Map Sprites/Nature/trees.png" --cell 0 --size 2x2
kb_cli map2d fill --project . --map /Game/Maps/World.kbmap2d --ground Dirt --at 0,0
kb_cli map2d paint --project . --map /Game/Maps/World.kbmap2d --ground Grass --at 10,10 --to 20,14
kb_cli map2d paint --project . --map /Game/Maps/World.kbmap2d --object Oak --at 14,12
kb_cli map2d info --project . --map /Game/Maps/World.kbmap2d
```

- A higher ground `--z` draws its edges over lower grounds; `--at x,y,z` paints another floor.
- Tile (x, y) of a flat map lies at X = x + 0.5 - width/2, Z = height/2 - y - 0.5 relative to its
  entity; floor z is z * 3 m up.
- 2D players: `kb_cli entity add --project . --create "Platformer Player"` or `"Top Down Player"`.

A 2D platformer without a tile map: the player preset, then platforms in the X/Y plane with 2D
physics, each one command (its Collider 2D follows `--scale`, so leave its size at 1x1):

```
kb_cli entity add --project . --create "Platformer Player" --position 0,2,0
kb_cli entity add --project . --create Cube --name Ground --position 10,-0.5,0 --scale 40,1,1 --color "#4C8C3A" --body2d Static
kb_cli entity add --project . --create Cube --name Platform1 --position 6,1.5,0 --scale 4,0.5,1 --body2d Static
```

The preset's camera follows the player; `run` prints where the player moved.
)md";

constexpr std::string_view kVerify = R"md(# Checking your work

Always finish with these, and fix what they report:

```
kb_cli validate --project . Assets/Scripts/MyScript.lua   # syntax and sandbox load of a script
kb_cli run --project . --frames 120                        # the startup scene headless
kb_cli scene-list --project . --scene Assets/Scenes/Main.21kbscene   # what the scene holds
```

- `run` prints script logs, events and errors; a change is done when it ends with `0 diagnostics`.
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
kb_cli terrain new --project . --name World --size 1024 --resolution 513 --preset mountains --seed 7
# Or a flat terrain, shaped by hand.
kb_cli terrain new --project . --name Hills --size 256 --resolution 257
kb_cli terrain brush --project . --terrain /Game/Terrains/Hills.kbterrain --mode raise --x 0 --z 0 --radius 30 --strength 4
kb_cli terrain brush --project . --terrain /Game/Terrains/Hills.kbterrain --mode raise --path "-80,-60;0,0;80,60" --radius 12 --strength 2
kb_cli terrain brush --project . --terrain /Game/Terrains/Hills.kbterrain --mode noise --x 0 --z 0 --radius 90 --strength 3 --noise-scale 24
kb_cli terrain brush --project . --terrain /Game/Terrains/Hills.kbterrain --mode ramp --x -60 --z 0 --to 60,0 --radius 6
kb_cli terrain generate --project . --terrain /Game/Terrains/Hills.kbterrain --kind ridged --seed 3 --amplitude 80 --scale 120
kb_cli terrain erode --project . --terrain /Game/Terrains/Hills.kbterrain --mode hydraulic --iterations 150
kb_cli terrain layer add --project . --terrain /Game/Terrains/Hills.kbterrain --material /Game/Materials/Grass.kbmat
kb_cli terrain layer add --project . --terrain /Game/Terrains/Hills.kbterrain --material /Game/Materials/Rock.kbmat
kb_cli terrain paint --project . --terrain /Game/Terrains/Hills.kbterrain --layer 1 --auto slope=35..90
kb_cli terrain preview --project . --terrain /Game/Terrains/Hills.kbterrain --out Exports/hills.png --kind height
kb_cli terrain info --project . --terrain /Game/Terrains/Hills.kbterrain
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
kb_cli entity add --project . --create Canvas --name Menu
kb_cli entity add --project . --create Button --name PlayButton --parent Menu
kb_cli component set --project . --entity Menu/PlayButton --type Text --set "content=Play"
kb_cli component set --project . --entity Menu/PlayButton --type Selectable --set eventName=PlayClicked
kb_cli script-new --project . --name MenuController --language lua
kb_cli scene-attach --project . --scene Assets/Scenes/Main.21kbscene --node PlayButton --script /Game/Scripts/MenuController.lua
kb_cli validate --project . Assets/Scenes/Main.21kbscene Assets/Scripts/MenuController.lua
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
- Change widgets from the script: `self:SetProperty("Text", "content", "Score: 3")`, or another
  object with `World.FindByName` and `World.SetPropertyFloat`.
- `validate` warns about what loads fine but never works: a widget outside a Canvas
  (`ui-no-canvas`), a Text without a font (`ui-text-no-font`), an `eventName` no script answers
  (`ui-event-no-handler`) and a Button that reacts to nothing (`ui-button-no-reaction`). `run` and the
  cook print the same warnings.
- `entity add --create Button` (or Text, Slider, ...) puts the widget under a Canvas, creating one when the scene has none, and gives
  every Text the bundled font; `component set ... --set eventTarget=Menu/Root` takes an object by its path.
- Whole screens: `kb_cli entity add --project . --create "Main Menu"` (also `Pause`, `HUD`, `Settings`) builds the title, buttons,
  health bar or sliders at once; the buttons carry event names (`PlayClicked`, `SettingsClicked`, `QuitClicked`, `ResumeClicked`,
  `MainMenuClicked`, `BackClicked`) for your script to answer.
- Try the menu without a window: `kb_cli run --project . --frames 12 --click Menu/PlayButton@5` presses the widget on frame 5 and says
  what was under the pointer and which script answered; `--report ui` lists every widget with its rectangle and its answering scripts.
)md";

constexpr std::string_view kUiAudioParticles = R"md(# UI, sound and particles

```
# A "Score: 0" label on screen: a Canvas with a Text under it.
kb_cli entity add --project . --create Canvas --name HUD
kb_cli entity add --project . --create Text --name Score --parent HUD
kb_cli component set --project . --entity HUD/Score --type Text --set "content=Score: 0" --set fontSize=32

# A sound on start: a .wav under Assets/ (copied there or `kb_cli import`).
kb_cli entity add --project . --create "Audio Source" --name Music
kb_cli component set --project . --entity Music --type AudioSource --set clipAssetId=/Game/Audio/Music.wav --set autoplay=true --set loop=true

# Particles: a particle effect asset, then a Particle System that plays it.
kb_cli vfx-new --project . --name Sparks
kb_cli entity add --project . --create "Particle System" --name Sparks --position 0,1,0
kb_cli component set --project . --entity Sparks --type "Particle Effect" --set effectAssetId=/Game/Particles/Sparks.kbvfx
```

- UI items for `--create`: Canvas, Text, Image, Button, Toggle, Slider, Progress Bar, Input
  Field, Dropdown, Scroll View, Horizontal Box, Vertical Box, Grid; put them under a Canvas.
- Scripts change text with `self:SetProperty("Text", "content", "Score: " .. score)`.
- A button that runs a script, and what `validate` says about UI: guide `menus-and-buttons`.
- Check sound and particles: `kb_cli run --project . --frames 60 --report audio` (or
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
kb_cli material-new --project . --name Water --set "Base Color=0.1,0.3,0.6,0.5" --set Roughness=0.05
kb_cli material-graph --project . setting --material /Game/Materials/Water.kbmat --name "Blend Mode" --value translucent
kb_cli material-graph --project . setting --material /Game/Materials/Water.kbmat --name "Shading Model" --value singleLayerWater
kb_cli entity add --project . --create Plane --name Water --scale 3,1,3 --material /Game/Materials/Water.kbmat
kb_cli component remove --project . --entity Water --type Collider

# Fog, bloom and vignette for the scene, and the sky colour.
kb_cli post-process-new --project . --name Look --set fogEnabled=1 --set fogDensity=0.03 --set bloomEnabled=1 --set vignetteEnabled=1
kb_cli scene-post-process --project . --profile /Game/PostProcess/Look.kbppfx
kb_cli component set --project . --entity Environment --type WorldBackdrop --set skyTint=0.4,0.6,1
```

- Any translucent material: the alpha of Base Color plus `Blend Mode` translucent.
- `kb_cli help material-graph` and `kb_cli help post-process-new` list every setting.
)md";

constexpr std::array<ScriptAgentGuide, 11> kGuides{ {
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

std::string ScriptAgentGuides::Index() {
    std::string out = "# How-to guides\n\n";
    out += "Read only the guide for your task: the kb-engine `guide` tool with its name as `topic`, or the file of that name ";
    out += "next to this one. The script API is separate: the `api_reference` tool, one function or component at a time.\n\n";
    for (const ScriptAgentGuide& guide : kGuides) {
        out += "- `" + std::string{ guide.name } + "` - " + std::string{ guide.summary } + "\n";
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
