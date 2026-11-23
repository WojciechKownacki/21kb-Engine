#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/core/JsonValue.hpp"
#include "engine/script/ScriptNameSuggestion.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace kb::cli {
#if KB_CLI_RENDERER
int RunCsgCommand(const ArgumentList&, CommandIo);
int RunRig2DCommand(const ArgumentList&, CommandIo);
#endif
namespace {

constexpr std::array<std::string_view, 6U> kUsageParts{
R"(kb_cli - 21kb Engine headless tooling for developers and AI coding agents

Usage: kb_cli <command> [options]

Commands:
  commands     List the commands as JSON: name and purpose. --command gives one command's
               subcommands and options (name, type, required, default), --full every command with them.
                 [--command <name>] [--full]
  help         One line per command, or the usage of one command.
                 [<command>]
  new          Create a project, the same one the Hub creates, in a new or empty
               folder. Its startup scene Assets/Scenes/Main.21kbscene is made from
               a template: Basic3D (camera, sun, sky, floor; default; "Basic" works
               too), Basic2D (orthographic camera, sprite floor), FirstPerson or
               ThirdPerson (a walking character with its script and input), TopDown
               (click to walk over a navigation mesh), Platformer2D (side view, 2D
               physics) or Vehicle (a drivable car), or Empty.
                 --parent <dir> --name <Name>
                 [--template Basic3D|Basic2D|FirstPerson|ThirdPerson|TopDown|Platformer2D|Vehicle|Empty]
  layout       Save, restore or reset the editor workspace for the next session.
                 list|save|restore|reset --project <dir> [--name <name>]
  scene-new    Create a scene from a template, as File > New Scene does.
                 --project <dir> --name <Name> [--folder Assets/Scenes]
                 [--template Basic3D|Basic2D|FirstPerson|ThirdPerson|TopDown|Platformer2D|Vehicle|Empty]
                 [--default yes|no]
               (--default yes makes it the scene the game starts in)
  scene-duplicate Copy a scene under a new name, with an identity of its own, as File >
               Save Scene As does. The scene is --scene, or the one the game starts in.
                 --project <dir> --name <Name> [--scene <path>] [--folder Assets/Scenes]
  script-new   Create a script, as the editor's Create menu does: Lua (one .lua
               file) or C++ (a .native asset, with its source and CMake project in
               Source/NativeScripts/<Name>). Existing files are never overwritten.
               C++ is for developers: it needs the engine SDK and a C++ toolchain
               (CMake and the Visual Studio C++ tools); a game's scripts are Lua.
                 --project <dir> --name <Name> --language lua|cpp
                 [--folder Assets/Scripts] [--code "<Lua text>"]
               (--code writes the whole Lua script at once instead of the template)
  scene-lightmap Bake static diffuse lighting and ray shadows into a UV0 texture.
                 --project <dir> --entity <receiver> --out /Game/<path>.tga [--scene <path>] [--resolution 128]
  scene-bake-lighting Bake the whole scene: static lights, bounces, generated lightmap UVs and light probes.
                 --project <dir> [--scene <path>] [--resolution <16..1024>] [--bounces <0..4>]
                 [--max-probes <1..65536>] [--probe-spacing <metres>]
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
               exposure, bloom, anti-aliasing, vignette, chromatic aberration, film
               grain, ambient occlusion) a scene can use. Existing files are never
               overwritten.
                 --project <dir> --name <Name> [--folder Assets/PostProcess]
                 [--set field=value ...]
  post-process-set Change values of a post-process profile.
                 --project <dir> --profile /Game/<path>.kbppfx --set field=value [...]
               (fields as the .kbppfx file names them, e.g. tonemapEnabled=0,
               outputTransform.tonemap=None, outputTransform.autoExposure.enabled=0,
               outputTransform.exposureStops=0, bloomEnabled=0, vignetteEnabled=1,
               vignetteIntensity=0.3, chromaticAberrationEnabled=1,
               chromaticAberrationIntensity=0.2, filmGrainEnabled=1, filmGrainIntensity=0.3,
               filmGrainSize=1, ambientOcclusionEnabled=1,
               ambientOcclusionIntensity=1, ambientOcclusionRadius=0.6,
               screenSpaceReflectionsEnabled=1, screenSpaceReflectionsIntensity=1,
               screenSpaceReflectionsMaxDistance=25, motionBlurEnabled=1,
               motionBlurIntensity=0.5, motionBlurSamples=8,
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
               Snapshot edits:
                 new|set [--add-snapshot <Name> ...] [--snapshot <Name> --set-snapshot Bus=volume ...]
                 set [--remove-snapshot <Name> ...] [--snapshot <Name> --clear-snapshot-bus <Bus> ...]
               DSP edits (indexes start at 0):
                 new|set [--effect-bus <Bus>]
                     [--add-effect Id=lowpass|highpass|equalizer|delay|reverb|compressor|limiter ...]
                     [--set-effect Id.parameter=value ...]
                 set [--remove-effect <Id> ...] [--move-effect Id=index ...]
               new/set accept --dry-run; new/set/show accept --json.
                 new|set [--dry-run]
                 new|set|show [--json]
  blend-space  Author a 2D animation blend asset; every edit is validated and atomic.
                 new|show --project <dir> --space /Game/Animation/Motion.kbblend2d
                 add --project <dir> --space <path> --at x,y --clip <path>
                 set --project <dir> --space <path> --index <0..255> [--at x,y] [--clip <path>]
                 remove --project <dir> --space <path> --index <0..255>
                 sample --project <dir> --space <path> --at x,y
                 assign --project <dir> --space <path> --controller <path> --state <Name>
                     --x-param <FloatParameter> --y-param <FloatParameter> [--layer <Name>]
               assign replaces the named state's motion and creates missing Float axes.
               Outputs versioned JSON with sample indices, triangles and query weights.
  graph        Author gameplay nodes, typed values and connections; validate and save atomically.
                 catalog [--project <dir>] [--filter <text>]
                 new --project <dir> --graph <path>.kbgraph [--name <Name>]
                 show|check --project <dir> --graph <path>
                 add --project <dir> --graph <path> --entry <id> [--x <n>] [--y <n>] [--value <text>]
                 remove --project <dir> --graph <path> --node <id>
                 move --project <dir> --graph <path> --node <id> --x <n> --y <n>
                 literal|event|symbol --project <dir> --graph <path> --node <id> --value <text>
                 connect --project <dir> --graph <path> --from <id> --out <pin> --to <id> --in <pin>
                 disconnect --project <dir> --graph <path> --edge <index>
               Catalog, show and check print JSON.
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
               way: bones are matched by name and role and the
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
R"(  rig2d        Author a 2D skeletal rig (.kbrig2d) from a source object, or build it.
                 new --project <dir> --name <Name> [--folder /Game/Rigs]
                 show|build --project <dir> --rig /Game/<path>.kbrig2d
                 set --project <dir> --rig /Game/<path>.kbrig2d --data <source object>
  vector2d-new Create a 2D vector shape (.kbvector2d), a polygon or a line.
                 --project <dir> --name <Name> [--folder /Game/Shapes] [--kind Polygon|Line] [--points <JSON array>]
  vector2d-set Change the points or the width of a 2D vector shape.
                 --project <dir> --shape /Game/<path>.kbvector2d [--points <JSON array>] [--width <number>]
  vector2d-show Print a 2D vector shape.
                 --project <dir> --shape /Game/<path>.kbvector2d
  csg          Author a constructive solid geometry source (.kbcsg) and build it into a mesh.
                 new --project <dir> --name <Name> [--folder /Game/Geometry] [--data <JSON> | --data-file <file>]
                 show --project <dir> --source /Game/<path>.kbcsg
                 set --project <dir> --source /Game/<path>.kbcsg (--data <JSON> | --data-file <file>)
                 build --project <dir> --source /Game/<path>.kbcsg --output /Game/<folder>
)"
R"(  mesh-lod     Levels of detail of an imported model: how many levels, how many triangles each keeps and how
               much deviation it may add. Stored in the model asset, so the editor, the cooker and a game
               drawing the source build the same levels; a reimport keeps them.
                 show --project <dir> --mesh <model>    (settings; per level triangles, share, error, size drawn from)
                 generate --project <dir> --mesh <model> [--levels 1..8] [--keep 50] [--error 20] [--min-triangles 1024]
                 add --project <dir> --mesh <model> --triangles <percent> [--error <percent>]
                 remove --project <dir> --mesh <model> [--level <n>]
                 set --project <dir> --mesh <model> (--level <n> [--triangles <percent>] [--error <percent>] |
                     [--mode auto|manual] [--min-triangles <n>])
               (percents are of the finest level's triangles and of the mesh's extent; every level keeps fewer
               triangles than the one above; --levels 1 or a mesh under --min-triangles has no chain)
)"
R"(  terrain      Terrain files (.kbterrain): what the Terrain Editor does - brushes, material layers,
               heightmap import, generators and erosion - on a file, so an agent can shape ground without
               the editor. x, z are world units from the terrain's centre (terrain info prints the extent).
               Strokes, paths and batches are saved at once; a stroke that changes nothing is an error.
                 new --project <dir> (--name <Entity> [--scene <path>] [--position x,y,z] | --terrain <file>)
                     [--resolution 129] [--size 128|256x128] [--chunk-quads 32] [--lod-count 4]
                     [--preset flat|hills|mountains|island|plateau] [--seed 1] [--threads <n>] [--max-seconds <n>]
                     (flat unless a preset makes a whole world, generated and eroded; resolution 2^n+1, 17..2049)
                 info --terrain <file>    (size, heights, slopes, layers, heightHash)
                 sample --terrain <file> --x <n> --z <n>    (height, normal, slope, layer weights)
                 brush --terrain <file> [--mode raise|lower|smooth|flatten|noise|terrace|cut-hole|fill-hole|ramp|stamp]
                     [--shape soft-round|hard-round|linear-round|bell|ring|speckle]
                     (--x <n> --z <n> | --path "x,z;x,z") [--radius 8] [--strength 1] [--falloff 0.65]
                     [--invert yes (Ctrl: raise lowers, ...)] [--symmetry x|z|xz (mirrored across x=0 and/or z=0)]
                     [--spacing <n>] (strength is per stamp; a path stamps every --spacing, default a quarter radius)
                     flatten: [--height <n>|sample]   terrace: [--step 2] [--softness 0..1]   noise: [--seed <n>]
                     [--noise-kind gradient|value] [--noise-scale 16] [--noise-octaves 3]
                     ramp: --x/--z the start, --to x,z the end, [--height <n> --to-height <n>] (none: the terrain's own),
                     --radius the half width, --falloff the flat share of the strip
                     stamp: --image <heightmap.png> [--rotation 0] [--blend add|max|min] [--height <base for max/min>]
                     (strength = height of white; the image covers a square of side 2 x radius)
                 apply --terrain <file> --ops <ops.json | inline JSON>    (a batch: [{"op":"brush","mode":"raise","x":0,"z":0}, ...])
                 layer list|add|set|remove --terrain <file> [--material </Game/....kbmat> | --material-id <n>] [--index <n>]   (up to 4)
                 paint --terrain <file> --layer <n> (--x <n> --z <n> | --path "x,z;x,z") [--radius 8] [--opacity 1] [--falloff 0.65]
                     [--shape soft-round] [--erase yes] | --layer <n> --auto slope=35..90,height=-5..40
                     [--slope-blend 3] [--height-blend 1] [--opacity 1]    (by slope in degrees and height)
                 generate --terrain <file> [--kind fbm|ridged|island|plateau] [--seed 1] [--amplitude 24] [--base 0]
                     [--scale <world units>] [--octaves 6] [--lacunarity 2] [--gain 0.5] [--noise-kind gradient|value] [--warp 0]
                     [--island-radius 0.65] [--island-falloff 0.3] [--plateau-level 0.55] [--plateau-softness 0.04] [--add yes]
                     (heights from noise on every core; the same seed gives the same ground)
                 erode --terrain <file> [--mode hydraulic|thermal] [--iterations 150]
                     hydraulic: [--rain 0.0015] [--evaporation 0.03] [--strength 0.4]   thermal: [--talus 35] [--strength 0.5]
                 (generate, erode and presets run in the background of the command: [--threads <n>] caps the cores,
                 [--max-seconds <n>] stops them and saves nothing)
                 import --terrain <file> --source <heightmap.png|.raw|.r16> [--min -32] [--max 32] [--size 512[x256]] [--flip yes] [--replace]
                 export --terrain <file> --out <file.png|.raw|.r16> [--min <n> --max <n>]    (16 bit)
                 preview --terrain <file> --out <file.png> [--kind height|slope|splat] [--pixels 512]
                 (--terrain is /Game/Terrains/Hills.kbterrain, Assets/..., or a file path; --project is needed for /Game paths.)
)"
R"(  map2d        2D tile maps (World, 2D Map Editor plugin, on by default): what the editor's World 2D
               Map component and Map Editor window do - grounds with automatic borders,
               sprites as items or grounds, floors -50..50, a Map Camera for Play.
                 new --project <dir> --name <Entity> [--scene <path>] [--size 64x64]
                     [--orientation flat|wall] [--topology rectangular|isometric|hexagonal] [--view 15x11]   (map file, Mesh Renderer, Map Camera)
                 ground new --project <dir> --name Grass --texture "/Game/2D Map Sprites/terrain.png"
                     [--cell-size 32] [--z 30] [--tiles 25,26] [--weights 3,1] [--island 16] [--hole 19]
                     (--tiles lists sprites the ground picks between, --weights how often each is picked, one number per tile)
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
                 set --project <dir> --map <path> [--size WxH] [--orientation flat|wall] [--topology rectangular|isometric|hexagonal] [--view WxH]
                     [--camera-pixels-per-unit n] [--camera-follow <Name>|none] [--camera-smoothing seconds] [--camera-zoom n]
                     [--camera-dead-zone w,h] [--camera-clamp yes|no]   (the play camera, see docs/world_2d_camera.md)
                 view --project <dir> --entity <Name>   (update the Map Camera and painted collision)
                 info --project <dir> --map <path>
                 schema --project <dir> --map <path> --name <field> --type bool|int|float|string --default <value> [--rename-from <field>]
                 path --project <dir> --map <path> --from x,y --to x,y [--floor n] [--clearance 0..8]
                     [--diagonal none|safe|free] [--cost-property <name>] [--max-visited 1..1048576]
                     [--timeout-ms <milliseconds>] (default 25 ms; JSON; exits 0 found, 2 no path, 3 budget)
               Sprite sheets go into Assets/2D Map Sprites/<Category>; cells count row by row from 0.
  vfx-new      Create a particle effect (.kbvfx) as the editor's New Particle Effect.
                 --project <dir> --name <Name> [--folder Assets/Particles] [--set key=value ...]
  vfx-show     Print a particle effect: every key=value of its file.
                 --project <dir> --effect /Game/<path>.kbvfx
  particle-process-cook  Compile a particle simulation into a .kbpsim file.
                 --source <file.particle.sc> --output <file.kbpsim> [--profile all]
  render-effect-run  Run a native compute effect in a hidden renderer (Windows).
                 --manifest <effect.kbfx> [--frames 8]
  render-effect-export  Export a native compute effect and compiled shader variants.
                 --manifest <effect.kbfx> --output <directory>
  renderer-info  Print the graphics backend a hidden bgfx host selects as JSON: capabilities, limits,
                 formats and the reason for every quality fallback (Windows).
                 [--backend auto|d3d11|d3d12|vulkan|opengl] [--allow-fallback]
                 Exits 1 when an explicit backend is not the one selected, unless --allow-fallback.
  debug        Control a local Lua debugger endpoint (connect, breakpoints, step_into/over/out, continue, stack, locals, evaluate, disconnect).
                 --endpoint <dir> --command <command> [--expression <expression>] [--frame 0]
                 [--breakpoints <json-file>] [--timeout-ms 5000]
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
  vfx-emitter-add     Add an emitter to a particle effect.
                 --project <dir> --effect <path> [--name <Name>]
  vfx-emitter-remove  Remove an emitter of a particle effect by id.
                 --project <dir> --effect <path> --emitter <id>
  vfx-module-add      Add a module (Gravity, Wind, Drag, Noise, TorusSwirl, Separation, ...)
               to an emitter of a particle effect.
                 --project <dir> --effect <path> --emitter <id> --type <Type> [--target <id>]
  vfx-module-remove   Remove a module of a particle effect by id.
                 --project <dir> --effect <path> --module <id>
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
               do. Exits 1 when a build fails, or at once when this installation has no
               engine SDK or C++ toolchain: C++ scripts are for developers, use Lua.
                 --project <dir> [--script <path>]
  entity       Add, remove or show a scene entity (the project's startup scene unless --scene).
                 add --project <dir> [--scene <path>] --name <Name> [--parent <Name>]
                     [--create <Item>] [--position x,y,z] [--rotation x,y,z (degrees)] [--scale x,y,z]
                     [--color #RRGGBB (a new material named after it) | --material /Game/<path>.kbmat]
                     [--body Static|Dynamic|Kinematic (adds a Rigidbody)]
                     [--look-at x,y,z|<Entity> (turns forward, +Z, toward it; for cameras and lights)]
                     [--body2d Static|Kinematic|Dynamic (2D physics: a Body 2D and a Collider 2D that follows --scale)]
                     [--physics 2d|3d (the physics of --create 'Platformer Player' or 'Top Down Player'; 2d is the default)]
                     [--count <n> [--offset x,y,z] (n entities Name1..Name<n>, each offset further; default 0,1,0)]
                 (--create makes what the editor's Create menu makes: Cube, Sphere, Capsule,
                 Cylinder, Cone, Plane, Quad, Directional Light, Point Light, Spot Light,
                 Area Light, Camera, Environment, Particle System, Audio Source, Audio Listener;
                 --name then defaults to the item. UI widgets (Canvas, Text, Button, Slider, ...) and the screens
                 'Main Menu', 'Pause', 'HUD' and 'Settings' go under a Canvas with the bundled font; a screen's
                 buttons carry event names (PlayClicked, ResumeClicked, BackClicked, ...) for a script to answer.
                 --create 'Third Person Player' or 'First Person Player' adds a walking 3D
                 character named Player with its controller script, controls (WASD or arrows, Space
                 to jump) and the camera the game shows, behind it or through its eyes.
                 --create 'Platformer Player', 'Top Down Player' or 'Platformer Navigator'
                 makes a 2D player named Player with Sprite2D, CharacterController2D, a following
                 orthographic camera and Platformer2DBox2DPlayer.lua or TopDown2DBox2DPlayer.lua.
                 Existing scripts and input contexts are preserved; a new project gets Move/Jump
                 bindings. --physics 3d selects the legacy CharacterController and scripts.
                 see docs/world_2d_presets.md)
                 set --project <dir> [--scene <path>] --name <Name> [--position x,y,z]
                     [--rotation x,y,z] [--scale x,y,z] [--look-at x,y,z|<Entity>] [--parent <Name>|none]
                 remove --project <dir> [--scene <path>] --name <Name>
                 rename --project <dir> [--scene <path>] --name <Name> --to <NewName>
                        [--occurrence <n>]
                 attach --project <dir> [--scene <path>] --name <Object> --to <Character>
                        --socket <Socket|Bone> [--position x,y,z] [--rotation x,y,z] [--scale x,y,z]
                        (a sword in a hand: parents the object under the character and holds it
                        at the socket - or bone - every frame after animation; the offset is in
                        the socket's space, --scale is the object's own size)
                 detach --project <dir> [--scene <path>] --name <Object>
                 show --project <dir> [--scene <path>] --name <Object>
                     (its parent, children, transform and every component value, assets as paths)
                 (--name, --parent and --entity take a name or a path: Parent/Child;
                 names are unique among siblings, --occurrence picks one of several
                 same-named entities made elsewhere)
  component    Add, set or remove a component of an entity.
                 add|set|remove --project <dir> [--scene <path>] --entity <Name>
                     --type <Component> [--set name=value ...]
                 (vectors at once: --set localPosition=0,2,-8; Transform rotation in
                 degrees: --set rotation=35,30,0)
  schema       List the components, one component's properties, or everything.
                 [--component <Name> | --all]
  api          Generate the script API contract (Lua stubs, reference pages, JSON),
               or print one page of it: a group, function or component by --topic,
               the index with --topic index, or topics matching --search text.
                 --project <dir> | --out <dir> | --print markdown|json|lua
                 | --topic <name> | --search <text>
  guide        Print a short how-to for one kind of task (objects and materials,
               physics, scripts, characters, combat and AI, 2D maps, checking your
               work); without --topic, the list of guides.
                 [--topic <name>]
  code         Find your way in the engine's own source without reading all of it; read
               from the source tree when asked, so it is never out of date. modules
               prints its own JSON: start there.
                 modules [--topic <words>] [--limit <n>] [--check]
                                                          the code map: every module and
                                                          first-level subsystem with its
                                                          responsibility, entry points, include
                                                          root, build and test targets, ctest
                                                          pattern and dependencies; --topic
                                                          keeps the entries about those words;
                                                          --check fails when the map is stale
                 map  [--path <folder>] [--depth <1-8>]   folders, their size and purpose,
                                                          and the files of --path
                 find --text <words> [--path <folder>] [--limit <n>]
                                                          the files that deal with a topic,
                                                          each with the line to start at
                 show --symbol <Name>                     where a type or function is
                                                          declared, its comment, its
                                                          declaration, who uses it, its tests
                 [--engine-root <dir>]  (default: found from kb_cli or the current folder)
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
               (a model keeps its scale and rotation in its import;
               flags override the preset. --skeletal auto, the default,
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
)",
R"(  validate     Validate Lua behaviour scripts (syntax + sandbox load) and the UI of scenes: a widget
               outside a Canvas, a Text without a font, an event name or Button nothing answers (WARN lines).
               A script that calls a global, function, component or property that does not exist, or passes the wrong
               arguments, fails (FAIL lines, with the nearest real name); a function named like an event hook the
               engine never calls is a WARN line. kb_cli lsp shows the same findings.
                 [--project <dir>] <file.lua|scene.21kbscene> [more files...]
                 --project <dir>   (no files: every scene of the project)
  scene-list   List nodes, components, behaviours and world positions (@ x,y,z) of a scene.
                 [--project <dir>] --scene <path>
  scene-attach Attach a behaviour asset to a scene node and save the scene.
                 --project <dir> --scene <path> --node <name> --script <path>
                 [--tick-group <group>] [--execution-order <n>] [--disabled] [--replace]
                 [--set <variable>=<value> ...]
               (a node runs several scripts, up to 8, each with its own self: a second script is added after
               the first; the same script again changes its settings; --replace makes it the only one.
               --set gives this node its own value for an exposed variable of the Lua script, stored in the
               scene and in prefabs made from it; a value equal to the script default stores nothing, an
               unknown variable lists the ones the script exposes)
  scene-detach Take one script off a scene node and save the scene.
                 --project <dir> [--scene <path>] --node <name> [--script <path>]
               (--script names the one to remove; it can be left out when the node runs one script)
  input-action Create an input action and bind keys in the project's input context
               (made and set when the project has none), as the Input Actions window.
                 new --project <dir> --name <Name> [--type Bool|Float|Axis2D|Axis3D]
                     --key <Key> [--key <Key> ...]
                 list --project <dir>
               (keys: A..Z, Space, Enter, LeftShift, ArrowUp, MouseLeft, GamepadFaceBottom, ...;
               an axis key names its direction: --key W=+y --key S=-y --key D=+x --key A=-x;
               scripts read Input.Pressed("Fire"), Input.Value(...) or Input.Vector2("Move"))
  prefab       Save an entity as a prefab, or place one in a scene.
                 new --project <dir> [--scene <path>] --from-entity <Name> [--name <Prefab>]
                     [--folder Assets/Prefabs]
                 place --project <dir> [--scene <path>] --prefab /Game/<path>.kbprefab
                     [--name <Name>] [--parent <Name>] [--position x,y,z] [--rotation x,y,z]
                     [--scale x,y,z] [--look-at x,y,z|<Entity>]
               (scripts spawn copies with World.InstantiatePrefab)
)",
R"(  run          Run a scene headless and report logs, events, and errors (the
               project's startup scene unless --scene). Exits 1 on errors,
               124 on playback timeout, or 130 on cancellation. A script error (a failed assert, a call
               that does not exist) is an error; --json lists them with file, line, message and traceback.
                 --project <dir> [--scene <path>] [--frames <n>] [--dt <seconds>] [--realtime]
                 [--quiet] [--verbose (lists the active plugins)] [--report particles|audio|audio-meters|ecs|world-memory|input|ai|determinism|ui] [--pcm-frames <n>]
                 [--timeout-ms <1..86400000>] [--cancel-file <path>]
                 [--input-replay <recording>] [--input-record <recording>]
                 [--press <Key>@<frame>[+<frames>] ... (holds a key: --press Space@30, --press E@10+20)]
                 [--click <widget path|x,y>@<frame> ... (clicks a widget or a point of the 1280x720 view,
                  or --viewport: --click Menu/PlayButton@10; the pointer arrives the frame before,
                  presses on the frame, lets go on the next; the run says what was under it and who answered)]
                 [--viewport WIDTHxHEIGHT] [--safe-area LEFT,TOP,RIGHT,BOTTOM]
                 [--workers <1..64>] [--ecs-history <1..256> with --report ecs]
                 [--ecs-parallelism systems|chunks|combined] [--deterministic] [--fresh-saves]
               Save.Write and Save.Read (and Settings.*) keep their slots in the project's Saved/Saves, so a
               second run reads what the first wrote; --fresh-saves (and --deterministic) starts with none.
               Replay uses recorded per-tick durations and controller slots; --frames
               may select a prefix. --dt and audio probes cannot override replay timing.
               Recording captures input before each completed tick, including a partial
               run stopped by cancellation. Replay does not guarantee world determinism.
               Playback limits are cooperative between ticks and during paced waits;
               startup, an executing callback, and shutdown are not forcibly interrupted.
               Create the cancellation file to stop; the command does not remove it.
               --deterministic orders ECS/scene dispatch and makes fixed ticks independent
               of CPU duration; --report determinism exposes the active policy and scope.
               Content queries, RNG ownership and backend tolerances remain caller contracts.
               Reports are versioned JSON on stdout; logs go to stderr.
               Audio probes use explicit offline PCM rendering (up to 600 ticks
               and 1920000 total PCM frames). --pcm-frames overrides audio frames
               per tick; the default follows the sample rate and --dt.
               --report ai shows the last AI rule, condition, action, target and state.
               --report ui lists every laid-out widget (path, rectangle, text, event name), the scripts that answer
               its events, the --click results and every widget event delivered with the frame and the number of
               behaviours that answered (schema 21kb.ui-report/v1).
               --report world-memory states what the streamed worlds hold by kind (assets,
               instances, scripts, physics, graphics; each measured, estimated or
               unavailable) against what the streaming budget charged; see
               docs/world_memory.md.
)",
R"(  mcp          Serve the commands above as MCP tools over stdio (newline-
               delimited JSON-RPC), for use from MCP-compatible clients.
                 [--project <dir>] [--tools core|all] [--images on|off]
               A capture comes back as an image block under its text; --images off sends the text alone.
  lsp          Serve the Language Server Protocol over stdio for the Lua scripts of a
               project, so any code editor that speaks it gets diagnostics (syntax and
               constructs outside the language every platform runs), completion and hover
               from the script API, go to definition across Import/require, and the outline.
                 [--project <dir>]
  dap          Serve the Debug Adapter Protocol over stdio, so a code editor can debug the Lua
               scripts of a game started with --lua-debug-dir <new folder>: attach to that folder
               (arguments.endpoint), then breakpoints, continue and steps, stack, locals and
               expressions, as the live Lua debugger offers them.
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
  package      Packages of a project: folders of assets and scripts taken from a registry
               (a folder or an https address) and installed under Assets/Packages (docs/packages.md).
                 list|verify --project <dir>
                 search --project <dir> [--registry <folder|url>] [--cache <dir>] [<text>]
                 info --project <dir> [--registry <folder|url>] [--cache <dir>] <name>[@<range>]
                 add --project <dir> [--registry <folder|url>] [--cache <dir>] <name>[@<range>]
                 update --project <dir> [--registry <folder|url>] [--cache <dir>] [<name>]
                 remove --project <dir> <name>
                 trust --project <dir> [--add <public key>] [--remove <public key>]
                 publish --registry <dir> [--key <key file>]... <package folder>
                 sign --registry <dir> --key <key file> [--key <key file>]...
  release      Sign the manifest of a finished release directory, or verify one
               against the trust anchor of the player it ships.
                 sign --key <key file> --dir <dir> --product <id>
                      --content-version <version> --release <number>
                      [--anti-rollback]
                 verify [--anchor <file>] <dir>
  build        Cook a project, pack it and stage a folder that runs: Game.kbpack, the
               player kb_game, the engine module libraries the project enables and the
               project's own libraries (RuntimeModules). A relative --out is inside the project.
                 --project <dir> --out <new folder> [--pack-compression-level <0-19>]
                 [--cache <dir>] [--engine-root <dir>] [--shaderc <executable>]
               Needs kb_cooker and kb_game beside kb_cli. Sign the result with pack sign
               and release sign when it ships.
                 --project <dir> --platform android|web|linux [--out <folder for packages, outside the project>]
                 [--profile development|release] [--texture astc|etc2 (android)] [--renderer webgl|webgpu (web)]
                 [--build-root <engine build folder>] [--engine-root <engine folder>] [--python <executable>]
                 [--emsdk <folder> (web)] [--android-format apk|aab] [--android-keystore <file> --android-key-alias <alias>]
                 [--linux-host <host> --linux-user <user> --linux-host-key <key> --linux-identity <file> --linux-port <n>] [--verbose]
               --platform runs the editor's Build Game pipeline (scripts/package_game.py) without the editor, with the
               settings its panel saved for the project. It needs Python 3.10+, and for android the Android SDK with
               its NDK; for web the Emscripten SDK (--emsdk or EMSDK); for linux an SSH host to build on. CMake and
               a JDK 17+ are found by themselves (PATH, JAVA_HOME, the Android SDK, Android Studio, the usual install
               folders); what is missing is listed before anything starts. A release Android package reads the
               keystore passwords from KB_ANDROID_STORE_PASSWORD and KB_ANDROID_KEY_PASSWORD.
               Studio and game name come from the project settings (project-settings --set Product.Publisher=...).
  capture      Render a project or a build headless and write one frame as a PNG.
                 (--project <dir> | --build <folder made by build>) [--out <new file.png>]
                 (a relative --out is inside the --project folder; a project without --out
                 gets Saved/Agent/Captures/capture-<n>.png)
                 [--scene </Game/Path.21kbscene>] [--frames <1-120000>]
                 [--width <64-8192>] [--height <64-8192>]
                 [--camera <camera entity> | --view overview|top|front|side |
                  --eye <x,y,z> --look-at <x,y,z>]
               The picture is frame --frames (default 60) of a fixed 1/60 s step, so the
               same count gives the same picture. Needs kb_game beside kb_cli and a GPU.
               --camera draws from that camera of the scene, --view or --eye/--look-at from
               a free view like the editor's scene view (overview frames everything).
               Prints the picture's size, mean colour, dominant colours and whether the frame
               is blank. Through `kb_cli mcp` the picture itself comes back as an image.
  undo         The undo point of an agent's changes to the project's files, kept in
               Saved/Agent/Undo (the editor's Agent panel takes one before each request).
                 --project <dir> --begin [--label <text>]   record the project as it is
                 --project <dir> --list                     the files changed, added, deleted since
                 --project <dir> --last [--file <path>]...  put them back (all, or the named ones)
                 --project <dir> --accept                   keep the changes
               Folders of generated data (Saved, Library, Binaries, Intermediate, Build, Saves,
               .git, .kb) are not covered; a file over 48 MiB is listed but cannot be put back.
  world        Convert a single-file scene into a partitioned world (one file
               per object), or build a world's streaming cells.
                 migrate [--scene <scene>] --out <file.21kbworld>
                         [--cell-size <metres>] [--project <dir>]
                 build --world <file.21kbworld> [--project <dir>]
               (the scene defaults to the one the game starts in; scene and files may be /Game/... paths, with --project)
               build writes the cells, the cell index and the HLOD proxies,
               exactly as the editor and kb_cooker do.
                 chunks --project <dir> [--exclude <prefix>[,<prefix>...]]
               chunks prints one pack chunk rule per region of every built world.
               A world whose file enables navigation also gets one navigation
               mesh per cell from build.
  navmesh      Bake a scene's navigation mesh, or describe a baked one.
                 bake [--scene <scene>] [--project <dir>]
                      [--out <file.21kbnavmesh>] [--cell-size <metres>]
                      [--cell-height <metres>] [--tile-cells <n>]
                      [--agent <name>:<radius>:<height>:<climb>:<slope>]...
               bake writes <scene>.21kbnavmesh by default, exactly as the
               editor's Bake Navigation Mesh does; settings not given are
               those of the mesh it replaces, else the defaults. Without
               --scene it takes the scene the game starts in; a scene is
               /Game/Scenes/Main.21kbscene or a path in the project.
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
  project-settings  Show or change any project setting: the startup scene, the game's name,
               window, display, physics, input, Android and packaging values.
                 --project <dir> [--set Section.Key=value]...
               Without --set it lists every setting as Section.Key=value, e.g.
               Maps.DefaultMap (the scene the game starts in), Identity.GameName,
               Display.WindowWidth, Physics.GravityY. A scene is /Game/<path>.21kbscene.
                 scenes --project <dir> [--add <scene>]... [--remove <scene>]...
                        [--order <scene>,<scene>...] [--startup <scene>]
               scenes lists the scenes the built game holds, the startup scene first (only those
               are in a build, also for a scene a script or a button opens), and changes them:
               --order sets the whole list (the first one starts the game); a scene is its /Game/
               path or just its name (Level2). It also warns about a scene a script opens that is left out.
                 tags --project <dir> [--add <Name>]... [--remove <Name>]...
               tags lists the project's own tags for objects, besides the built-in ones, and changes them.
  asset        Tidy the project's files as Project Files does: delete (to the Recycle Bin), rename
               or move a file or folder with its .meta, or make a folder.
                 delete --project <dir> --path /Game/<file or folder>
                 rename --project <dir> --path /Game/<file or folder> --to <NewName>
                 move --project <dir> --path /Game/<file or folder> --to /Game/<folder>
                 new-folder --project <dir> --path /Game/<folder>
               Refused for a file another asset uses, the startup scene and the scene open in
               the editor (and the folders holding them).
)",
R"(  image        Draw a picture into the project and import it, so it is an asset at once: an icon,
               a UI background, a texture or a sprite sheet. No outside tool is needed.
                 new --project <dir> --name <Name> --preset <preset> [--style flat|soft|neon|pixel]
                     [--color #RRGGBB] [--color2 #RRGGBB] [--size 256] [--folder Assets/Images] [--replace]
               (presets: heart, star, coin, circle, ring, square, arrow, gem - shapes; button, panel -
               interface backgrounds; noise, ground, lowpoly - textures that tile; sky - a gradient that
               tiles sideways; glow - a soft dot; flame - a sprite sheet of 8 frames in a row (--size is
               one frame, up to 512). --color is the main colour, --color2 the
               second one (a gradient, a glow, a flame's core). The file is Assets/Images/<Name>.png
               and is used as /Game/Images/<Name>.png: imageAssetId of a UI Image or a Sprite2D)
  sound        Make a sound with the engine's own synthesiser and import it, so it is an asset
               at once. No outside tool is needed.
                 new --project <dir> --name <Name> --preset <preset> [--pitch <semitones>]
                     [--length <seconds>] [--folder Assets/Audio] [--replace]
               (presets: jump, coin (or pickup), hit, explosion, click, laser, powerup, footstep, wind
               and music, which loop. --pitch moves it up or down in semitones (12 is an octave), --length
               is its duration. The file is Assets/Audio/<Name>.wav, used as /Game/Audio/<Name>.wav:
               clipAssetId of an Audio Source, or Audio.Play in a script)
  timeline     A short cinematic (the engine's Timeline, .kbtimeline): a camera or object that moves
               over time, and events. `fly` makes the usual intro in one step.
                 fly --project <dir> --name <Name> --to <Object> [--entity "Main Camera"] [--seconds 3]
                     [--distance 5] [--height 2] [--scene <path>]
                 new --project <dir> --name <Name> [--duration 3] [--folder Assets/Cinematics] [--replace]
                 key --project <dir> --timeline <path> --entity <Object> --time <s> [--position x,y,z]
                     [--rotation x,y,z] [--look-at x,y,z|<Object>] [--scale x,y,z] [--scene <path>]
                 event --project <dir> --timeline <path> --time <s> --event <Name> [--entity <Object>]
                     [--payload <text>]
                 show --project <dir> --timeline <path>
                 play --project <dir> --timeline <path> [--loop yes] [--scene <path>]
               (fly: the object flies from where it is to a place --distance behind and --height above
               the --to object, looking at it, and plays when the scene starts. key: a position and
               rotation at a time (what is not given stays as it is); play: a Lua script on an empty
               object <Name>Player starts the timeline with the scene)
  material-function A reusable piece of material graph (.kbmatfn) that several materials call: a glowing
               edge, a scrolling or tiling texture coordinate. `use` adds the call to a material, gives
               every input a value and wires the output, in one step.
                 new --project <dir> --name <Name> --preset fresnel-glow|panner|tiling
                     [--folder Assets/Materials] [--replace]
                 show --project <dir> --function <path|Name>
                 use --project <dir> (--entity <Object> | --material /Game/<path>.kbmat) --function <path|Name>
                     [--set Input=value ...] [--to emissive|customizedUv0|<output>] [--output <Name>]
                     [--scene <path>]
               (fresnel-glow has the inputs Color, Power (thin edge = high) and Intensity and the output
               Glow, wired to Emissive; panner has Speed, tiling has Tiling, both give UV, wired to
               customizedUv0; a colour is #RRGGBB, a number is 3. The same function can be used on any
               number of materials; show lists the inputs with their defaults; --entity takes the material the
               object's Mesh Renderer uses)
  animation-clip A motion of an object over time as an animation clip (.kbanim): a coin that bobs and
               spins, a door that opens and closes, something that pulses. The presets do it in one
               step: the clip, a controller for it and an Animator on the object, playing from the start.
                 new --project <dir> --name <Name> [--preset bob|spin|open-close|pulse[,...]]
                     [--entity <Object>] [--seconds 2] [--amount <n>] [--loop yes|no] [--play no]
                     [--folder Assets/Animation] [--scene <path>] [--replace]
                 key --project <dir> --clip <path|Name> --time <s> [--entity <Object>] [--position x,y,z]
                     [--rotation x,y,z] [--scale x,y,z] [--target Child/Path]
                 show --project <dir> --clip <path|Name>
                 play --project <dir> --clip <path|Name> --entity <Object> [--scene <path>]
               (the preset's keys are relative to the object's own place; --amount is the size of one
               preset: bob height in units, spin or open degrees, pulse share; several presets joined
               with a comma are played together: bob,spin. key sets one moment by hand: what it does
               not say stays as the object or the key before has it; play gives the clip a controller
               and puts an Animator with it on --entity)
  pickup       A coin (or any object) the player collects by touching it, with a counter on the screen,
               in one step: the object with a trigger Collider, a Canvas with a Text, their two Lua
               scripts, the attachment and a validate of both; it takes effect completely or not at all.
                 new --project <dir> [--name Coin] [--count 1] [--position x,y,z] [--player Player] [--label Coins]
                     [--with-player yes] [--scene <path>]
               (--player is the name of the object whose touch collects it, as kb_cli scene-list prints it;
               --with-player yes adds the walking Third Person Player first when the scene has none;
               --count makes that many (Coin, Coin2, ... in a row, 1.2 units apart along Z) sharing the one script;
               --label is the word before the count on the screen)
  ui-theme     The look of the user interface as a shared UI Theme (.kbtheme): colours, text size,
               rounded corners. A theme has a base style and a Button variant.
                 new --project <dir> --name <Name> [--preset dark|light] [--folder Assets/UI] [--replace]
                     [tokens]
                 set --project <dir> --theme <path> [tokens]
                 show --project <dir> --theme <path>
                 apply --project <dir> --theme <path> [--scene <path>] [--entity <Object>]
               (tokens: --background #RRGGBB, --text #RRGGBB, --border #RRGGBB, --border-width <px>,
               --corner-radius <px>, --font-size <px>, --button #RRGGBB and --button-text #RRGGBB for
               the buttons. apply gives the theme to every control of the scene, or of --entity and
               what is below it)
  localization The texts of the game in several languages, as one catalog (Assets/Localization/Game.kbloc)
               that Text controls read through their localizationKey.
                 collect --project <dir> [--language en] [--scene <path>] [--catalog <path>]
                 set --project <dir> --language <code> --set key=text [--set key=text ...] [--catalog <path>]
                 add-language --project <dir> --language <code> [--catalog <path>]
                 show --project <dir> [--catalog <path>]
                 missing --project <dir> [--catalog <path>]
                 default --project <dir> --language <code> [--catalog <path>]
               (collect: the texts written on the controls of the scene become keys of the default
               language, and the game loads the catalog when the scene starts. add-language starts a
               language with the default language's texts; missing lists what is not translated yet;
               default is the language the game starts in and a missing text falls back to)
  editor       Ask the editor that has the project open to open a scene, to build the game
               for a platform with its Build Game panel (its settings, keys and tools), to start or
               stop the game, to take a picture of its view or to measure its frame times. Fails
               when no editor takes the request within a few seconds, and says what to do without
               one (build --platform and capture need no editor).
                 open-scene --project <dir> [--scene /Game/<path>.21kbscene]
                 build --project <dir> --target windows|android|web|webgpu|linux
                       [--profile development|release]
                 play|stop --project <dir>
                 capture --project <dir> [--out <file.png>]
                 frames --project <dir> [--seconds <1-30>]
               capture writes the editor's scene view (Saved/Agent/Captures/editor-view.png by
               default); frames reports the editor's frames per second and the slowest frames over
               the seconds, and says whether the game was playing (start it with play first).
               A build reports in Saved/Agent/BuildResult.txt: Running, then Succeeded <folder> or
               Failed <reason>.
  plugins      List the engine's plugins with whether the project has each on, or switch them,
               as the editor's Plugins window does.
                 --project <dir> [--enable <id>]... [--disable <id>]...
               Each is a game plugin, part of the packaged game, or an editor plugin, an
               authoring tool the packaged game leaves out.

Global options:
  --json       Print one JSON object instead of text, for scripts and agents:
                 {"schema":"21kb.cli-result/v1","ok":true,"exitCode":0,
                  "command":"entity","output":"<stdout>","errors":["<stderr line>",...]}
               "ok" is true when the exit code is 0; "errors" lists what the command
               wrote to stderr. A command that prints its own JSON (audio-mixer,
               run --report, api --print json, batch) keeps it unchanged. mcp, lsp and dap ignore --json.
               Native compiler logs go to process stderr and never precede the JSON object.
  --dry-run    Only for the commands that name it (audio-mixer new/set).
  --command-timeout-ms <1..86400000>  Bound the entire command on Windows.
  --command-cancel-file <path>       Stop when this caller-owned file exists.
               Put these options before the command (optionally after --json).
               The supervisor stops its command and descendants, including external
               build tools. Text output streams; interrupted JSON is one result
               with reason timeout|cancelled|supervisor-error. JSON capture is
               limited to 16 MiB. Interruption does not roll back completed writes.
               Example: kb_cli --json --command-timeout-ms 30000 script-build --project .

Exit codes: normally 0 on success and 1 on failure (bad arguments, an unknown
command, a failed check or build, or inaccessible files). Commands may document
additional codes; run returns 124 on timeout and 130 on cancellation.
Command supervision returns 124 on timeout, 130 on cancellation and 125 on a
supervisor error; otherwise it preserves the child command's exit code.
help, --help, -h and no command at all print this text and exit 0.

Scene paths may be physical (relative to the project root) or virtual
("/Game/Scenes/Main.21kbscene", requires --project).
)"
};

// What `kb_cli help` prints: one line per command. The options of a command are `kb_cli help <command>`, so a first look
// costs a few KB instead of the whole manual.
void PrintUsage(std::ostream& output) {
    output << "kb_cli - 21kb Engine headless tooling for developers and AI coding agents\n\n"
              "Usage: kb_cli <command> [options]     kb_cli help <command> prints that command's options\n\n"
              "Commands:\n"
           << CliCommandSummary()
           << "\nkb_cli help options explains the global options (--json, timeouts) and the exit codes.\n";
}

void PrintCommandUsage(std::ostream& output, std::string_view command) {
    if (command == "options") {
        const std::string_view tail = kUsageParts.back();
        output << tail.substr(tail.find("Global options:"));
        return;
    }
    const std::string block = CliCommandUsageBlock(command);
    if (block.empty()) {
        const std::vector<std::string> names = CliUsageCommandNames();
        const std::string closest = kb::script::ClosestScriptName(command, std::span<const std::string>{ names });
        output << "kb_cli has no command '" << command << '\'' << (closest.empty() ? std::string{} : "; did you mean " + closest + "?") << " (kb_cli help lists them)\n";
        return;
    }
    output << block;
}

// A misspelled option must fail, not be ignored: for these commands the options accepted are the
// ones their usage block names, plus the global ones. The other commands check their own.
constexpr std::string_view kUsageCheckedCommands[]{ "entity", "component", "schema", "guide", "code", "api", "validate", "scene-list",
    "scene-attach", "scene-detach", "scene-new", "scene-duplicate", "run", "material-new", "material-set", "material-show", "input-action", "prefab", "build", "capture",
    "package" };

bool RejectUnknownOptions(std::string_view command, const ArgumentList& arguments, std::ostream& err) {
    if (std::ranges::find(kUsageCheckedCommands, command) == std::end(kUsageCheckedCommands)) return false;
    const std::string block = CliCommandUsageBlock(command);
    if (block.empty()) return false;
    std::vector<std::string> known{ "--json", "--dry-run", "--project", "--help", "--command-timeout-ms", "--command-cancel-file" };
    for (std::size_t at = block.find("--"); at != std::string::npos; at = block.find("--", at + 2U)) {
        std::size_t end = at + 2U;
        while (end < block.size() && (std::isalnum(static_cast<unsigned char>(block[end])) != 0 || block[end] == '-')) ++end;
        if (end > at + 2U) known.emplace_back(block.substr(at, end - at));
    }
    const std::vector<std::string_view> allowed{ known.begin(), known.end() };
    const std::vector<std::string> unknown = arguments.UnknownOptions(allowed);
    if (unknown.empty()) return false;
    const std::string& wrong = unknown.front();
    const std::string closest = kb::script::ClosestScriptName(wrong, std::span<const std::string>{ known });
    err << "error: " << command << " has no option '" << wrong << '\'';
    if (!closest.empty()) err << "; did you mean " << closest << '?';
    err << " (kb_cli help " << command << " lists its options)\n";
    return true;
}

constexpr std::array<std::string_view, 25> kFlagNames{
    "--full", "--verbose", "--anti-rollback", "--disabled", "--quiet", "--update-baseline", "--require-gpu", "--allow-fallback", "--as-ground", "--no-borders", "--items",
    "--diff", "--list-backups", "--allow-unsigned", "--deny-unsigned", "--replace", "--realtime", "--deterministic", "--all", "--check",
    "--begin", "--list", "--last", "--accept", "--fresh-saves" };

#if KB_CLI_RENDERER
constexpr bool kRendererAvailable = true;
#else
constexpr bool kRendererAvailable = false;
#endif

constexpr CliCommandDescriptor kCommands[]{
    {"debug", &RunLuaDebugCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Native},
    {"animator", &RunAnimatorCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"blend-space", &RunBlendSpace2DCommand, CliCommandKind::Normal, true, "blend_space", CliJsonOutput::Native},
    {"graph", &RunVisualGraphCommand, CliCommandKind::Normal, true, "visual_graph", CliJsonOutput::Native},
    {"skeleton", &RunSkeletonCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"ability", &RunAbilityCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"ai", &RunAiCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"api", &RunApiCommand, CliCommandKind::Normal, true, "api_reference", CliJsonOutput::Structured, true},
    {"guide", &RunGuideCommand, CliCommandKind::Normal, true, "guide", CliJsonOutput::Structured},
    {"code", &RunCodeCommand, CliCommandKind::Normal, true, "code", CliJsonOutput::Structured},
    {"api-check", &RunApiCheckCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"entity", &RunEntityCommand, CliCommandKind::Normal, true, "entity_edit", CliJsonOutput::Structured, true},
    {"component", &RunComponentCommand, CliCommandKind::Normal, true, "component_edit", CliJsonOutput::Structured, true},
    {"audio-mixer", &RunAudioMixerCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Native},
    {"schema", &RunSchemaCommand, CliCommandKind::Normal, true, "component_schema", CliJsonOutput::Structured},
    {"new", &RunNewCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"init-agent", &RunInitAgentCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"import", &RunImportCommand, CliCommandKind::Normal, true, "import_asset", CliJsonOutput::Structured, true},
    {"import-preset-new", &RunImportPresetNewCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"validate", &RunValidateCommand, CliCommandKind::Normal, true, "validate_script", CliJsonOutput::Structured},
    {"scene-list", &RunSceneListCommand, CliCommandKind::Normal, true, "scene_list", CliJsonOutput::Structured},
    {"scene-attach", &RunSceneAttachCommand, CliCommandKind::Normal, true, "scene_attach", CliJsonOutput::Structured, true},
    {"scene-detach", &RunSceneDetachCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"input-action", &RunInputActionCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"prefab", &RunPrefabCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"layout", &RunLayoutCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"scene-new", &RunSceneNewCommand, CliCommandKind::Normal, true, "scene_create", CliJsonOutput::Structured, true},
    {"scene-duplicate", &RunSceneDuplicateCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"script-new", &RunScriptNewCommand, CliCommandKind::Normal, true, "script_create", CliJsonOutput::Structured, true},
    {"script-build", &RunScriptBuildCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"run", &RunRunCommand, CliCommandKind::Normal, true, "run_scene", CliJsonOutput::Partial},
    {"keys", &RunKeysCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"pack", &RunPackCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"mod", &RunModCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"package", &RunPackageCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"release", &RunReleaseCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"build", &RunBuildCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"capture", &RunCaptureCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"undo", &RunUndoCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"world", &RunWorldCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"navmesh", &RunNavMeshCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"physics-layers", &RunPhysicsLayersCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"lighting-quality", &RunLightingQualityCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"project-settings", &RunProjectSettingsCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"plugins", &RunPluginsCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"editor", &RunEditorCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"asset", &RunAssetCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"sound", &RunSoundCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"timeline", &RunTimelineCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Envelope},
    {"animation-clip", &RunAnimationClipCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Envelope},
    {"ui-theme", &RunUiThemeCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Envelope},
    {"pickup", &RunPickupCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Envelope},
    {"localization", &RunLocalizationCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Envelope},
#if KB_CLI_RENDERER
    {"image", &RunImageCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Structured, true},
    {"material-function", &RunMaterialFunctionCommand, CliCommandKind::Normal, true, "", CliJsonOutput::Envelope},
    {"csg", &RunCsgCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"rig2d", &RunRig2DCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"vector2d-new", &RunVectorShapeNewCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"vector2d-show", &RunVectorShapeShowCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured},
    {"vector2d-set", &RunVectorShapeSetCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"map2d", &RunMap2DCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"terrain", &RunTerrainCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"mesh-lod", &RunMeshLodCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"scene-lightmap", &RunSceneLightmapCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"scene-bake-lighting", &RunSceneBakeLightingCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"material-new", &RunMaterialNewCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"material-set", &RunMaterialSetCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"material-show", &RunMaterialShowCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured},
    {"material-graph", &RunMaterialGraphCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"material-instance-new", &RunMaterialInstanceNewCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"material-instance-set", &RunMaterialInstanceSetCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"material-instance-show", &RunMaterialInstanceShowCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured},
    {"post-process-new", &RunPostProcessNewCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"post-process-set", &RunPostProcessSetCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"scene-post-process", &RunScenePostProcessCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"particle-process-cook", &RunParticleProcessCookCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"render-effect-run", &RunRenderEffectRunCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"render-effect-export", &RunRenderEffectExportCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Structured, true},
    {"renderer-info", &RunRendererInfoCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Native},
    {"vfx-profile", &RunVfxProfileCommand, CliCommandKind::Normal, true, {}, CliJsonOutput::Native},
#else
    {"image", nullptr, CliCommandKind::Normal, false, "", CliJsonOutput::Structured, true},
    {"material-function", nullptr, CliCommandKind::Normal, false, "", CliJsonOutput::Envelope},
    {"csg", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"rig2d", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"vector2d-new", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"vector2d-show", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured},
    {"vector2d-set", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"map2d", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"terrain", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"mesh-lod", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"scene-lightmap", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"scene-bake-lighting", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"material-new", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"material-set", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"material-show", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured},
    {"material-graph", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"material-instance-new", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"material-instance-set", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"material-instance-show", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured},
    {"post-process-new", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"post-process-set", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"scene-post-process", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"particle-process-cook", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"render-effect-run", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"render-effect-export", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Structured, true},
    {"renderer-info", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Native},
    {"vfx-profile", nullptr, CliCommandKind::Normal, false, {}, CliJsonOutput::Native},
#endif
    {"vfx-new", nullptr, CliCommandKind::Particle, kRendererAvailable, {}, CliJsonOutput::Structured, true},
    {"vfx-show", nullptr, CliCommandKind::Particle, kRendererAvailable, {}, CliJsonOutput::Structured},
    {"vfx-set", nullptr, CliCommandKind::Particle, kRendererAvailable, {}, CliJsonOutput::Structured, true},
    {"vfx-emitter-add", nullptr, CliCommandKind::Particle, kRendererAvailable, {}, CliJsonOutput::Structured, true},
    {"vfx-emitter-remove", nullptr, CliCommandKind::Particle, kRendererAvailable, {}, CliJsonOutput::Structured, true},
    {"vfx-module-add", nullptr, CliCommandKind::Particle, kRendererAvailable, {}, CliJsonOutput::Structured, true},
    {"vfx-module-remove", nullptr, CliCommandKind::Particle, kRendererAvailable, {}, CliJsonOutput::Structured, true},
    {"batch", nullptr, CliCommandKind::Batch, true, {}, CliJsonOutput::Native},
    {"mcp", nullptr, CliCommandKind::Mcp, true, {}, CliJsonOutput::Protocol},
    {"lsp", nullptr, CliCommandKind::Lsp, true, {}, CliJsonOutput::Protocol},
    {"dap", nullptr, CliCommandKind::Dap, true, {}, CliJsonOutput::Protocol},
    {"help", nullptr, CliCommandKind::Help, true, {}},
    {"commands", nullptr, CliCommandKind::Catalog, true, {}, CliJsonOutput::Native},
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
    case CliCommandKind::Lsp: return RunLspCommand(arguments, in, io);
    case CliCommandKind::Dap: return RunDapCommand(arguments, in, io);
    case CliCommandKind::Help:
        if (!arguments.Positionals().empty()) PrintCommandUsage(io.out, arguments.Positionals().front());
        else PrintUsage(io.out);
        return 0;
    case CliCommandKind::Catalog:
        if (arguments.Flag("--full")) io.out << CliCatalogJson() << '\n';
        else if (const std::optional<std::string> one = arguments.Option("--command"); one.has_value()) {
            const std::string json = CliCommandJson(*one);
            if (json.empty()) {
                io.err << "error: kb_cli has no command '" << *one << "' (kb_cli help lists them)\n";
                return 1;
            }
            io.out << json << '\n';
        } else io.out << CliCatalogShortJson() << '\n';
        return 0;
    }
    return std::nullopt;
}

} // namespace

std::span<const CliCommandDescriptor> CliCommandCatalog() noexcept { return kCommands; }

std::string CliUsageText() {
    std::string usage;
    for (const auto part : kUsageParts) usage += part;
    return usage;
}

const CliCommandDescriptor* FindCliCommand(std::string_view name) noexcept {
    for (const auto& command : CliCommandCatalog())
        if (command.name == name) return &command;
    return nullptr;
}

bool IsProtocolCommand(std::string_view name) noexcept {
    const CliCommandDescriptor* command = FindCliCommand(name);
    return command != nullptr && command->json == CliJsonOutput::Protocol;
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
int RunAsJson(std::string_view command, CommandIo io, const ChangeWatch* watch, const std::function<int(CommandIo)>& run) {
    std::ostringstream out;
    std::ostringstream err;
    int exitCode = 1;
    kb::core::JsonValue data;
    try {
        exitCode = run(CommandIo{ .out = out, .err = err, .data = &data });
    } catch (const std::exception& exception) {
        err << "error: " << exception.what() << '\n';
    } catch (...) {
        err << "error: unexpected failure\n";
    }

    kb::core::JsonValue own;
    std::string parseError;
    const bool printedObject = data.GetKind() != kb::core::JsonValue::Kind::Object && kb::core::JsonValue::Parse(out.str(), own, parseError) &&
        own.GetKind() == kb::core::JsonValue::Kind::Object;
    // A command's own JSON object stays as it is, unless the command changed files: then they are listed beside it.
    const bool listed = watch != nullptr && watch->Publish(data, printedObject);
    if (printedObject && !listed) {
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
    if (data.GetKind() == JsonValue::Kind::Object) result.Set("data", std::move(data));
    io.out << result.Dump() << '\n';
    return exitCode;
}

} // namespace

namespace {

std::filesystem::path g_confinedRoot;

[[nodiscard]] bool LooksLikePath(std::string_view text) noexcept {
    return text.find('/') != std::string_view::npos || text.find('\\') != std::string_view::npos ||
        text.find("..") != std::string_view::npos || (text.size() >= 2U && text[1] == ':');
}

// Why `words` (a command and its arguments) may not run in a confined process: the first path in them that
// leads outside the project, or an empty string.
[[nodiscard]] std::string ConfinementRefusal(const std::vector<std::string>& words) {
    if (g_confinedRoot.empty() || words.empty()) return {};
    const std::string& command = words.front();
    // Commands that only read or describe: the engine's code, the guides, the usage text.
    if (command == "code" || command == "guide" || command == "help" || command == "commands") return {};
    std::error_code error;
    const std::filesystem::path root = std::filesystem::weakly_canonical(g_confinedRoot, error);
    const auto inside = [&root](const std::filesystem::path& candidate) {
        std::error_code resolveError;
        // Links and junctions are followed, so a link inside the project cannot lead out of it.
        const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, resolveError);
        if (resolveError) return false;
        const std::filesystem::path relative = resolved.lexically_relative(root);
        return !relative.empty() && *relative.begin() != "..";
    };
    std::string option;
    for (std::size_t index = 1U; index < words.size(); ++index) {
        const std::string& word = words[index];
        if (word.starts_with("--")) {
            option = word;
            continue;
        }
        const std::string previous = option;
        option.clear();
        // Reading a file to bring into the project, and the engine and tools the build uses.
        if (previous == "--source" || previous == "--engine-root" || previous == "--shaderc" ||
            (command == "import" && previous.empty())) {
            continue;
        }
        // A value may be a setting=value pair whose value is the path.
        std::string text = word;
        if (const std::size_t equals = text.find('='); equals != std::string::npos && !LooksLikePath(text.substr(0U, equals))) {
            text = text.substr(equals + 1U);
        }
        if (!LooksLikePath(text)) continue;
        // A project path (/Game/...) lives in the project's Assets, unless it climbs out with ..
        if ((text == "/Game" || text.starts_with("/Game/")) && text.find("..") == std::string::npos) continue;
        std::filesystem::path path{ std::u8string{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
        if (path.is_relative()) path = g_confinedRoot / path;
        if (!inside(path)) {
            return "refused for safety: '" + word + "' is outside the project. Only files and folders inside the project " +
                root.string() + " may be created, changed or deleted.";
        }
    }
    return {};
}

} // namespace

void ConfineCliToProject(const std::filesystem::path& projectRoot) {
    if (g_confinedRoot.empty() && !projectRoot.empty()) {
        g_confinedRoot = std::filesystem::absolute(projectRoot);
    }
}

void ResetCliConfinementForTests() {
    g_confinedRoot.clear();
}

int RunCli(std::span<const std::string> args, std::istream& in, CommandIo io) {
    if (args.empty()) {
        PrintUsage(io.out);
        return 0;
    }
    if (const std::string refused = ConfinementRefusal(std::vector<std::string>{ args.begin(), args.end() }); !refused.empty()) {
        io.err << "error: " << refused << '\n';
        return 1;
    }

    // --json may come before the command: kb_cli --json entity add ...
    std::vector<std::string> words{ args.begin(), args.end() };
    if (words.size() > 1U && words.front() == "--json") {
        words.erase(words.begin());
        words.emplace_back("--json");
    }
    // kb_cli <command> --help is kb_cli help <command>.
    if (words.size() > 1U && FindCliCommand(words.front()) != nullptr &&
        std::ranges::any_of(words.begin() + 1, words.end(), [](const std::string& word) { return word == "--help" || word == "-h"; })) {
        PrintCommandUsage(io.out, words.front());
        return 0;
    }
    const std::string_view command{ words.front() };
    const std::vector<std::string> rest{ words.begin() + 1, words.end() };
    std::vector<std::string_view> flagNames{kFlagNames.begin(), kFlagNames.end()};
    flagNames.insert(flagNames.end(), {"--json", "--dry-run"});
    ArgumentList arguments{ rest, flagNames };
    const bool json = arguments.Flag("--json") && !IsProtocolCommand(command);

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
        if (RejectUnknownOptions(command, arguments, target.err)) return 1;
        // The first command after a cut-off transaction puts the project back (a batch checks for itself).
        if (const std::optional<std::string> project = arguments.Option("--project"); project.has_value() && command != "batch") {
            static_cast<void>(ResolveInterruptedTransaction(*project, target.err));
        }
        if (const std::optional<int> code = RunNamedCommand(command, arguments, in, target)) {
            return *code;
        }
        // One line, not the whole usage text: an agent that guessed a name reads it on every mistake.
        const std::vector<std::string> names = CliUsageCommandNames();
        const std::string closest = kb::script::ClosestScriptName(command, std::span<const std::string>{ names });
        target.err << "error: unknown command '" << command << '\'' << (closest.empty() ? std::string{} : "; did you mean " + closest + "?")
                   << " (kb_cli help lists the commands, kb_cli help <command> shows one)\n";
        static_cast<void>(json);
        return 1;
    };

    if (!json) return execute(io);
    // The files a writing command changes are listed from a look at the disk before it runs and after.
    const CliCommandDescriptor* descriptor = FindCliCommand(command);
    std::optional<ChangeWatch> watch;
    if (descriptor != nullptr && descriptor->writes && arguments.Errors().empty()) watch.emplace(command, arguments);
    return RunAsJson(command, io, watch ? &*watch : nullptr, execute);
}

} // namespace kb::cli
