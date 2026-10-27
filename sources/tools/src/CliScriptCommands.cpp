#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/script/ScriptAsset.hpp"
#include "engine/script/ScriptProjectFiles.hpp"

#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace kb::cli {
namespace {

[[nodiscard]] std::string Utf8Text(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{ reinterpret_cast<const char*>(text.data()), text.size() };
}

[[nodiscard]] std::string ProjectPathText(const std::filesystem::path& path, const std::filesystem::path& projectRoot) {
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, projectRoot, error);
    return Utf8Text(error || relative.empty() ? path : relative);
}

[[nodiscard]] std::optional<kb::script::ScriptLanguage> ParseLanguage(std::string text) {
    for (char& character : text) {
        character = character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
    }
    if (text == "lua") {
        return kb::script::ScriptLanguage::Lua;
    }
    if (text == "cpp" || text == "c++") {
        return kb::script::ScriptLanguage::Cpp;
    }
    return std::nullopt;
}

// The project file of --project, or an empty path with the reason printed.
[[nodiscard]] std::filesystem::path ProjectFile(const ArgumentList& arguments, std::string_view command, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        io.err << "error: " << command << " requires --project <dir>\n";
        return {};
    }
    std::string error;
    const std::filesystem::path projectFile =
        kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
    }
    return projectFile;
}

// Builds one C++ script: its descriptor's build command, from where Play and the cooker run it, and
// then the DLL it names must be there. Returns why it failed, or an empty string.
[[nodiscard]] std::string BuildScript(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata,
    const std::filesystem::path& projectRoot, CommandIo io) {
    const kb::assets::AssetHandle<kb::script::NativeBehaviourDescriptor> descriptor =
        manager.Load<kb::script::NativeBehaviourDescriptor>(metadata.id);
    if (!descriptor.IsLoaded()) {
        return "the descriptor could not be read: " + manager.LastError();
    }
    const std::filesystem::path module = kb::script::ScriptProjectFiles::ModuleOf(*descriptor, metadata.physicalPath);
    if (!descriptor->build.enabled) {
        io.out << Utf8Text(metadata.virtualPath) << ": no build command; it uses " << Utf8Text(module) << " as it is.\n";
        return {};
    }
    io.out << "Building " << Utf8Text(metadata.virtualPath) << "...\n";
    // The build tool writes to the same console; what was printed so far comes first.
    io.out.flush();
    const kb::script::NativeScriptBuildResult built = kb::script::NativeScriptBuildPipeline::Build(
        kb::script::ScriptProjectFiles::BuildOf(*descriptor, metadata.physicalPath, projectRoot));
    if (!built.Succeeded()) {
        return built.errors.empty() ? "the build command failed with exit code " + std::to_string(built.exitCode)
                                    : built.errors.front();
    }
    std::error_code error;
    if (!module.empty() && !std::filesystem::is_regular_file(module, error)) {
        return "the build command succeeded but did not write " + Utf8Text(module);
    }
    io.out << "Built " << Utf8Text(metadata.virtualPath) << ": " << ProjectPathText(module, projectRoot) << '\n';
    return {};
}

} // namespace

int RunScriptNewCommand(const ArgumentList& arguments, CommandIo io) {
    const std::optional<std::string> name = arguments.Option("--name");
    const std::optional<std::string> languageText = arguments.Option("--language");
    if (!arguments.Option("--project").has_value() || !name.has_value() || !languageText.has_value()) {
        io.err << "error: script-new requires --project <dir> --name <Name> --language lua|cpp\n";
        return 1;
    }
    const std::optional<kb::script::ScriptLanguage> language = ParseLanguage(*languageText);
    if (!language.has_value()) {
        io.err << "error: unknown language '" << *languageText << "'. Languages: lua cpp\n";
        return 1;
    }
    const std::filesystem::path projectFile = ProjectFile(arguments, "script-new", io);
    if (projectFile.empty()) {
        return 1;
    }
    const std::filesystem::path projectRoot = projectFile.parent_path();
    const kb::assets::ProjectAssetFolder folder =
        kb::assets::ResolveProjectAssetFolder(projectRoot, arguments.Option("--folder").value_or("Assets/Scripts"));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return 1;
    }
    const kb::script::CreatedScript created = kb::script::ScriptProjectFiles::Create(*language, projectRoot, folder.folder, *name);
    if (!created.error.empty()) {
        io.err << "error: " << created.error << '\n';
        return 1;
    }
    const std::string asset = folder.virtualPath + "/" + Utf8Text(created.asset.filename());
    if (*language == kb::script::ScriptLanguage::Lua) {
        io.out << "Lua script created: " << asset << '\n';
        return 0;
    }
    io.out << "C++ script created: " << asset << " (source " << ProjectPathText(created.source, projectRoot)
           << ")\nBuild it with: kb_cli script-build --project \"" << *arguments.Option("--project") << "\" --script " << asset
           << '\n';
    return 0;
}

int RunScriptBuildCommand(const ArgumentList& arguments, CommandIo io) {
    const std::filesystem::path projectFile = ProjectFile(arguments, "script-build", io);
    if (projectFile.empty()) {
        return 1;
    }
    const kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        io.err << "error: the project could not be loaded: " << loaded.error << '\n';
        return 1;
    }
    const std::filesystem::path projectRoot = projectFile.parent_path();
    kb::scene::Scene scene{ loaded.descriptor };
    std::string error;
    if (!MountProjectAssets(scene, projectRoot, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    std::vector<const kb::assets::AssetMetadata*> scripts;
    if (const std::optional<std::string> script = arguments.Option("--script"); script.has_value()) {
        const kb::assets::AssetMetadata* metadata =
            FindAssetByFlexiblePath(manager.Registry(), ResolveInputPath(kb::assets::Utf8Path(*script), projectRoot));
        if (metadata == nullptr) {
            io.err << "error: " << *script << " is not an asset of this project\n";
            return 1;
        }
        if (metadata->type != kb::script::ScriptAssetTypes::NativeBehaviour) {
            io.err << "error: " << *script << " is not a C++ script (.native)\n";
            return 1;
        }
        scripts.push_back(metadata);
    } else {
        for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
            if (metadata.type == kb::script::ScriptAssetTypes::NativeBehaviour && !metadata.physicalPath.empty()) {
                scripts.push_back(&metadata);
            }
        }
    }
    if (scripts.empty()) {
        io.out << "The project has no C++ scripts.\n";
        return 0;
    }
    std::size_t failed = 0;
    for (const kb::assets::AssetMetadata* metadata : scripts) {
        if (std::string problem = BuildScript(manager, *metadata, projectRoot, io); !problem.empty()) {
            io.err << "error: " << Utf8Text(metadata->virtualPath) << ": " << problem << '\n';
            ++failed;
        }
    }
    io.out << "C++ scripts: " << scripts.size() - failed << " ready, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

} // namespace kb::cli
