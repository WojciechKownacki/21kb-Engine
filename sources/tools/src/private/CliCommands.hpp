#pragma once

#include "CliCommon.hpp"

#include <filesystem>
#include <istream>
#include <optional>
#include <string>

namespace kb::cli {

[[nodiscard]] int RunApiCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAnimatorCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSkeletonCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAbilityCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAiCommand(const ArgumentList& arguments, CommandIo io);
// Places a baked navigation mesh in the scene of --project/--scene (once), as the editor's Bake does.
[[nodiscard]] bool PlaceSceneNavMesh(const ArgumentList& arguments, const std::filesystem::path& navMesh, CommandIo io);
[[nodiscard]] int RunApiCheckCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunEntityCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunComponentCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSchemaCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunInitAgentCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunImportCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunImportPresetNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunValidateCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneListCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneAttachCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunScriptNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialSetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialShowCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialGraphCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialInstanceNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialInstanceSetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialInstanceShowCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPostProcessNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunVfxCommand(std::string_view command, const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPostProcessSetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunScenePostProcessCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunScriptBuildCommand(const ArgumentList& arguments, CommandIo io);
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
[[nodiscard]] int RunPhysicsLayersCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunLightingQualityCommand(const ArgumentList& arguments, CommandIo io);
// The directory the cooker and the editor mount as /Game: the content root of the project in
// `project`, else of the nearest folder above `path` that holds a project file.
[[nodiscard]] std::optional<std::filesystem::path> FindProjectContentRoot(
    const std::optional<std::string>& project, const std::filesystem::path& path, std::string& error);

} // namespace kb::cli
