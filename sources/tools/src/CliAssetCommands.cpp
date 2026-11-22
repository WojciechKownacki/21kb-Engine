#include "CliCommands.hpp"
#include "engine/core/JsonValue.hpp"

#include "engine/assets/AssetChangeRules.hpp"
#include "engine/assets/AssetFileName.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace kb::cli {

namespace {

// The physical file of a project scene setting (/Game/...), or nothing.
[[nodiscard]] std::filesystem::path SceneFile(const std::filesystem::path& root, const std::string& virtualPath) {
    if (!virtualPath.starts_with("/Game/")) return {};
    return (root / "Assets" / kb::assets::Utf8Path(virtualPath.substr(6U))).lexically_normal();
}

} // namespace

// Tidies the project's files as Project Files does: deletes (to the Recycle Bin), renames or moves an asset
// or a folder, or makes a folder, with the asset's .meta and under the same rules - nothing another asset
// uses, and not the startup scene or the scene open in the editor.
int RunAssetCommand(const ArgumentList& arguments, CommandIo io) {
    const auto project = arguments.Option("--project");
    const auto path = arguments.Option("--path");
    const auto to = arguments.Option("--to");
    const std::string action = arguments.Positionals().size() == 1U ? arguments.Positionals().front() : std::string{};
    if (!project || !path || !path->starts_with("/Game") || !((action == "delete" || action == "new-folder") || ((action == "rename" || action == "move") && to))) {
        io.err << "error: asset delete --project <dir> --path /Game/<file or folder>\n"
                  "       asset rename --project <dir> --path /Game/<file or folder> --to <NewName>\n"
                  "       asset move --project <dir> --path /Game/<file or folder> --to /Game/<folder>\n"
                  "       asset new-folder --project <dir> --path /Game/<folder>\n";
        return 1;
    }
    std::string error;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(kb::assets::Utf8Path(*project), error);
    if (projectFile.empty()) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    const std::filesystem::path root = projectFile.parent_path();
    const kb::project::ProjectDescriptorReadResult descriptor = kb::project::ProjectManager::LoadProject(projectFile);
    if (!descriptor.succeeded) {
        io.err << "error: " << descriptor.error << '\n';
        return 1;
    }
    // Mounted as every kb_cli command mounts it, so every asset type the editor knows is known here.
    kb::scene::Scene scene{ descriptor.descriptor };
    if (!MountProjectAssets(scene, root, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    kb::assets::AssetManager& manager = scene.Assets().Manager();
    // The startup scene, and the scene the editor has open (it keeps it as the last open one).
    const kb::project::ProjectSettingsLoadResult settings =
        kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(root));
    const kb::assets::AssetProtectedScenes protectedScenes{
        .openScene = SceneFile(root, settings.settings.lastOpenMap),
        .startupScene = SceneFile(root, settings.settings.defaultMap),
    };

    const std::filesystem::path virtualPath = kb::assets::Utf8Path(*path);
    if (action == "new-folder") {
        if (!manager.CreateFolder(virtualPath)) {
            io.err << "error: " << (manager.LastError().empty() ? "the folder could not be made" : manager.LastError()) << '\n';
            return 1;
        }
        io.out << "made the folder " << *path << '\n';
    } else {
        const kb::assets::AssetMetadata* asset = manager.Registry().FindByPath(virtualPath);
        std::error_code missing;
        const std::optional<std::filesystem::path> physical = manager.Mounts().Resolve(virtualPath);
        const bool folder = asset == nullptr && physical.has_value() && std::filesystem::is_directory(*physical, missing);
        if (asset == nullptr && !folder) {
            io.err << "error: the project has no file or folder " << *path << '\n';
            return 1;
        }
        const std::filesystem::path file = asset != nullptr ? asset->physicalPath : *physical;
        const std::string verb = action == "delete" ? "delete" : action;
        const std::string done = action == "delete" ? "deleted" : action == "rename" ? "renamed" : "moved";
        const kb::assets::AssetId id = asset != nullptr ? asset->id : kb::assets::AssetId{};
        bool ok = kb::assets::AssetChangeRules::LeavesProtectedScenes(manager, protectedScenes, file, verb) &&
            (folder ? kb::assets::AssetChangeRules::CanChangeFolder(manager, virtualPath, done)
                    : kb::assets::AssetChangeRules::CanChange(manager, id, done, action == "delete"
                          ? std::unordered_set<std::uint64_t>{ id.value } : std::unordered_set<std::uint64_t>{}));
        if (ok) {
            if (action == "delete") {
                ok = folder ? manager.DeleteFolder(virtualPath) : manager.DeleteAsset(id);
            } else if (action == "rename") {
                ok = folder ? manager.RenameFolder(virtualPath, *to) : manager.RenameAsset(id, *to);
            } else {
                const std::filesystem::path destination = kb::assets::Utf8Path(*to);
                ok = folder ? static_cast<bool>(manager.MoveFolderIntoFolder(virtualPath, destination))
                            : static_cast<bool>(manager.MoveAssetIntoFolder(id, destination));
            }
        }
        if (!ok) {
            io.err << "error: " << (manager.LastError().empty() ? "the " + std::string{ folder ? "folder" : "file" } + " could not be " + done
                                                              : manager.LastError()) << '\n';
            return 1;
        }
        io.out << done << ' ' << *path << (action == "delete" ? " (to the Recycle Bin)" : action == "rename" ? " to " + *to : " into " + *to) << '\n';
    }
    kb::core::JsonValue data = DataObject("21kb.cli.asset/v1");
    data.Set("action", kb::core::JsonValue::MakeString(action));
    data.Set("path", kb::core::JsonValue::MakeString(*path));
    PublishData(io, std::move(data));
    return 0;
}

} // namespace kb::cli
