#pragma once

#include "CliCommon.hpp"

#include <filesystem>
#include <istream>
#include <optional>
#include <string>

namespace kb::script {
struct ScriptApiCatalog;
}

namespace kb::cli {
[[nodiscard]] int RunBlendSpace2DCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunVisualGraphCommand(const ArgumentList& arguments, CommandIo io);

[[nodiscard]] int RunLuaDebugCommand(const ArgumentList& arguments, CommandIo io);

[[nodiscard]] int RunLayoutCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunApiCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunGuideCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunCodeCommand(const ArgumentList& arguments, CommandIo io);
// code modules: the code map of the source tree under `root` as JSON (CliCodeModules.cpp).
[[nodiscard]] int RunCodeModules(const std::filesystem::path& root, const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAudioMixerCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneAudioMixerCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAnimatorCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSkeletonCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAbilityCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAiCommand(const ArgumentList& arguments, CommandIo io);
// World 2D maps: new/view work on a scene (CliEntityCommands.cpp), the rest on map and tileset files.
[[nodiscard]] int RunVectorShapeNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunVectorShapeShowCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunVectorShapeSetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMap2DCommand(const ArgumentList& arguments, CommandIo io);
// Terrain files (.kbterrain): the Terrain Editor's brushes, layers and heightmaps (CliTerrainCommands.cpp).
[[nodiscard]] int RunTerrainCommand(const ArgumentList& arguments, CommandIo io);
// How many levels of detail an imported model gets (CliMeshLodCommands.cpp).
[[nodiscard]] int RunMeshLodCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMap2DSceneCommand(std::string_view action, const ArgumentList& arguments, CommandIo io);
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
[[nodiscard]] int RunInputActionCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPrefabCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneDuplicateCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunScriptNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneLightmapCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSceneBakeLightingCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialSetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialShowCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialGraphCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialInstanceNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialInstanceSetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMaterialInstanceShowCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPostProcessNewCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunVfxCommand(std::string_view command, const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunVfxProfileCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunParticleProcessCookCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunRenderEffectRunCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunRenderEffectExportCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunRendererInfoCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPostProcessSetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunScenePostProcessCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunScriptBuildCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunRunCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunMcpCommand(const ArgumentList& arguments, std::istream& in, CommandIo io);
// lsp: the Language Server Protocol for a project's Lua scripts, over `in` and io.out (CliLspCommand.cpp).
[[nodiscard]] int RunLspCommand(const ArgumentList& arguments, std::istream& in, CommandIo io);
// dap: the Debug Adapter Protocol for the live Lua debugger of a running game (CliDapCommand.cpp).
[[nodiscard]] int RunDapCommand(const ArgumentList& arguments, std::istream& in, CommandIo io);
// The script API of the engine, with the names of the project in `project` when one is given (what `api` prints).
[[nodiscard]] bool BuildScriptApiCatalog(const std::optional<std::string>& project, kb::script::ScriptApiCatalog& catalog, std::string& error);
[[nodiscard]] int RunKeysCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackCommand(const ArgumentList& arguments, CommandIo io);
// Mods of a game: build, install, list, verify, enable, disable, remove, trust (CliModCommands.cpp).
[[nodiscard]] int RunModCommand(const ArgumentList& arguments, CommandIo io);
// Packages of a project: list, search, info, add, update, remove, verify (CliPackageCommands.cpp).
[[nodiscard]] int RunPackageCommand(const ArgumentList& arguments, CommandIo io);
// The pack subcommands that work on pack contents and pack sets (CliPackSetCommands.cpp).
[[nodiscard]] int RunPackInfoCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackCompressCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackSplitCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackPatchCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackSetKeysCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPackSetVerifyCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunReleaseCommand(const ArgumentList& arguments, CommandIo io);
// Cook, stage and render the project with the engine's cooker and player (CliBuildCommands.cpp).
[[nodiscard]] int RunBuildCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunCaptureCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunWorldCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunNavMeshCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPhysicsLayersCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunLightingQualityCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunProjectSettingsCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunPluginsCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunEditorCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunAssetCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunSoundCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunTimelineCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunUiThemeCommand(const ArgumentList& arguments, CommandIo io);
[[nodiscard]] int RunLocalizationCommand(const ArgumentList& arguments, CommandIo io);
#if KB_CLI_RENDERER
[[nodiscard]] int RunImageCommand(const ArgumentList& arguments, CommandIo io);
#endif
// From now on every command of this process may only name files and folders inside `projectRoot`: a path
// anywhere else (absolute, with .., or through a link that leads out) refuses the command before it runs.
// Reading a source to import and looking at the engine's code stay allowed. The MCP server, through which
// an agent runs kb_cli, confines itself to its project; nothing lifts the confinement again.
void ConfineCliToProject(const std::filesystem::path& projectRoot);
// For the test binary only, which runs the MCP server of many projects in one process: the engine itself never lifts a confinement.
void ResetCliConfinementForTests();
// The directory the cooker and the editor mount as /Game: the content root of the project in
// `project`, else of the nearest folder above `path` that holds a project file.
[[nodiscard]] std::optional<std::filesystem::path> FindProjectContentRoot(
    const std::optional<std::string>& project, const std::filesystem::path& path, std::string& error);

} // namespace kb::cli
