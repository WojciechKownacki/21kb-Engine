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
               format (.21kb plus metadata).
                 --project <dir> --destination /Game/<folder> <file> [more files...]
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
    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << kUsage;
        return 0;
    }

    std::cerr << "error: unknown command '" << command << "'\n\n" << kUsage;
    return 1;
}
