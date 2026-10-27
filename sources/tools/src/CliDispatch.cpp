#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/core/JsonValue.hpp"

#include <array>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::cli {
namespace {

constexpr std::array<std::string_view, 3U> kUsageParts{
R"(kb_cli — 21kb Engine headless tooling for developers and AI coding agents

Usage: kb_cli <command> [options]

Commands:
  commands     List registered commands, availability and MCP tool names as JSON.
  new          Create a project, the same one the Hub creates, in a new or empty
               folder. Its startup scene Assets/Scenes/Main.21kbscene is made from
               a template: Basic (camera, sun, sky, floor; default) or Empty.
                 --parent <dir> --name <Name> [--template Basic|Empty]
  scene-new    Create a scene from a template, as File > New Scene does.
                 --project <dir> --name <Name> [--folder Assets/Scenes]
                 [--template Basic|Empty] [--default yes|no]
               (--default yes makes it the scene the game starts in)
  script-new   Create a script, as the editor's Create menu does: Lua (one .lua
               file) or C++ (a .native asset, with its source and CMake project in
               Source/NativeScripts/<Name>). Existing files are never overwritten.
                 --project <dir> --name <Name> --language lua|cpp
                 [--folder Assets/Scripts]
  material-new Create a material, as the editor's Create > Material does, with
               its values set at once. Existing files are never overwritten.
                 --project <dir> --name <Name> [--folder Assets/Materials]
                 [--set Field=value ...]
  material-set Change values of a material.
                 --project <dir> --material /Game/<path>.kbmat --set Field=value [...]
  material-show Print the values of a material.
                 --project <dir> --material /Game/<path>.kbmat
               (fields: Base Color, Roughness, Metallic, Emissive Color, Emissive
               Strength, Normal Scale, Occlusion Strength, Alpha Cutoff; a colour is
               #RRGGBB as in a colour picker, or linear r,g,b 0..1; textures: Base
               Color Texture, Normal Texture, Metallic Roughness Texture, Occlusion
               Texture, Emissive Texture = an image of the project, e.g.
               /Game/Textures/Albedo.png, or none)
  material-graph Edit a material's graph, as the Material Editor does; every change
               is saved and checked as the editor's Save checks it.
                 show --project <dir> --material /Game/<path>.kbmat
                 kinds
                 add-node --project <dir> --material <path> --kind <Kind>
                          [--name <Name>] [--value <value>] [--at x,y]
                 connect --project <dir> --material <path> --from <node> [--from-pin <pin>]
                         --to <node> --to-pin <pin>
                 set --project <dir> --material <path> --node <node> --value <value>
                 promote --project <dir> --material <path> --node <node> [--name <Name>]
                 rename --project <dir> --material <path> --node <node> --name <Name>
                 setting --project <dir> --material <path> --name <Domain | Shading Model |
                         Blend Mode> --value <value>
               (<node> is a node id, its name, its parameter id, or Output; kinds as
               kinds lists them, also ScalarParameter, VectorParameter,
               TextureParameter, Constant, Constant3Vector, Constant4Vector; a value is a
               number, r g b [a], #RRGGBB, or a texture path; other nodes take their
               settings as the Material Editor shows them, e.g. Rotator "speed centerU
               centerV", Panner "speedU speedV", TextureCoordinate "uTile vTile set";
               Blend Mode: opaque, masked, translucent, additive, modulate; Shading Model:
               unlit, defaultLit, clearCoat, ...)
  material-instance-new Create a material instance of a material, as Create Material
               Instance does (beside its parent unless --folder), with overrides.
                 --project <dir> --parent /Game/<path>.kbmat [--name <Name>]
                 [--folder Assets/...] [--set Parameter=value ...]
  material-instance-set Change or clear overrides of a material instance.
                 --project <dir> --instance /Game/<path>.kbmatinst
                 [--set Parameter=value ...] [--clear Parameter ...]
  material-instance-show Print a material instance: its parent and effective values.
                 --project <dir> --instance /Game/<path>.kbmatinst
               (a Parameter is one the parent's graph exposes, by name, or a material
               field as material-set names it; a Mesh Renderer takes the instance as
               its material: component set ... --set material=<path>.kbmatinst)
  post-process-new Create a post-process profile: the image settings (tonemapping,
               exposure, bloom, anti-aliasing, vignette, ambient occlusion) a scene
               can use, as a Unity Volume Profile. Existing files are never overwritten.
                 --project <dir> --name <Name> [--folder Assets/PostProcess]
                 [--set field=value ...]
  post-process-set Change values of a post-process profile.
                 --project <dir> --profile /Game/<path>.kbppfx --set field=value [...]
               (fields as the .kbppfx file names them, e.g. tonemapEnabled=0,
               outputTransform.tonemap=None, outputTransform.autoExposure.enabled=0,
               outputTransform.exposureStops=0, bloomEnabled=0, vignetteEnabled=1,
               vignetteIntensity=0.3, ambientOcclusionEnabled=1,
               ambientOcclusionIntensity=1, ambientOcclusionRadius=0.6,
               globalIllumination=Off|ScreenSpace|Voxel, globalIlluminationIntensity=1,
               fogEnabled=1, fogDensity=0.02, fogHeightFalloff=0.2, fogBaseHeight=0,
               fogColor.r=0.6, fogColor.g=0.65, fogColor.b=0.7, volumetricFogEnabled=1,
               fogAnisotropy=0.3; an unknown
               field or a value it does not take is refused with the field list)
  scene-post-process Give a scene (the startup scene unless --scene) its post-process
               profile, or none for the renderer's defaults.
                 --project <dir> [--scene <path>] --profile /Game/<path>.kbppfx|none
)",
R"(  audio-mixer  Create, edit and assign an audio bus graph (.kbmixer).
                 new --project <dir> --name <Name> [--folder Assets/Audio]
                     [--bus <Bus> ...] [--set Bus.parent|volume|mute=value ...]
                 set --project <dir> --mixer <path> [--bus <Bus> ...]
                     [--remove-bus <Bus> ...] [--set Bus.parent|volume|mute=value ...]
                 show --project <dir> --mixer <path>
                 assign --project <dir> [--scene <path>] --mixer <path>|none
                     [--snapshot <Name>]   (parent '-' means the implicit master)
               Snapshot edits for new/set: [--add-snapshot <Name> ...]
                     [--snapshot <Name> --set-snapshot Bus=volume ...]
               Additional set edits: [--remove-snapshot <Name> ...]
                     [--snapshot <Name> --clear-snapshot-bus <Bus> ...]
               DSP edits for new/set: --effect-bus <Bus>
                     [--add-effect Id=lowpass|highpass|equalizer|delay|reverb|compressor|limiter ...]
                     [--remove-effect <Id> ...] [--move-effect Id=index ...]
                     [--set-effect Id.parameter=value ...]  (indexes start at 0)
               new/set accept --dry-run; new/set/show accept --json.
  animator     Build an Animator Controller, the state machine that picks a character's
               animation; every change is validated and saved at once.
                 new --project <dir> --name <Name> --clip <clip> [--clip ...]
                     [--folder Assets/Animation]   (a state per clip, named after it:
                     Hero_Walk -> Walk; starts in Idle when there is one)
                 show|check --project <dir> --controller <path> [--character <.kbskeletalmesh>]
                 param add|remove --project <dir> --controller <path> --name <Name>
                     [--type float|int|bool|trigger] [--default <value>]
                 state add|remove|default --project <dir> --controller <path> --name <State>
                     [--clip <clip> | --blend <FloatParameter> --child <threshold>=<clip> ...]
                 transition add|remove --project <dir> --controller <path> --from <State|Any>
                     --to <State> [--when <condition> ...] [--exit-time 0..1] [--duration <s>]
                     (conditions: Speed>0.1 Speed<0.1 Lives==3 Grounded !Grounded Jump;
                     Any = from every state, checked first; --exit-time 1 = when the
                     source state's clip ends, with or without a condition)
                 locomotion --project <dir> --controller <path> --parameter Speed
                     --clip 0=<idle> --clip 1.5=<walk> --clip 4=<run>
                     (one state blending the clips by Speed, at one phase whatever
                     their lengths; the layer starts in it unless --default no)
               A character in a scene: entity add --name Hero, then
               component set --entity Hero --type MeshRenderer --set mesh=<.kbskeletalmesh>
               (draws it with its skeleton), component add --type Animator and
               component set --type Animator --set controller=<.kbanimcontroller>.
               Its clips may come from another character, even one rigged and named another
               way (Mixamo, Unreal, Blender/Rigify): bones are matched by name and role and the
               motion is retargeted; check --character shows how many bones follow each clip.
  skeleton     Read a character's bones and edit its sockets (named grip points on a bone).
                 show --project <dir> --skeleton <.kbskeleton | .kbskeletalmesh>
                 socket add --project <dir> --skeleton <path> --name <Socket> --bone <Bone>
                     [--position x,y,z] [--rotation x,y,z (degrees)]
                 socket remove --project <dir> --skeleton <path> --name <Socket>
  ability      Make what a combatant can do (a swing, a fireball, a heal) as a .kbability.
                 new --project <dir> --name <Name> [--folder Assets/Abilities] [--set key=value ...]
                 set --project <dir> --ability <path> --set key=value [...]
                 show --project <dir> --ability <path>
                 (keys: name, target Enemy|Ally|Self, range, radius (0 = one target), amount,
                 damageType Physical|Fire|Cold|Lightning|Poison|Healing, manaCost, cooldown,
                 animatorTrigger. A fighter: component add --entity Hero --type Combatant
                 --set team=1 --set maximumHealth=200 --set ability1=/Game/Abilities/Fireball.kbability;
                 teams 1 and 2 are enemies, 0 fights nobody)
  ai           Give an object a whole AI at once: a Combatant (health, team, senses) and an
               AI Brain - "when <condition>, do <action>" rules checked from the top, filled
               by a preset. The Gameplay.AI plugin runs it (added to the project if missing).
                 presets --project <dir>
                 make --project <dir> --entity <Name> [--preset Melee|Ranged|Guard|Coward|Wanderer]
                      [--team <n>] [--health <n>] [--mana <n>] [--speed <m/s>] [--sight <m>]
                      [--ability1 <.kbability> ...]
                 rule --project <dir> --entity <Name> --rule <1-8> --when <Condition>[=value]
                      --do <Action>[=value]   (conditions: Always SeesEnemy KnowsEnemy
                      EnemyCloserThan=m EnemyFartherThan=m HealthBelowPercent=p ManaAbovePercent=p
                      AbilityReady=slot FarFromHome=m Never; actions: Idle Wander ReturnHome Chase
                      UseAbility=slot KeepDistance=m Flee)
                 show --project <dir> --entity <Name>
               e.g. a goblin pack: entity add --name Goblin --create Capsule; ai make --entity Goblin
               --preset Melee --health 60 --ability1 /Game/Abilities/Slash.kbability
)"
R"(  map2d        2D tile maps (World, 2D Map Editor plugin, on by default): what the editor's World 2D
               Map component and Map Editor window do - grounds with automatic borders (Remere's Map
               Editor style), sprites as items or grounds, floors -50..50, a Map Camera for Play.
                 new --project <dir> --name <Entity> [--scene <path>] [--size 64x64]
                     [--orientation flat|wall] [--view 15x11]   (map file, Mesh Renderer, Map Camera)
                 ground new --project <dir> --name Grass --texture "/Game/2D Map Sprites/terrain.png"
                     [--cell-size 32] [--z 30] [--tiles 25] [--island 16] [--hole 19]
                     [--border n=<cell>,cse=<cell>,...]   (island: top-left sprite of a 3x3 island,
                     hole: of a 2x2 hole; a higher --z borders onto lower grounds and empty tiles)
                 ground set --project <dir> --name <Name> (same options) | ground list --project <dir>
                 object new --project <dir> --name Oak --texture "/Game/2D Map Sprites/Nature/trees.png"
                     --cell <top-left sprite> [--size 2x2]   (a tree, a house: placed in one click)
                 palette --project <dir>   (categories = folders of Assets/2D Map Sprites)
                 paint --project <dir> --map <path> --at x,y[,floor] [--to x,y] [--size n]
                     (--ground <Name> | --object <Name> | --sprite <texture> --cell <n> [--cell-size 32]
                     [--as-ground]) [--no-borders]
                 fill --project <dir> --map <path> --ground <Name> --at x,y[,floor]
                 erase --project <dir> --map <path> --at x,y[,floor] [--to x,y] [--items]
                 block|unblock --project <dir> --map <path> --at x,y[,floor] [--to x,y]
                 borderize --project <dir> --map <path> [--floor n]
                 set --project <dir> --map <path> [--size WxH] [--orientation flat|wall] [--view WxH]
                     [--camera-pixels-per-unit n] [--camera-follow <Name>|none] [--camera-smoothing seconds]
                     [--camera-dead-zone w,h] [--camera-clamp yes|no]   (the play camera, see docs/world_2d_camera.md)
                 view --project <dir> --entity <Name>   (update the Map Camera and painted collision)
                 info --project <dir> --map <path>
                 path --project <dir> --map <path> --from x,y --to x,y [--floor n] [--clearance 0..8]
                     [--diagonal none|safe|free] [--cost-property <name>] [--max-visited 1..1048576]
                     [--timeout-ms <milliseconds>] (default 25 ms; JSON; exits 0 found, 2 no path, 3 budget)
               Sprite sheets go into Assets/2D Map Sprites/<Category>; cells count row by row from 0.
  vfx-new      Create a particle effect (.kbvfx) as the editor's New Particle Effect.
                 --project <dir> --name <Name> [--folder Assets/Particles] [--set key=value ...]
  vfx-show     Print a particle effect: every key=value of its file.
                 --project <dir> --effect /Game/<path>.kbvfx
  renderer-info  Print the graphics backend a hidden bgfx host selects as JSON: capabilities, limits,
                 formats and the reason for every quality fallback (Windows).
                 [--backend auto|d3d11|d3d12|vulkan|opengl] [--allow-fallback]
                 Exits 1 when an explicit backend is not the one selected, unless --allow-fallback.
  vfx-profile  Profile an isolated effect with a hidden bgfx GPU host (Windows).
                 --project <dir> --effect /Game/<path>.kbvfx
                 [--backend d3d11|d3d12|vulkan|opengl] [--warmup 60] [--frames 180]
                 [--samples 64] [--require-gpu]
                 [--gpu-buffer-budget <bytes>] (0: engine limit; bounds logical buffers)
               Versioned JSON includes actual routing, buffer readback, process/thread
               CPU time, GPU timestamps and memory. Asset backend policy is preserved.
  vfx-set      Change values of a particle effect (keys as vfx-show prints them, e.g.
               effect.emitter[0].spawn.shape.type=Sphere).
                 --project <dir> --effect <path> --set key=value [...]
  vfx-emitter-add / vfx-emitter-remove   Add an emitter / remove one by id.
                 --project <dir> --effect <path> [--name <Name>] | --emitter <id>
  vfx-module-add / vfx-module-remove     Add a module (Gravity, Wind, Drag, Noise,
               TorusSwirl, Separation, ...) to an emitter / remove one by id.
                 --project <dir> --effect <path> --emitter <id> --type <Type> [--target <id>]
                 | --module <id>
  batch        Run many kb_cli commands as one transaction on a project: all of them
               take effect, or none (one fails: every file below the project is put back
               and the exit code is 1). --dry-run runs them the same way, then puts the
               files back and prints what would change, file by file with a short diff.
                 --project <dir> --file <operations> [--dry-run]
               (the file has one command per line, # starts a comment, "quotes" keep
               spaces together: entity add --name Hero; a .json file or a file starting
               with [ holds an array of operations, each an array of words. --project is
               added to each; files outside the project, and Binaries, Intermediate,
               Saved, Library, Build, .git, are not restored; a project over 512 MiB is
               refused; batch and mcp cannot be used in it. With --json it prints
               {"schema":"21kb.batch/v1","ok","dryRun","applied","rolledBack",
               "failedOperation","operations":[...],"changes":[{"path","status",
               "bytesBefore","bytesAfter","diff"}],"errors":[...]})
  script-build Build the project's C++ scripts (or one), as Play and the cooker
               do. Exits 1 when a build fails.
                 --project <dir> [--script <path>]
  entity       Add or remove a scene entity (the project's startup scene unless --scene).
                 add --project <dir> [--scene <path>] --name <Name> [--parent <Name>]
                     [--create <Item>] [--position x,y,z] [--rotation x,y,z (degrees)] [--scale x,y,z]
                 (--create makes what the editor's Create menu makes: Cube, Sphere, Capsule,
                 Cylinder, Cone, Plane, Quad, Directional Light, Point Light, Spot Light,
                 Area Light, Camera, Environment, Particle System, Audio Source, Audio Listener;
                 --name then defaults to the item. --create 'Platformer Player' or 'Top Down Player'
                 makes a ready-made 2D player named Player: a CharacterController, a copy of
                 Platformer2DPlayer.lua or TopDown2DPlayer.lua in Assets/Scripts (never overwritten;
                 speed, jumpSpeed, coyoteTime, jumpBuffer are its @expose lines), default Move and
                 Jump input when the project has no input context, and every map of the scene
                 following the player; --physics 2d makes it a Character Controller 2D on the
                 2D physics with Platformer2DBox2DPlayer.lua or TopDown2DBox2DPlayer.lua;
                 see docs/world_2d_presets.md)
                 remove --project <dir> [--scene <path>] --name <Name>
                 rename --project <dir> [--scene <path>] --name <Name> --to <NewName>
                        [--occurrence <n>]
                 attach --project <dir> [--scene <path>] --name <Object> --to <Character>
                        --socket <Socket|Bone> [--position x,y,z] [--rotation x,y,z] [--scale x,y,z]
                        (a sword in a hand: parents the object under the character and holds it
                        at the socket - or bone - every frame after animation; the offset is in
                        the socket's space, --scale is the object's own size)
                 detach --project <dir> [--scene <path>] --name <Object>
                 (--name, --parent and --entity take a name or a path: Parent/Child;
                 names are unique among siblings, --occurrence picks one of several
                 same-named entities made elsewhere)
  component    Add, set or remove a component of an entity.
                 add|set|remove --project <dir> [--scene <path>] --entity <Name>
                     --type <Component> [--set name=value ...]
                 (vectors at once: --set localPosition=0,2,-8; Transform rotation in
                 degrees: --set rotation=35,30,0)
  schema       List components and their properties.
  api          Generate the script API contract (Lua stubs, Markdown, JSON).
                 --project <dir> | --out <dir> | --print markdown|json|lua
  api-check    Compare the current API surface against a committed baseline
               and fail on breaking changes (CI compatibility gate).
                 --baseline <path> [--project <dir>] [--update-baseline]
  init-agent   Provision a game project for AI coding agents (AGENTS.md,
               .luarc.json, .kb/api/*).
                 --project <dir>
  import       Import external source assets into the project's runtime asset
               format (.21kb plus metadata), as the editor's Import... does.
                 --project <dir> --destination /Game/<folder> <file> [more files...]
                 [--preset /Game/<path>.kbimportpreset] [--scale <factor>]
                 [--rotation x,y,z (degrees)] [--import-materials yes|no]
                 [--import-textures yes|no] [--skeletal auto|yes|no]
               (a model keeps its scale and rotation in its import, as Unity's
               Scale Factor; flags override the preset. --skeletal auto, the default,
               imports a skinned model - or an armature with animations and no mesh -
               as a character: Skeleton, Skeletal Mesh and one Animation Clip per
               source animation, named after it, all listed with '+'; FBX, glTF and
               GLB as Blender exports them)
               Reimport a changed source into the asset it was imported to, in place (the
               asset keeps its id and path, so scenes keep working):
                 --project <dir> --reimport /Game/<path>.21kb <changed source file>
                 [--on-conflict report|keep-local|overwrite] [--diff]
               The textures and materials of the last import are kept in a record beside
               the asset. One edited since then is a local override: report (the default)
               refuses and changes nothing, keep-local leaves it, overwrite replaces it.
               --diff lists what would change and writes nothing (also for a character
               import). What a reimport overwrote is backed up first:
                 --project <dir> --list-backups | --undo <backup id>
  import-preset-new Create an import preset (.kbimportpreset), as the editor's
               Create > Import Preset; the import dialog and import --preset use it.
                 --project <dir> --name <Name> [--folder Assets/ImportPresets]
                 [--set field=value ...]
               (fields: scale, rotation, importMaterialSlots, importTextures,
               importMaterials, combineMeshes, importSkeletalMesh)
  validate     Validate Lua behaviour scripts (syntax + sandbox load).
                 [--project <dir>] <file.lua> [more files...]
  scene-list   List nodes, components, and behaviours of a scene file.
                 [--project <dir>] --scene <path>
  scene-attach Attach a behaviour asset to a scene node and save the scene.
                 --project <dir> --scene <path> --node <name> --script <path>
                 [--tick-group <group>] [--execution-order <n>] [--disabled]
)",
R"(  run          Run a scene headless and report logs, events, and errors (the
               project's startup scene unless --scene). Exits 1 on errors,
               124 on playback timeout, or 130 on cancellation.
                 --project <dir> [--scene <path>] [--frames <n>] [--dt <seconds>] [--realtime]
                 [--quiet] [--report particles|audio|audio-meters|ecs|world-memory|input] [--pcm-frames <n>]
                 [--timeout-ms <1..86400000>] [--cancel-file <path>]
                 [--input-replay <recording>] [--input-record <recording>]
                 [--viewport WIDTHxHEIGHT] [--safe-area LEFT,TOP,RIGHT,BOTTOM]
               Replay uses recorded per-tick durations and controller slots; --frames
               may select a prefix. --dt and audio probes cannot override replay timing.
               Recording captures input before each completed tick, including a partial
               run stopped by cancellation. Replay does not guarantee world determinism.
               Playback limits are cooperative between ticks and during paced waits;
               startup, an executing callback, and shutdown are not forcibly interrupted.
               Create the cancellation file to stop; the command does not remove it.
               Reports are versioned JSON on stdout; logs go to stderr.
               Audio probes use explicit offline PCM rendering (up to 600 ticks
               and 1920000 total PCM frames). --pcm-frames overrides audio frames
               per tick; the default follows the sample rate and --dt.
               --report world-memory states what the streamed worlds hold by kind (assets,
               instances, scripts, physics, graphics; each measured, estimated or
               unavailable) against what the streaming budget charged; see
               docs/world_memory.md.
  mcp          Serve the commands above as MCP tools over stdio (newline-
               delimited JSON-RPC), for use from MCP-compatible clients.
                 [--project <dir>]
  keys         Manage release signing keys. Private keys are refused inside a
               project or a repository; keep them out of version control.
                 generate --out <key file>
                 public --key <key file>
                 content-key --out <file>
                 anchor --key <key file> --product <id> [--content-key <file>]
                        --out <file>
  pack         Sign (and optionally encrypt) an asset pack, or verify one;
               inspect, recompress, split and patch packs and pack sets.
                 sign --key <key file> [--content-key <file>] <pack.kbpack>
                 verify [--anchor <file> | --public-key <hex>
                        [--content-key <file>]] <pack.kbpack>
                 info <pack.kbpack>
                 compress [--level <0-19>] <input.kbpack> <output.kbpack>
                 split --base <base.kbpack> --chunk <label>=<prefix>[,...]
                       [--chunk ...] [--chunk-cells <label>=<world.21kbworld>
                       [@<x0>:<z0>..<x1>:<z1>][#<layer>,...] ...]
                       [--level <0-19>] [--index <file>]
                       <cooked.kbpack>
                 patch --current <Game.kbpackset | Game.kbpack>
                       --patch-level <n> --output <patch.kbpack>
                       [--label <label>] [--level <0-19>]
                       [--current-release <dir>] <new-cook.kbpack>
                 set-keys [--content-key <file>]
                          [--previous-release <dir> ...] <Game.kbpackset>
                 set-verify [--anchor <file> | --public-key <hex>]
                            <Game.kbpackset>
  mod          Mods of a game (content and scripts in a chunk pack; docs/modding.md).
                 build --game <Game.kbpackset | Game.kbpack> --cook <cook.kbpack>
                       --id <id> --version <x.y.z> --output <folder>
                       [--name <name>] [--author <text>] [--description <text>]
                       [--api <range>] [--requires <id>[@<range>]]...
                       [--conflicts <id>]... [--entry <scene path>]
                       [--key <signing key file>] [--current-release <dir>]
                 install --mods <Mods folder> [--game <Game pack>]
                         [--trust <public key>]... [--allow-unsigned]
                         [--replace] <mod folder>
                 list|verify --mods <Mods folder> [--game <Game pack>]
                 enable|disable|remove --mods <Mods folder> <id>
                 trust --mods <Mods folder> [--add <public key>]
                       [--remove <public key>] [--allow-unsigned|--deny-unsigned]
                 info [--game <Game pack>] <mod folder>
  release      Sign the manifest of a finished release directory, or verify one
               against the trust anchor of the player it ships.
                 sign --key <key file> --dir <dir> --product <id>
                      --content-version <version> --release <number>
                      [--anti-rollback]
                 verify [--anchor <file>] <dir>
  world        Convert a single-file scene into a partitioned world (one file
               per object), or build a world's streaming cells.
                 migrate --scene <file.21kbscene> --out <file.21kbworld>
                         [--cell-size <metres>] [--project <dir>]
                 build --world <file.21kbworld> [--project <dir>]
               build writes the cells, the cell index and the HLOD proxies,
               exactly as the editor and kb_cooker do.
                 chunks --project <dir> [--exclude <prefix>[,<prefix>...]]
               chunks prints one pack chunk rule per region of every built world.
               A world whose file enables navigation also gets one navigation
               mesh per cell from build.
  navmesh      Bake a scene's navigation mesh, or describe a baked one.
                 bake --scene <file.21kbscene> [--project <dir>]
                      [--out <file.21kbnavmesh>] [--cell-size <metres>]
                      [--cell-height <metres>] [--tile-cells <n>]
                      [--agent <name>:<radius>:<height>:<climb>:<slope>]...
               bake writes <scene>.21kbnavmesh by default, exactly as the
               editor's Bake Navigation Mesh does; settings not given are
               those of the mesh it replaces, else the defaults.
                 info <file.21kbnavmesh>
  physics-layers  Show or edit the project's layer collision matrix.
                 --project <dir> [--name <index>=<Name>]...
                 [--ignore <layer>,<layer>]... [--collide <layer>,<layer>]...
               Layers are indexes 0-30 or names. The asset is the project's
               [Physics] LayersAsset, created at
               /Game/Settings/PhysicsLayers.21kbphysicslayers when it has none.
  lighting-quality  Show or set the project's shadow and global illumination quality.
                 --project <dir> [--shadows <level>] [--shadows-mobile <level>]
                 [--gi <level>] [--gi-mobile <level>]
               Levels are auto, low, medium, high and ultra. Auto leaves the choice to
               the engine; the mobile settings apply on phones and tablets.

Global options:
  --json       Print one JSON object instead of text, for scripts and agents:
                 {"schema":"21kb.cli-result/v1","ok":true,"exitCode":0,
                  "command":"entity","output":"<stdout>","errors":["<stderr line>",...]}
               "ok" is true when the exit code is 0; "errors" lists what the command
               wrote to stderr. A command that prints its own JSON (audio-mixer,
               run --report, api --print json, batch) keeps it unchanged. mcp ignores --json.
               Native compiler logs go to process stderr and never precede the JSON object.
  --dry-run    Only for the commands that name it (audio-mixer new/set).

Exit codes: normally 0 on success and 1 on failure (bad arguments, an unknown
command, a failed check or build, or inaccessible files). Commands may document
additional codes; run returns 124 on timeout and 130 on cancellation.
help, --help, -h and no command at all print this text and exit 0.

Scene paths may be physical (relative to the project root) or virtual
("/Game/Scenes/Main.21kbscene", requires --project).
)"
};

void PrintUsage(std::ostream& output) {
    for (const auto part : kUsageParts) output << part;
}

constexpr std::array<std::string_view, 15> kFlagNames{
    "--anti-rollback", "--disabled", "--quiet", "--update-baseline", "--require-gpu", "--allow-fallback", "--as-ground", "--no-borders", "--items",
    "--diff", "--list-backups", "--allow-unsigned", "--deny-unsigned", "--replace", "--realtime" };

#if KB_CLI_RENDERER
constexpr bool kRendererAvailable = true;
#else
constexpr bool kRendererAvailable = false;
#endif

constexpr CliCommandDescriptor kCommands[]{
    {"animator", &RunAnimatorCommand, CliCommandKind::Normal, true, ""},
    {"skeleton", &RunSkeletonCommand, CliCommandKind::Normal, true, ""},
    {"ability", &RunAbilityCommand, CliCommandKind::Normal, true, ""},
    {"ai", &RunAiCommand, CliCommandKind::Normal, true, ""},
    {"api", &RunApiCommand, CliCommandKind::Normal, true, "api_reference"},
    {"api-check", &RunApiCheckCommand, CliCommandKind::Normal, true, ""},
    {"entity", &RunEntityCommand, CliCommandKind::Normal, true, "entity_edit"},
    {"component", &RunComponentCommand, CliCommandKind::Normal, true, "component_edit"},
    {"audio-mixer", &RunAudioMixerCommand, CliCommandKind::Normal, true, ""},
    {"schema", &RunSchemaCommand, CliCommandKind::Normal, true, "component_schema"},
    {"new", &RunNewCommand, CliCommandKind::Normal, true, ""},
    {"init-agent", &RunInitAgentCommand, CliCommandKind::Normal, true, ""},
    {"import", &RunImportCommand, CliCommandKind::Normal, true, "import_asset"},
    {"import-preset-new", &RunImportPresetNewCommand, CliCommandKind::Normal, true, ""},
    {"validate", &RunValidateCommand, CliCommandKind::Normal, true, "validate_script"},
    {"scene-list", &RunSceneListCommand, CliCommandKind::Normal, true, "scene_list"},
    {"scene-attach", &RunSceneAttachCommand, CliCommandKind::Normal, true, "scene_attach"},
    {"scene-new", &RunSceneNewCommand, CliCommandKind::Normal, true, "scene_create"},
    {"script-new", &RunScriptNewCommand, CliCommandKind::Normal, true, "script_create"},
    {"script-build", &RunScriptBuildCommand, CliCommandKind::Normal, true, ""},
    {"run", &RunRunCommand, CliCommandKind::Normal, true, "run_scene"},
    {"keys", &RunKeysCommand, CliCommandKind::Normal, true, ""},
    {"pack", &RunPackCommand, CliCommandKind::Normal, true, ""},
    {"mod", &RunModCommand, CliCommandKind::Normal, true, ""},
    {"release", &RunReleaseCommand, CliCommandKind::Normal, true, ""},
    {"world", &RunWorldCommand, CliCommandKind::Normal, true, ""},
    {"navmesh", &RunNavMeshCommand, CliCommandKind::Normal, true, ""},
    {"physics-layers", &RunPhysicsLayersCommand, CliCommandKind::Normal, true, ""},
    {"lighting-quality", &RunLightingQualityCommand, CliCommandKind::Normal, true, ""},
#if KB_CLI_RENDERER
    {"map2d", &RunMap2DCommand, CliCommandKind::Normal, true, {}},
    {"material-new", &RunMaterialNewCommand, CliCommandKind::Normal, true, {}},
    {"material-set", &RunMaterialSetCommand, CliCommandKind::Normal, true, {}},
    {"material-show", &RunMaterialShowCommand, CliCommandKind::Normal, true, {}},
    {"material-graph", &RunMaterialGraphCommand, CliCommandKind::Normal, true, {}},
    {"material-instance-new", &RunMaterialInstanceNewCommand, CliCommandKind::Normal, true, {}},
    {"material-instance-set", &RunMaterialInstanceSetCommand, CliCommandKind::Normal, true, {}},
    {"material-instance-show", &RunMaterialInstanceShowCommand, CliCommandKind::Normal, true, {}},
    {"post-process-new", &RunPostProcessNewCommand, CliCommandKind::Normal, true, {}},
    {"post-process-set", &RunPostProcessSetCommand, CliCommandKind::Normal, true, {}},
    {"scene-post-process", &RunScenePostProcessCommand, CliCommandKind::Normal, true, {}},
    {"renderer-info", &RunRendererInfoCommand, CliCommandKind::Normal, true, {}},
    {"vfx-profile", &RunVfxProfileCommand, CliCommandKind::Normal, true, {}},
#else
    {"map2d", nullptr, CliCommandKind::Normal, false, {}},
    {"material-new", nullptr, CliCommandKind::Normal, false, {}},
    {"material-set", nullptr, CliCommandKind::Normal, false, {}},
    {"material-show", nullptr, CliCommandKind::Normal, false, {}},
    {"material-graph", nullptr, CliCommandKind::Normal, false, {}},
    {"material-instance-new", nullptr, CliCommandKind::Normal, false, {}},
    {"material-instance-set", nullptr, CliCommandKind::Normal, false, {}},
    {"material-instance-show", nullptr, CliCommandKind::Normal, false, {}},
    {"post-process-new", nullptr, CliCommandKind::Normal, false, {}},
    {"post-process-set", nullptr, CliCommandKind::Normal, false, {}},
    {"scene-post-process", nullptr, CliCommandKind::Normal, false, {}},
    {"renderer-info", nullptr, CliCommandKind::Normal, false, {}},
    {"vfx-profile", nullptr, CliCommandKind::Normal, false, {}},
#endif
    {"vfx-new", nullptr, CliCommandKind::Particle, kRendererAvailable, {}},
    {"vfx-show", nullptr, CliCommandKind::Particle, kRendererAvailable, {}},
    {"vfx-set", nullptr, CliCommandKind::Particle, kRendererAvailable, {}},
    {"vfx-emitter-add", nullptr, CliCommandKind::Particle, kRendererAvailable, {}},
    {"vfx-emitter-remove", nullptr, CliCommandKind::Particle, kRendererAvailable, {}},
    {"vfx-module-add", nullptr, CliCommandKind::Particle, kRendererAvailable, {}},
    {"vfx-module-remove", nullptr, CliCommandKind::Particle, kRendererAvailable, {}},
    {"batch", nullptr, CliCommandKind::Batch, true, {}},
    {"mcp", nullptr, CliCommandKind::Mcp, true, {}},
    {"help", nullptr, CliCommandKind::Help, true, {}},
    {"commands", nullptr, CliCommandKind::Catalog, true, {}},
};

// CLI dispatch and MCP discovery resolve operations through this same catalog.
std::optional<int> RunNamedCommand(
    std::string_view command, const ArgumentList& arguments, std::istream& in, CommandIo io) {
    if (command == "--help" || command == "-h") command = "help";
    const auto* operation = FindCliCommand(command);
    if (operation == nullptr) return std::nullopt;
    if (!operation->available) {
        io.err << "error: " << command << " needs the renderer; this kb_cli was built with KB_BUILD_RENDERER=OFF\n";
        return 1;
    }
    switch (operation->kind) {
    case CliCommandKind::Normal: return operation->run(arguments, io);
    case CliCommandKind::Particle:
#if KB_CLI_RENDERER
        return RunVfxCommand(command, arguments, io);
#else
        return 1;
#endif
    case CliCommandKind::Batch:
        return RunBatchCommand(arguments, io, [&in](std::span<const std::string> words, CommandIo target) {
            return RunCli(words, in, target);
        });
    case CliCommandKind::Mcp: return RunMcpCommand(arguments, in, io);
    case CliCommandKind::Help: PrintUsage(io.out); return 0;
    case CliCommandKind::Catalog: {
        using kb::core::JsonValue;
        auto result = JsonValue::MakeObject();
        result.Set("schema", JsonValue::MakeString("21kb.cli-commands/v1"));
        auto operations = JsonValue::MakeArray();
        for (const auto& entry : CliCommandCatalog()) {
            auto row = JsonValue::MakeObject();
            row.Set("name", JsonValue::MakeString(std::string{entry.name}));
            row.Set("available", JsonValue::MakeBool(entry.available));
            row.Set("mcpTool", entry.mcpTool.empty() ? JsonValue::MakeNull() : JsonValue::MakeString(std::string{entry.mcpTool}));
            operations.Append(std::move(row));
        }
        result.Set("commands", std::move(operations));
        io.out << result.Dump() << '\n';
        return 0;
    }
    }
    return std::nullopt;
}

} // namespace

std::span<const CliCommandDescriptor> CliCommandCatalog() noexcept { return kCommands; }

const CliCommandDescriptor* FindCliCommand(std::string_view name) noexcept {
    for (const auto& command : CliCommandCatalog())
        if (command.name == name) return &command;
    return nullptr;
}

std::string ToValidUtf8(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0U; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 1U;
        if (lead >= 0xC2U && lead <= 0xDFU) length = 2U;
        else if (lead >= 0xE0U && lead <= 0xEFU) length = 3U;
        else if (lead >= 0xF0U && lead <= 0xF4U) length = 4U;
        bool valid = lead < 0x80U || (length > 1U && index + length <= text.size());
        for (std::size_t offset = 1U; valid && offset < length; ++offset) {
            valid = (static_cast<unsigned char>(text[index + offset]) & 0xC0U) == 0x80U;
        }
        if (valid && length >= 3U) {
            const auto second = static_cast<unsigned char>(text[index + 1U]);
            valid = !(lead == 0xE0U && second < 0xA0U) && !(lead == 0xEDU && second >= 0xA0U) &&
                !(lead == 0xF0U && second < 0x90U) && !(lead == 0xF4U && second >= 0x90U);
        }
        if (valid) {
            result.append(text.substr(index, length));
            index += length;
        } else {
            result += "\xEF\xBF\xBD";
            ++index;
        }
    }
    return result;
}

namespace {

// The command's stderr, one entry per non-empty line, without the "error: " prefix.
kb::core::JsonValue StderrLines(const std::string& text) {
    constexpr std::string_view kPrefix = "error: ";
    auto lines = kb::core::JsonValue::MakeArray();
    std::istringstream stream{ text };
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.starts_with(kPrefix)) line.erase(0U, kPrefix.size());
        lines.Append(kb::core::JsonValue::MakeString(ToValidUtf8(line)));
    }
    return lines;
}

// Runs `run` with its output captured and prints one result object. A command that printed a
// JSON object of its own keeps it: it goes out unchanged, with its stderr and exit code.
int RunAsJson(std::string_view command, CommandIo io, const std::function<int(CommandIo)>& run) {
    std::ostringstream out;
    std::ostringstream err;
    int exitCode = 1;
    try {
        exitCode = run(CommandIo{ .out = out, .err = err });
    } catch (const std::exception& exception) {
        err << "error: " << exception.what() << '\n';
    } catch (...) {
        err << "error: unexpected failure\n";
    }

    kb::core::JsonValue own;
    std::string parseError;
    if (kb::core::JsonValue::Parse(out.str(), own, parseError) && own.GetKind() == kb::core::JsonValue::Kind::Object) {
        io.out << out.str();
        io.err << err.str();
        return exitCode;
    }

    using kb::core::JsonValue;
    auto result = JsonValue::MakeObject();
    result.Set("schema", JsonValue::MakeString("21kb.cli-result/v1"));
    result.Set("ok", JsonValue::MakeBool(exitCode == 0));
    result.Set("exitCode", JsonValue::MakeNumber(exitCode));
    result.Set("command", JsonValue::MakeString(ToValidUtf8(command)));
    result.Set("output", JsonValue::MakeString(ToValidUtf8(out.str())));
    result.Set("errors", StderrLines(err.str()));
    io.out << result.Dump() << '\n';
    return exitCode;
}

} // namespace

int RunCli(std::span<const std::string> args, std::istream& in, CommandIo io) {
    if (args.empty()) {
        PrintUsage(io.out);
        return 0;
    }

    // --json may come before the command: kb_cli --json entity add ...
    std::vector<std::string> words{ args.begin(), args.end() };
    if (words.size() > 1U && words.front() == "--json") {
        words.erase(words.begin());
        words.emplace_back("--json");
    }
    const std::string_view command{ words.front() };
    const std::vector<std::string> rest{ words.begin() + 1, words.end() };
    std::vector<std::string_view> flagNames{kFlagNames.begin(), kFlagNames.end()};
    flagNames.insert(flagNames.end(), {"--json", "--dry-run"});
    ArgumentList arguments{ rest, flagNames };
    const bool json = arguments.Flag("--json") && command != "mcp";

    // Only audio-mixer and run --report act on --json themselves; for the rest it asks for the
    // result object, and the command must not see it.
    if (json && command != "audio-mixer" && command != "batch" && !(command == "run" && arguments.Option("--report").has_value())) {
        std::vector<std::string> withoutJson;
        for (const std::string& token : rest) {
            if (token != "--json") withoutJson.push_back(token);
        }
        arguments = ArgumentList{ withoutJson, flagNames };
    }

    const auto execute = [&](CommandIo target) {
        if (!arguments.Errors().empty()) {
            for (const std::string& error : arguments.Errors()) {
                target.err << "error: " << error << '\n';
            }
            return 1;
        }
        if (const std::optional<int> code = RunNamedCommand(command, arguments, in, target)) {
            return *code;
        }
        target.err << "error: unknown command '" << command << "'\n";
        if (!json) {
            target.err << '\n';
            PrintUsage(target.err);
        }
        return 1;
    };

    if (json) return RunAsJson(command, io, execute);
    return execute(io);
}

} // namespace kb::cli
