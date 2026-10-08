#include "CliCommands.hpp"

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kUsage = R"(kb_cli — 21kb Engine headless tooling for developers and AI coding agents

Usage: kb_cli <command> [options]

Commands:
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
  run          Run a scene headless and report logs, events, and errors.
                 --project <dir> --scene <path> [--frames <n>] [--dt <seconds>]
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
  pack         Sign (and optionally encrypt) an asset pack, or verify one.
                 sign --key <key file> [--content-key <file>] <pack.kbpack>
                 verify [--anchor <file> | --public-key <hex>
                        [--content-key <file>]] <pack.kbpack>
  release      Sign the manifest of a finished release directory, or verify one
               against the trust anchor of the player it ships.
                 sign --key <key file> --dir <dir> --product <id>
                      --content-version <version> --release <number>
                      [--anti-rollback]
                 verify [--anchor <file>] <dir>

Scene paths may be physical (relative to the project root) or virtual
("/Game/Scenes/Main.21kbscene", requires --project).
)";

constexpr std::array<std::string_view, 4> kFlagNames{ "--anti-rollback", "--disabled", "--quiet", "--update-baseline" };

} // namespace

int main(int argc, char** argv) {
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
    if (command == "help" || command == "--help" || command == "-h") {
        std::cout << kUsage;
        return 0;
    }

    std::cerr << "error: unknown command '" << command << "'\n\n" << kUsage;
    return 1;
}
