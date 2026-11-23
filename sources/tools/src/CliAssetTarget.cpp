#include "CliAssetTarget.hpp"
#include "CliCommands.hpp"

#include "engine/assets/AssetFileName.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <system_error>

namespace kb::cli {
namespace {

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower;
    for (const char c : text) lower += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    return lower;
}

} // namespace

std::optional<AssetTarget> ResolveAssetTarget(const ArgumentList& arguments, std::string_view command, std::string_view usage,
    std::string_view defaultFolder, std::string_view extension, bool allowExisting, CommandIo io) {
    const std::optional<std::string> project = arguments.Option("--project");
    std::optional<std::string> name = arguments.Option("--name");
    if (!project.has_value() || !name.has_value()) {
        io.err << "error: " << command << " requires " << usage << '\n';
        return std::nullopt;
    }
    if (Lower(*name).ends_with(extension)) name->resize(name->size() - extension.size());
    const bool plain = !name->empty() && name->size() <= 64U && std::ranges::all_of(*name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
    if (!plain) {
        io.err << "error: --name is a plain name of letters, digits, - and _ (for example Intro), not a path, got '" << *name << "'\n";
        return std::nullopt;
    }
    // Names Windows keeps for devices (CON, NUL, COM1 ...) cannot be files there, with any extension.
    const std::string lowerName = Lower(*name);
    const bool device = lowerName == "con" || lowerName == "prn" || lowerName == "aux" || lowerName == "nul" ||
        ((lowerName.starts_with("com") || lowerName.starts_with("lpt")) && lowerName.size() == 4U && lowerName[3] >= '0' && lowerName[3] <= '9');
    if (device) {
        io.err << "error: --name '" << *name << "' is a reserved system name; pick another, for example " << *name << "1\n";
        return std::nullopt;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return std::nullopt;
    }
    AssetTarget target;
    target.projectRoot = projectFile.parent_path();
    target.project = *project;
    target.name = *name;
    const kb::assets::ProjectAssetFolder folder =
        kb::assets::ResolveProjectAssetFolder(target.projectRoot, arguments.Option("--folder").value_or(std::string{ defaultFolder }));
    if (!folder.error.empty()) {
        io.err << "error: " << folder.error << '\n';
        return std::nullopt;
    }
    std::error_code ec;
    const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(target.projectRoot / "Assets", ec);
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(folder.folder, ec);
    const std::filesystem::path relative = resolved.lexically_relative(assetsRoot);
    if (ec || relative.empty() || *relative.begin() == "..") {
        io.err << "error: refused for safety: the folder must lie inside the project's Assets\n";
        return std::nullopt;
    }
    target.folderVirtual = folder.virtualPath;
    target.file = folder.folder / kb::assets::Utf8Path(*name + std::string{ extension });
    target.virtualPath = folder.virtualPath + "/" + *name + std::string{ extension };
    if (!allowExisting && std::filesystem::exists(target.file, ec) && !arguments.Flag("--replace")) {
        io.err << "error: " << target.virtualPath << " already exists; pick another --name, or add --replace to make it again\n";
        return std::nullopt;
    }
    return target;
}

bool RunSiblingCommand(int (*command)(const ArgumentList&, CommandIo), const std::vector<std::string>& words, std::string& message) {
    std::ostringstream out;
    std::ostringstream err;
    // Flags that take no value, so the command reads its words as the dispatcher does.
    static constexpr std::array<std::string_view, 3> kFlags{ "--replace", "--disabled", "--all" };
    const int code = command(ArgumentList{ words, kFlags }, CommandIo{ .out = out, .err = err });
    message = err.str().empty() ? out.str() : err.str();
    return code == 0;
}

std::optional<ProjectSceneDocument> LoadProjectScene(const ArgumentList& arguments, std::string& error) {
    const std::optional<std::string> project = arguments.Option("--project");
    if (!project.has_value()) {
        error = "--project <dir> is required";
        return std::nullopt;
    }
    kb::scene::Scene scene;
    if (!MountProjectAssets(scene, kb::assets::Utf8Path(*project), error)) return std::nullopt;
    std::string sceneText = arguments.Option("--scene").value_or(std::string{});
    if (sceneText.empty()) {
        sceneText = kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(*project)).settings.defaultMap;
    }
    ProjectSceneDocument result;
    if (const kb::assets::AssetMetadata* metadata = FindAssetByFlexiblePath(scene.Assets().Manager().Registry(), kb::assets::Utf8Path(sceneText))) {
        result.file = metadata->physicalPath;
        result.virtualPath = metadata->virtualPath.generic_string();
    } else {
        result.file = ResolveProjectFile(kb::assets::Utf8Path(sceneText), kb::assets::Utf8Path(*project), error);
        if (result.file.empty()) {
            if (error.empty()) error = "scene was not found: " + sceneText;
            return std::nullopt;
        }
        result.virtualPath = sceneText;
    }
    const kb::scene::SceneDocumentLoadResult loaded = kb::scene::SceneDocumentService::Load(result.file);
    if (!loaded.succeeded) {
        error = "could not load scene " + result.file.generic_string() + ": " + loaded.error;
        return std::nullopt;
    }
    result.document = loaded.document;
    return result;
}

bool AttachScriptOnObject(const ArgumentList& arguments, const std::string& object, const std::string& scriptName, const std::string& code,
    std::string& error) {
    const std::string project = arguments.Option("--project").value_or(std::string{});
    std::vector<std::string> sceneWords;
    if (const std::optional<std::string> scene = arguments.Option("--scene")) sceneWords = { "--scene", *scene };
    const auto withScene = [&](std::vector<std::string> words) {
        words.insert(words.end(), { "--project", project });
        words.insert(words.end(), sceneWords.begin(), sceneWords.end());
        return words;
    };
    kb::scene::Scene mounted;
    if (!MountProjectAssets(mounted, kb::assets::Utf8Path(project), error)) return false;
    std::string message;
    const std::string scriptPath = "/Game/Scripts/" + scriptName + ".lua";
    if (const kb::assets::AssetMetadata* existing = FindAssetByFlexiblePath(mounted.Assets().Manager().Registry(), scriptPath)) {
        std::ofstream file(existing->physicalPath, std::ios::binary | std::ios::trunc);
        file << code;
    } else if (!RunSiblingCommand(&RunScriptNewCommand, { "--project", project, "--name", scriptName, "--language", "lua", "--code", code }, message)) {
        error = "the script could not be made: " + message;
        return false;
    }
    const std::optional<ProjectSceneDocument> loaded = LoadProjectScene(arguments, error);
    if (!loaded.has_value()) return false;
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = loaded->document.worldPrefab.Nodes();
    const bool there = std::ranges::any_of(nodes, [&](const kb::scene::ScenePrefabNodeDesc& node) { return node.name == object; });
    if (!there && !RunSiblingCommand(&RunEntityCommand, withScene({ "add", "--name", object }), message)) {
        error = "the object " + object + " could not be added: " + message;
        return false;
    }
    if (!RunSiblingCommand(&RunSceneAttachCommand, withScene({ "--node", object, "--script", scriptPath, "--replace" }), message)) {
        error = "the script could not be attached: " + message;
        return false;
    }
    return true;
}

std::optional<SceneObjectTransform> FindSceneObject(const ProjectSceneDocument& scene, std::string_view name) {
    const std::span<const kb::scene::ScenePrefabNodeDesc> nodes = scene.document.worldPrefab.Nodes();
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].name != name && NodePath(nodes, index) != name) continue;
        SceneObjectTransform found;
        found.transform.position = nodes[index].transform.localPosition;
        found.transform.rotation = nodes[index].transform.localRotation;
        found.transform.scale = nodes[index].transform.localScale;
        found.path = NodePath(nodes, index);
        return found;
    }
    return std::nullopt;
}

std::string NodePath(std::span<const kb::scene::ScenePrefabNodeDesc> nodes, std::size_t index) {
    std::string path = nodes[index].name;
    std::size_t parent = nodes[index].parentNode;
    for (std::size_t depth = 0U; parent < nodes.size() && depth < nodes.size(); ++depth) {
        path = nodes[parent].name + "/" + path;
        parent = nodes[parent].parentNode;
    }
    return path;
}

} // namespace kb::cli
