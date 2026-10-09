#pragma once

#include "CliCommon.hpp"

#include <filesystem>
#include <istream>
#include <optional>
#include <string>

namespace kb::cli {

[[nodiscard]] int RunApiCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunApiCheckCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunInitAgentCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunImportCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunValidateCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneListCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneAttachCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunRunCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMcpCommand(const ArgumentList& arguments, std::istream& in, CommandIo io);
[[nodiscard]] int RunKeysCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackCommand(const ArgumentList& arguments, CommandIo io);
// The pack subcommands that work on pack contents and pack sets (CliPackSetCommands.cpp).
[[nodiscard]] int RunPackInfoCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackCompressCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackSplitCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackPatchCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackSetKeysCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackSetVerifyCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunReleaseCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunWorldCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunNavMeshCommand(const ArgumentList& arguments, CommandIo io);
// The directory the cooker and the editor mount as /Game: the content root of the project in
// `project`, else of the nearest folder above `path` that holds a project file.
[[nodiscard]] std::optional<std::filesystem::path> FindProjectContentRoot(
    const std::optional<std::string>& project, const std::filesystem::path& path, std::string& error);

} // namespace kb::cli
