#include "engine/script/ScriptAgentGuides.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace kb::script {
namespace {

constexpr std::string_view kObjectsAndMaterials = R"md(# Objects and materials

Every line is one `kb_cli` command run from the project folder (through the kb-engine `kb_cli`
tool, pass the words after `kb_cli` as `args`; the project is filled in for you). Commands work
on the startup scene unless you add `--scene <path>`.

A red cube three metres up:

```
kb_cli entity add --project . --create Cube --name RedCube --position 0,3,0
kb_cli material-new --project . --name Red --set "Base Color=#E03C31" --set Roughness=0.4
kb_cli component set --project . --entity RedCube --type MeshRenderer --set material=/Game/Materials/Red.kbmat
```

- `--create` makes what the editor's Create menu makes: Cube, Sphere, Capsule, Cylinder, Cone,
  Plane, Quad, Directional Light, Point Light, Spot Light, Area Light, Camera, Environment,
  Particle System, Audio Source, Audio Listener, and UI items (Canvas, Text, Button, ...).
  Shapes come with a Mesh Renderer and a Collider.
- Place it with `--position x,y,z`, `--rotation x,y,z` (degrees) and `--scale x,y,z`, or later with
  `kb_cli component set --project . --entity RedCube --type Transform --set localPosition=0,3,0`.
- Material fields: Base Color, Roughness, Metallic, Emissive Color, Emissive Strength,
  Normal Scale, Occlusion Strength, Alpha Cutoff. A colour is `#RRGGBB` or linear `r,g,b` (0..1).
  Textures: Base Color Texture, Normal Texture, Metallic Roughness Texture, Occlusion Texture,
  Emissive Texture, each an image of the project (`/Game/Textures/Albedo.png`) or `none`.
- Change a material later: `kb_cli material-set --project . --material /Game/Materials/Red.kbmat --set Metallic=1`;
  print it: `kb_cli material-show --project . --material /Game/Materials/Red.kbmat`.
- A light: `kb_cli entity add --project . --create "Point Light" --position 0,4,0`, then
  `kb_cli component set --project . --entity "Point Light" --type Light --set intensity=8 --set range=10`.
- See what a scene holds: `kb_cli scene-list --project . --scene Assets/Scenes/Main.21kbscene`.
- Remove or rename: `kb_cli entity remove --project . --name RedCube`,
  `kb_cli entity rename --project . --name RedCube --to Crate`.

Check with `kb_cli run --project . --frames 30`; it must end with `0 diagnostics`.
)md";

constexpr std::string_view kPhysics = R"md(# Physics

Shapes made with `entity add --create` already have a Collider; give them a Rigidbody to move.

```
kb_cli entity add --project . --create Cube --name Crate --position 0,5,0
kb_cli component add --project . --entity Crate --type Rigidbody --set bodyType=Dynamic --set mass=2
```

- Rigidbody: `bodyType` Static, Dynamic or Kinematic; `mass`, `gravityScale`, `useGravity`,
  `linearDamping`, `angularDamping`, `lockRotation`, `useContinuousCollision`,
  `linearVelocity.x/y/z`.
- Collider: `shape` (Box, Sphere, Capsule, Mesh), `boxSize.x/y/z`, `radius`, `height`,
  `center.x/y/z`, `trigger`, `friction`, `restitution`, `layer`. An entity has one Collider;
  change it with `component set`, not `component add`.
- A floor that never moves: a Plane or Cube with a Static Rigidbody, or no Rigidbody at all.
- Character movement: the Character Controller component (`radius`, `height`,
  `slopeLimitDegrees`, `stepOffset`), or `entity add --create "Top Down Player"` /
  `"Platformer Player"` for a ready player with input.
- Scripts react with `function OnCollisionEnter(self, event)` (the other entity is
  `event.args.other`); raycasts and forces are in the `Physics` API group (`api_reference`
  topic `Physics`).

Check with `kb_cli run --project . --frames 120`: falling bodies must not report diagnostics.
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

- `-- @expose <name> <Type> [= default]` makes a variable editable in the Inspector; read it with
  `self:GetVariable`.
- Component properties: `self:GetProperty(component, property)` / `self:SetProperty(...)`; each
  component's properties are an `api_reference` topic (for example `Transform`).
- Engine functions are grouped (`Input`, `Physics`, `Transform`, `World`, `Audio`, ...): ask
  `api_reference` for a group, then for the one function you call.
- The sandbox has no `io`, `os`, `require` or `load`; share code with `-- @import` modules and
  `Import("Module.Name")`.
- Ready-made controllers live in `Assets/Samples/` (third person, top down, platformer,
  shooter).

A script is done when `validate` says OK and `kb_cli run --project . --frames 120` reports no
script diagnostics.
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

- Clips of another rig (Mixamo, Unreal, Rigify) play too: bones are matched by name and role;
  `animator check --character` shows how many bones follow each clip.
- Scripts drive animation through the `Animator` API group (`api_reference` topic `Animator`).
)md";

constexpr std::string_view kCombatAndAi = R"md(# Combat and AI

```
# An ability and a Combatant (health, mana, resistances, team, senses, 8 ability slots).
kb_cli ability new --project . --name Fireball --set damageType=Fire --set amount=40 --set range=12 --set radius=3 --set manaCost=10 --set animatorTrigger=Cast
kb_cli component add --project . --entity Hero --type Combatant --set team=1 --set maximumHealth=200 --set maximumMana=50 --set ability1=/Game/Abilities/Fireball.kbability

# A whole AI in one line: Combatant + AI Brain from a preset (Melee, Ranged, Guard, Coward, Wanderer).
kb_cli entity add --project . --name Goblin --create Capsule --position 0,1,8
kb_cli ai make --project . --entity Goblin --preset Melee --health 60
kb_cli ai rule --project . --entity Goblin --rule 4 --when HealthBelowPercent=20 --do Flee
kb_cli ai show --project . --entity Goblin
```

- Teams 1 and 2 are enemies; 0 is nobody's.
- The AI Brain is up to 8 rules, "when <condition> do <action>", checked from the top.
  Conditions: Always SeesEnemy KnowsEnemy EnemyCloserThan EnemyFartherThan HealthBelowPercent
  ManaAbovePercent AbilityReady FarFromHome. Actions: Idle Wander ReturnHome Chase UseAbility
  KeepDistance Flee. It walks the navigation mesh with a NavAgent and drives the Animator
  (float `Speed`, triggers `Attack`, `Hit`, `Die`, bool `Dead`).
- Scripts: `Gameplay.ApplyDamage`, `Gameplay.Heal`, `Gameplay.UseAbility`, `Gameplay.Health`,
  `Gameplay.NearestEnemy` (`api_reference` topic `Gameplay`); a combatant's script receives
  `OnDamaged`, `OnHealed`, `OnDeath`, `OnRevived`, `OnAbilityUsed`, `OnEnemySpotted`,
  `OnEnemyLost` with details in `event.args`.
)md";

constexpr std::string_view kMaps2D = R"md(# 2D tile maps

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
)md";

constexpr std::string_view kVerify = R"md(# Checking your work

Always finish with these, and fix what they report:

```
kb_cli validate --project . Assets/Scripts/MyScript.lua   # syntax and sandbox load of a script
kb_cli run --project . --frames 120                        # the startup scene headless
kb_cli scene-list --project . --scene Assets/Scenes/Main.21kbscene   # what the scene holds
```

- `run` prints script logs, events and errors; a change is done when it ends with `0 diagnostics`.
- `run --report input` or `--report particles` prints a versioned report for those systems.
- Scenes, projects and assets are binary: change them only through `kb_cli` or the kb-engine
  tools, never by editing or parsing the files.
)md";

constexpr std::array<ScriptAgentGuide, 7> kGuides{ {
    { "objects-and-materials", "Objects and materials", "Shapes, lights and cameras in a scene, their position, materials and colours.", kObjectsAndMaterials },
    { "physics", "Physics", "Rigidbodies, colliders, triggers, character controllers and collision events.", kPhysics },
    { "scripts", "Behaviour scripts", "Write a Lua behaviour, attach it to an entity, expose variables, call the engine.", kScripts },
    { "characters-and-animation", "Characters and animation", "Import a character, state machines, blends, sockets and weapons.", kCharacters },
    { "combat-and-ai", "Combat and AI", "Abilities, health and teams, AI brains from presets and rules.", kCombatAndAi },
    { "maps-2d", "2D tile maps", "Tile maps with grounds, borders, objects and floors; 2D players.", kMaps2D },
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
