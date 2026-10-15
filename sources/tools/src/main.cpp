#include "CliCommands.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr std::string_view kUsage = R"(kb_cli — 21kb Engine headless tooling for developers and AI coding agents

Usage: kb_cli <command> [options]

Commands:
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
  vfx-new      Create a particle effect (.kbvfx) as the editor's New Particle Effect.
                 --project <dir> --name <Name> [--folder Assets/Particles] [--set key=value ...]
  vfx-show     Print a particle effect: every key=value of its file.
                 --project <dir> --effect /Game/<path>.kbvfx
  vfx-set      Change values of a particle effect (keys as vfx-show prints them, e.g.
               effect.emitter[0].spawn.shape.type=Sphere).
                 --project <dir> --effect <path> --set key=value [...]
  vfx-emitter-add / vfx-emitter-remove   Add an emitter / remove one by id.
                 --project <dir> --effect <path> [--name <Name>] | --emitter <id>
  vfx-module-add / vfx-module-remove     Add a module (Gravity, Wind, Drag, Noise,
               TorusSwirl, Separation, ...) to an emitter / remove one by id.
                 --project <dir> --effect <path> --emitter <id> --type <Type> [--target <id>]
                 | --module <id>
  script-build Build the project's C++ scripts (or one), as Play and the cooker
               do. Exits 1 when a build fails.
                 --project <dir> [--script <path>]
  entity       Add or remove a scene entity (the project's startup scene unless --scene).
                 add --project <dir> [--scene <path>] --name <Name> [--parent <Name>]
                     [--create <Item>] [--position x,y,z] [--rotation x,y,z (degrees)] [--scale x,y,z]
                 (--create makes what the editor's Create menu makes: Cube, Sphere, Capsule,
                 Cylinder, Cone, Plane, Quad, Directional Light, Point Light, Spot Light,
                 Area Light, Camera, Environment, Particle System, Audio Source, Audio Listener;
                 --name then defaults to the item)
                 remove --project <dir> [--scene <path>] --name <Name>
                 rename --project <dir> [--scene <path>] --name <Name> --to <NewName>
                        [--occurrence <n>]
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
                 [--import-textures yes|no]
               (a model keeps its scale and rotation in its import, as Unity's
               Scale Factor; flags override the preset)
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
  run          Run a scene headless and report logs, events, and errors (the
               project's startup scene unless --scene). Exits 1 on any error.
                 --project <dir> [--scene <path>] [--frames <n>] [--dt <seconds>]
                 [--quiet]
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

Scene paths may be physical (relative to the project root) or virtual
("/Game/Scenes/Main.21kbscene", requires --project).
)";

constexpr std::array<std::string_view, 4> kFlagNames{ "--anti-rollback", "--disabled", "--quiet", "--update-baseline" };

} // namespace

int Dispatch(int argc, char** argv);

// kb_cli talks UTF-8: arguments arrive in UTF-8 (kb_cli.manifest sets the process code page on
// Windows; Linux is UTF-8 already) and names with any letters are printed as such. The console's
// own code page is restored on exit.
class Utf8Console final {
public:
    Utf8Console() {
#if defined(_WIN32)
        previous_ = GetConsoleOutputCP();
        if (previous_ != 0U) {
            static_cast<void>(SetConsoleOutputCP(CP_UTF8));
        }
#endif
    }
    ~Utf8Console() {
#if defined(_WIN32)
        if (previous_ != 0U) {
            static_cast<void>(SetConsoleOutputCP(previous_));
        }
#endif
    }
    Utf8Console(const Utf8Console&) = delete;
    Utf8Console& operator=(const Utf8Console&) = delete;

private:
    unsigned int previous_ = 0U;
};

int main(int argc, char** argv) {
    const Utf8Console console;
    try {
        return Dispatch(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "error: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "error: unexpected failure\n";
    }
    return 1;
}

int Dispatch(int argc, char** argv) {
    if (argc < 2) {
        std::cout << kUsage;
        return 0;
    }

    const std::string_view command{ argv[1] };
    std::vector<std::string> rest;
    rest.reserve(static_cast<std::size_t>(argc));
    for (int index = 2; index < argc; ++index) {
        rest.emplace_back(argv[index]);
    }

    const kb::cli::ArgumentList arguments{ rest, kFlagNames };
    if (!arguments.Errors().empty()) {
        for (const std::string& error : arguments.Errors()) {
            std::cerr << "error: " << error << '\n';
        }
        return 1;
    }
    const kb::cli::CommandIo io{ .out = std::cout, .err = std::cerr };

    if (command == "api") {
        return kb::cli::RunApiCommand(arguments, io);
    }
    if (command == "api-check") {
        return kb::cli::RunApiCheckCommand(arguments, io);
    }
    if (command == "entity") {
        return kb::cli::RunEntityCommand(arguments, io);
    }
    if (command == "component") {
        return kb::cli::RunComponentCommand(arguments, io);
    }
    if (command == "schema") {
        return kb::cli::RunSchemaCommand(arguments, io);
    }
    if (command == "new") {
        return kb::cli::RunNewCommand(arguments, io);
    }
    if (command == "init-agent") {
        return kb::cli::RunInitAgentCommand(arguments, io);
    }
    if (command == "import") {
        return kb::cli::RunImportCommand(arguments, io);
    }
    if (command == "import-preset-new") {
        return kb::cli::RunImportPresetNewCommand(arguments, io);
    }
    if (command == "validate") {
        return kb::cli::RunValidateCommand(arguments, io);
    }
    if (command == "scene-list") {
        return kb::cli::RunSceneListCommand(arguments, io);
    }
    if (command == "scene-attach") {
        return kb::cli::RunSceneAttachCommand(arguments, io);
    }
    if (command == "scene-new") {
        return kb::cli::RunSceneNewCommand(arguments, io);
    }
    if (command == "script-new") {
        return kb::cli::RunScriptNewCommand(arguments, io);
    }
    if (command == "material-new" || command == "material-set" || command == "material-show" ||
        command == "material-graph" || command == "material-instance-new" || command == "material-instance-set" ||
        command == "material-instance-show") {
#if KB_CLI_RENDERER
        if (command == "material-new") return kb::cli::RunMaterialNewCommand(arguments, io);
        if (command == "material-set") return kb::cli::RunMaterialSetCommand(arguments, io);
        if (command == "material-graph") return kb::cli::RunMaterialGraphCommand(arguments, io);
        if (command == "material-instance-new") return kb::cli::RunMaterialInstanceNewCommand(arguments, io);
        if (command == "material-instance-set") return kb::cli::RunMaterialInstanceSetCommand(arguments, io);
        if (command == "material-instance-show") return kb::cli::RunMaterialInstanceShowCommand(arguments, io);
        return kb::cli::RunMaterialShowCommand(arguments, io);
#else
        std::cerr << "error: " << command << " needs the renderer; this kb_cli was built with KB_BUILD_RENDERER=OFF\n";
        return 1;
#endif
    }
    if (command == "post-process-new" || command == "post-process-set" || command == "scene-post-process") {
#if KB_CLI_RENDERER
        if (command == "post-process-new") return kb::cli::RunPostProcessNewCommand(arguments, io);
        if (command == "post-process-set") return kb::cli::RunPostProcessSetCommand(arguments, io);
        return kb::cli::RunScenePostProcessCommand(arguments, io);
#else
        std::cerr << "error: " << command << " needs the renderer; this kb_cli was built with KB_BUILD_RENDERER=OFF\n";
        return 1;
#endif
    }
    if (command.starts_with("vfx-")) {
#if KB_CLI_RENDERER
        return kb::cli::RunVfxCommand(command, arguments, io);
#else
        std::cerr << "error: " << command << " needs the renderer; this kb_cli was built with KB_BUILD_RENDERER=OFF
";
        return 1;
#endif
    }
    if (command == "script-build") {
        return kb::cli::RunScriptBuildCommand(arguments, io);
    }
    if (command == "run") {
        return kb::cli::RunRunCommand(arguments, io);
    }
    if (command == "mcp") {
        return kb::cli::RunMcpCommand(arguments, std::cin, io);
    }
    if (command == "keys") {
        return kb::cli::RunKeysCommand(arguments, io);
    }
    if (command == "pack") {
        return kb::cli::RunPackCommand(arguments, io);
    }
    if (command == "release") {
        return kb::cli::RunReleaseCommand(arguments, io);
    }
    if (command == "world") {
        return kb::cli::RunWorldCommand(arguments, io);
    }
    if (command == "navmesh") {
        return kb::cli::RunNavMeshCommand(arguments, io);
    }
    if (command == "physics-layers") {
        return kb::cli::RunPhysicsLayersCommand(arguments, io);
    }
    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << kUsage;
        return 0;
    }

    std::cerr << "error: unknown command '" << command << "'\n\n" << kUsage;
    return 1;
}
