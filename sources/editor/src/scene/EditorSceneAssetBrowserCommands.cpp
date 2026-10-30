#include "scene/EditorSceneAssetBrowserCommands.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/SceneAssetCopy.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "kb/render/resources/RenderMeshImportArtifacts.hpp"

#include <array>
#include <optional>
#include <cctype>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace kb::editor {
namespace {

[[nodiscard]] bool SameVirtualPath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return kb::assets::NormalizeAssetPath(left) == kb::assets::NormalizeAssetPath(right);
}

[[nodiscard]] bool SameOrDescendantVirtualPath(const std::filesystem::path& parent, const std::filesystem::path& candidate) {
    const std::string parentText = kb::assets::NormalizeAssetPath(parent);
    const std::string candidateText = kb::assets::NormalizeAssetPath(candidate);
    return candidateText == parentText || candidateText.starts_with(parentText + "/");
}

[[nodiscard]] kb::assets::AssetManager& AssetManager(kb::scene::Scene& scene) noexcept {
    return scene.Assets().Manager();
}

[[nodiscard]] const kb::assets::AssetMetadata* FirstExternalDependent(
    const kb::assets::AssetManager& manager,
    kb::assets::AssetId target,
    const std::unordered_set<std::uint64_t>& sameOperationAssets = {},
    bool relocation = false) {
    const auto* targetMetadata = manager.Registry().Find(target);
    if (!target.IsValid()) {
        return nullptr;
    }
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (sameOperationAssets.contains(metadata.id.value)) {
            continue;
        }
        for (const kb::assets::AssetId dependency : metadata.dependencies) {
            if (dependency == target) {
                if (relocation && targetMetadata != nullptr && targetMetadata->type == "ScenePrefab" &&
                    (metadata.type == "Scene" || metadata.type == "ScenePrefab")) continue;
                return &metadata;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] bool CanMutateDependencyTarget(
    kb::assets::AssetManager& manager,
    kb::assets::AssetId target,
    std::string_view operation,
    const std::unordered_set<std::uint64_t>& sameOperationAssets = {}) {
    if (const kb::assets::AssetMetadata* dependent = FirstExternalDependent(manager, target, sameOperationAssets, operation == "moved" || operation == "renamed"); dependent != nullptr) {
        const kb::assets::AssetMetadata* targetMetadata = manager.Registry().Find(target);
        const std::string name = targetMetadata != nullptr ? targetMetadata->virtualPath.generic_string() : kb::assets::ToString(target);
        manager.SetError(
            name + " can't be " + std::string{ operation } + ": " + dependent->virtualPath.generic_string() +
            " uses it. Remove that reference first.");
        return false;
    }
    return true;
}

// The assets inside `virtualFolder`, at any depth.
[[nodiscard]] std::vector<kb::assets::AssetId> AssetsUnder(
    const kb::assets::AssetManager& manager,
    const std::filesystem::path& virtualFolder) {
    std::vector<kb::assets::AssetId> assets;
    const std::string folder = kb::assets::NormalizeAssetPath(virtualFolder) + "/";
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (kb::assets::NormalizeAssetPath(metadata.virtualPath).starts_with(folder)) {
            assets.push_back(metadata.id);
        }
    }
    return assets;
}

void RelocateLoadedPrefabs(kb::scene::Scene& scene, const std::filesystem::path& previous,
    const std::filesystem::path& current, const std::vector<std::filesystem::path>& sources) {
    for (const auto& source : sources) {
        scene.Prefabs().RelocateSource(source, current / source.lexically_relative(previous));
    }
}

[[nodiscard]] std::vector<std::filesystem::path> PrefabSourcesUnder(
    const kb::assets::AssetManager& manager, const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> result;
    for (const auto id : AssetsUnder(manager, folder)) {
        const auto* metadata = manager.Registry().Find(id);
        if (metadata != nullptr && metadata->type == "ScenePrefab") result.push_back(metadata->physicalPath);
    }
    return result;
}

// Removing both ends of a reference is safe. Relocation changes paths even when
// both files move together, so those references must still prevent relocation.
[[nodiscard]] bool CanMutateFolderContents(
    kb::assets::AssetManager& manager,
    const std::filesystem::path& virtualFolder,
    std::string_view operation) {
    const std::vector<kb::assets::AssetId> contents = AssetsUnder(manager, virtualFolder);
    std::unordered_set<std::uint64_t> moving;
    if (operation == "deleted") {
        for (const kb::assets::AssetId asset : contents) {
            moving.insert(asset.value);
        }
    }
    for (const kb::assets::AssetId asset : contents) {
        if (!CanMutateDependencyTarget(manager, asset, operation, moving)) {
            return false;
        }
    }
    return true;
}

// Why `physical` - an asset file, or a folder - must not be deleted, renamed or moved: it is, or holds,
// the scene open in the editor or the project's startup scene. Sets the manager's error and returns
// false then.
[[nodiscard]] bool LeavesProtectedScenes(
    kb::assets::AssetManager& manager,
    const EditorProtectedScenes& protectedScenes,
    const std::filesystem::path& physical,
    std::string_view operation) {
    if (physical.empty()) {
        return true;
    }
    const auto holds = [&physical](const std::filesystem::path& scene) {
        if (scene.empty()) {
            return false;
        }
        const std::filesystem::path relative = scene.lexically_normal().lexically_relative(physical.lexically_normal());
        return !relative.empty() && *relative.begin() != "..";
    };
    const std::u8string name = physical.filename().u8string();
    const std::string quoted = "'" + std::string{ reinterpret_cast<const char*>(name.data()), name.size() } + "'";
    if (holds(protectedScenes.openScene)) {
        manager.SetError("Can't " + std::string{ operation } + " " + quoted +
            ": it holds the scene open in the editor. Open another scene first.");
        return false;
    }
    if (holds(protectedScenes.startupScene)) {
        manager.SetError("Can't " + std::string{ operation } + " " + quoted +
            ": it holds the project's startup scene. Choose another startup scene in Build Game first.");
        return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path AssetFile(const kb::assets::AssetManager& manager, kb::assets::AssetId id) {
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    return metadata != nullptr ? metadata->physicalPath : std::filesystem::path{};
}

[[nodiscard]] std::filesystem::path FolderFile(const kb::assets::AssetManager& manager, const std::filesystem::path& virtualFolder) {
    return manager.Mounts().Resolve(virtualFolder).value_or(std::filesystem::path{});
}

void RefreshAssets(kb::scene::Scene& scene) {
    static_cast<void>(scene.Assets().Discover());
}

[[nodiscard]] std::filesystem::path UniquePathInFolder(const std::filesystem::path& folder, const std::filesystem::path& filename) {
    std::filesystem::path candidate = folder / filename;
    std::error_code error;
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }

    const std::string stem = filename.stem().string();
    const std::string extension = filename.extension().string();
    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = folder / (stem + "_" + std::to_string(suffix) + extension);
        error.clear();
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] std::filesystem::path MetaPathForAssetPath(std::filesystem::path assetPath) {
    assetPath.replace_extension(".meta");
    return assetPath;
}

void CopyMetaSidecarIfPresent(const std::filesystem::path& sourceAsset, const std::filesystem::path& destinationAsset) {
    const std::filesystem::path sourceMeta = MetaPathForAssetPath(sourceAsset);
    const std::filesystem::path destinationMeta = MetaPathForAssetPath(destinationAsset);
    std::error_code error;
    if (!std::filesystem::is_regular_file(sourceMeta, error) || std::filesystem::exists(destinationMeta, error)) {
        return;
    }
    error.clear();
    std::filesystem::copy_file(sourceMeta, destinationMeta, std::filesystem::copy_options::none, error);
}

} // namespace

bool EditorSceneAssetBrowserCommands::CommitTextEdit(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser,
    const EditorProtectedScenes& protectedScenes) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const std::string value{ assetBrowser.TextEditValue() };
    if (value.empty()) {
        assetBrowser.CancelTextEdit();
        return false;
    }

    bool committed = false;
    switch (assetBrowser.TextEditMode()) {
    case EditorAssetTextEditMode::NewFolder: {
        const std::filesystem::path parent = assetBrowser.TextEditTargetFolder().empty() ? assetBrowser.SelectedFolder() : assetBrowser.TextEditTargetFolder();
        const std::optional<std::filesystem::path> folder = manager.CreateUniqueFolder(parent, value);
        committed = folder.has_value();
        if (folder.has_value()) {
            RefreshAssets(scene);
            static_cast<void>(assetBrowser.SelectContentFolder(*folder, manager));
        }
        break;
    }
    case EditorAssetTextEditMode::RenameAsset: {
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(assetBrowser.TextEditTargetAsset());
        if (metadata != nullptr) {
            const auto previousPhysicalPath = metadata->physicalPath;
            const std::filesystem::path renamedVirtualPath = metadata->virtualPath.parent_path() / (value + metadata->virtualPath.extension().string());
            committed = LeavesProtectedScenes(manager, protectedScenes, metadata->physicalPath, "rename") &&
                CanMutateDependencyTarget(manager, metadata->id, "renamed") && manager.RenameAsset(metadata->id, value);
            if (committed) {
                if (const auto destination = manager.Mounts().Resolve(renamedVirtualPath)) {
                    scene.Prefabs().RelocateSource(previousPhysicalPath, *destination);
                }
                RefreshAssets(scene);
                if (const kb::assets::AssetMetadata* renamed = manager.Registry().FindByPath(renamedVirtualPath); renamed != nullptr) {
                    static_cast<void>(assetBrowser.SelectAsset(renamed->id, manager));
                }
            }
        }
        break;
    }
    case EditorAssetTextEditMode::RenameFolder: {
        const std::filesystem::path oldFolder = assetBrowser.TextEditTargetFolder();
        const std::filesystem::path renamedFolder = oldFolder.parent_path() / value;
        const bool renamedOpenFolder = SameVirtualPath(oldFolder, assetBrowser.SelectedFolder());
        const auto previousPhysical = FolderFile(manager, oldFolder);
        const auto prefabSources = PrefabSourcesUnder(manager, oldFolder);
        committed = LeavesProtectedScenes(manager, protectedScenes, FolderFile(manager, oldFolder), "rename") &&
            CanMutateFolderContents(manager, oldFolder, "renamed") && manager.RenameFolder(oldFolder, value);
        if (committed) {
            RelocateLoadedPrefabs(scene, previousPhysical, FolderFile(manager, renamedFolder), prefabSources);
            if (renamedOpenFolder) {
                static_cast<void>(assetBrowser.SelectFolder(renamedFolder, manager));
            } else {
                static_cast<void>(assetBrowser.SelectContentFolder(renamedFolder, manager));
            }
        }
        break;
    }
    case EditorAssetTextEditMode::None:
    default:
        break;
    }

    assetBrowser.CancelTextEdit();
    return committed;
}

bool EditorSceneAssetBrowserCommands::DeleteSelected(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser,
    const EditorProtectedScenes& protectedScenes) {
    kb::assets::AssetManager& manager = AssetManager(scene);

    std::vector<kb::assets::AssetId> assetsToDelete;
    std::vector<std::filesystem::path> foldersToDelete;
    for (const EditorAssetSelectionSummaryRow& row : assetBrowser.CheckedDeleteTargetRows(manager)) {
        if (row.key.rfind("Asset:", 0) == 0) {
            kb::assets::AssetId id{};
            if (kb::assets::TryParseAssetId(std::string_view(row.key).substr(6), id) && id.IsValid()) {
                assetsToDelete.push_back(id);
            }
        } else if (row.key.rfind("Folder:", 0) == 0) {
            foldersToDelete.push_back(row.id);
        }
    }

    if (assetsToDelete.empty() && foldersToDelete.empty()) {
        return false;
    }

    std::unordered_set<std::uint64_t> deletingAssetIds;
    deletingAssetIds.reserve(assetsToDelete.size());
    for (const kb::assets::AssetId asset : assetsToDelete) {
        deletingAssetIds.insert(asset.value);
    }
    for (const std::filesystem::path& folder : foldersToDelete) {
        if (!LeavesProtectedScenes(manager, protectedScenes, FolderFile(manager, folder), "delete")) {
            return false;
        }
        for (const kb::assets::AssetId asset : AssetsUnder(manager, folder)) {
            deletingAssetIds.insert(asset.value);
        }
    }
    for (const kb::assets::AssetId asset : assetsToDelete) {
        if (!LeavesProtectedScenes(manager, protectedScenes, AssetFile(manager, asset), "delete")) {
            return false;
        }
    }
    // Nothing outside what is deleted may use any of it - also an asset inside a deleted folder.
    for (const std::uint64_t asset : deletingAssetIds) {
        if (!CanMutateDependencyTarget(manager, kb::assets::AssetId{ asset }, "deleted", deletingAssetIds)) {
            return false;
        }
    }

    const std::filesystem::path openFolderBeforeDelete = assetBrowser.SelectedFolder();
    bool deleted = false;
    for (const kb::assets::AssetId asset : assetsToDelete) {
        deleted = manager.DeleteAsset(asset) || deleted;
    }
    for (const std::filesystem::path& folder : foldersToDelete) {
        deleted = manager.DeleteFolder(folder) || deleted;
    }

    if (deleted) {
        assetBrowser.ClearSelection();
        for (const std::filesystem::path& folder : foldersToDelete) {
            if (SameOrDescendantVirtualPath(folder, openFolderBeforeDelete)) {
                static_cast<void>(assetBrowser.SelectFolder(folder.parent_path(), manager));
                break;
            }
        }
        RefreshAssets(scene);
    }
    return deleted;
}

bool EditorSceneAssetBrowserCommands::DeleteAsset(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, kb::assets::AssetId id,
    const EditorProtectedScenes& protectedScenes) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    if (!LeavesProtectedScenes(manager, protectedScenes, AssetFile(manager, id), "delete") ||
        !CanMutateDependencyTarget(manager, id, "deleted", { id.value })) {
        return false;
    }
    const bool deleted = manager.DeleteAsset(id);
    if (deleted) {
        assetBrowser.ClearSelection();
        RefreshAssets(scene);
    }
    return deleted;
}

bool EditorSceneAssetBrowserCommands::DeleteFolder(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, const std::filesystem::path& virtualFolder,
    const EditorProtectedScenes& protectedScenes) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    if (!LeavesProtectedScenes(manager, protectedScenes, FolderFile(manager, virtualFolder), "delete") ||
        !CanMutateFolderContents(manager, virtualFolder, "deleted")) {
        return false;
    }
    const bool deleted = manager.DeleteFolder(virtualFolder);
    if (deleted) {
        if (SameVirtualPath(assetBrowser.SelectedFolder(), virtualFolder)) {
            static_cast<void>(assetBrowser.SelectFolder(virtualFolder.parent_path(), manager));
        } else if (SameVirtualPath(assetBrowser.SelectedContentFolder(), virtualFolder)) {
            assetBrowser.ClearSelection();
        }
        RefreshAssets(scene);
    }
    return deleted;
}

bool EditorSceneAssetBrowserCommands::MoveAssetToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    kb::assets::AssetId id,
    const std::filesystem::path& destinationVirtualFolder,
    const EditorProtectedScenes& protectedScenes) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    // Scene and prefab links use GUIDs; other references still require their original path.
    if (!LeavesProtectedScenes(manager, protectedScenes, AssetFile(manager, id), "move") ||
        !CanMutateDependencyTarget(manager, id, "moved")) {
        return false;
    }
    const auto previousPhysicalPath = AssetFile(manager, id);
    const kb::assets::AssetMoveResult moved = manager.MoveAssetIntoFolder(id, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
    }

    if (const auto destination = manager.Mounts().Resolve(moved.virtualPath)) {
        scene.Prefabs().RelocateSource(previousPhysicalPath, *destination);
    }
    RefreshAssets(scene);
    if (const kb::assets::AssetMetadata* movedMetadata = manager.Registry().FindByPath(moved.virtualPath); movedMetadata != nullptr) {
        static_cast<void>(assetBrowser.SelectAsset(movedMetadata->id, manager));
    }
    return true;
}

bool EditorSceneAssetBrowserCommands::MoveFolderToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    const std::filesystem::path& sourceVirtualFolder,
    const std::filesystem::path& destinationVirtualFolder,
    const EditorProtectedScenes& protectedScenes) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    if (!LeavesProtectedScenes(manager, protectedScenes, FolderFile(manager, sourceVirtualFolder), "move") ||
        !CanMutateFolderContents(manager, sourceVirtualFolder, "moved")) {
        return false;
    }
    const bool movedOpenFolder = SameVirtualPath(assetBrowser.SelectedFolder(), sourceVirtualFolder);
    const auto previousPhysical = FolderFile(manager, sourceVirtualFolder);
    const auto prefabSources = PrefabSourcesUnder(manager, sourceVirtualFolder);
    const kb::assets::AssetMoveResult moved = manager.MoveFolderIntoFolder(sourceVirtualFolder, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
    }

    RefreshAssets(scene);
    if (!moved.virtualPath.empty()) {
        RelocateLoadedPrefabs(scene, previousPhysical, FolderFile(manager, moved.virtualPath), prefabSources);
        if (movedOpenFolder) {
            static_cast<void>(assetBrowser.SelectFolder(moved.virtualPath, manager));
        } else {
            static_cast<void>(assetBrowser.SelectContentFolder(moved.virtualPath, manager));
        }
    }
    return true;
}

bool EditorSceneAssetBrowserCommands::CopyAssetToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    kb::assets::AssetId id,
    const std::filesystem::path& destinationVirtualFolder,
    std::string& reason) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        reason = "The asset to copy is no longer in the project.";
        return false;
    }

    const std::filesystem::path source = metadata->physicalPath.empty()
        ? manager.Mounts().Resolve(metadata->virtualPath).value_or(std::filesystem::path{})
        : metadata->physicalPath;
    const std::optional<std::filesystem::path> destinationFolder = manager.Mounts().Resolve(destinationVirtualFolder);
    if (source.empty()) {
        reason = metadata->virtualPath.generic_string() + " has no file on disk to copy.";
        return false;
    }
    if (!destinationFolder.has_value()) {
        reason = destinationVirtualFolder.generic_string() + " is not a folder of this project.";
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error)) {
        reason = "The file " + source.generic_string() + " is missing.";
        return false;
    }
    if (!std::filesystem::is_directory(*destinationFolder, error)) {
        reason = "The folder " + destinationVirtualFolder.generic_string() + " does not exist.";
        return false;
    }

    const std::filesystem::path destination = UniquePathInFolder(*destinationFolder, source.filename());
    if (destination.empty()) {
        reason = "There is no free name for a copy of " + source.filename().generic_string() + " in " +
            destinationVirtualFolder.generic_string() + ".";
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
    if (error) {
        reason = source.filename().generic_string() + " could not be copied to " + destinationVirtualFolder.generic_string() +
            ": " + error.message();
        return false;
    }
    if (!kb::scene::ReidentifyCopiedSceneAssets(destination, reason)) {
        std::filesystem::remove(destination, error);
        return false;
    }
    CopyMetaSidecarIfPresent(source, destination);

    RefreshAssets(scene);
    if (const std::optional<std::filesystem::path> copiedVirtualPath = manager.Mounts().ToVirtual(destination); copiedVirtualPath.has_value()) {
        if (const kb::assets::AssetMetadata* copied = manager.Registry().FindByPath(*copiedVirtualPath); copied != nullptr) {
            static_cast<void>(assetBrowser.SelectAsset(copied->id, manager));
        }
    }
    return true;
}

bool EditorSceneAssetBrowserCommands::CopyFolderToFolder(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    const std::filesystem::path& sourceVirtualFolder,
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    if (SameOrDescendantVirtualPath(sourceVirtualFolder, destinationVirtualFolder)) {
        return false;
    }

    const std::optional<std::filesystem::path> source = manager.Mounts().Resolve(sourceVirtualFolder);
    const std::optional<std::filesystem::path> destinationFolder = manager.Mounts().Resolve(destinationVirtualFolder);
    if (!source.has_value() || !destinationFolder.has_value()) {
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(*source, error) || !std::filesystem::is_directory(*destinationFolder, error)) {
        return false;
    }

    std::filesystem::path destination = UniquePathInFolder(*destinationFolder, source->filename());
    if (destination.empty()) {
        return false;
    }
    if (!std::filesystem::create_directory(destination, error)) {
        manager.SetError("Could not reserve a new folder for the copy.");
        return false;
    }
    std::filesystem::copy(*source, destination, std::filesystem::copy_options::recursive, error);
    std::string reason;
    if (error || !kb::scene::ReidentifyCopiedSceneAssets(destination, reason)) {
        manager.SetError(error ? error.message() : reason);
        std::error_code cleanup;
        std::filesystem::remove_all(destination, cleanup);
        return false;
    }

    RefreshAssets(scene);
    if (const std::optional<std::filesystem::path> copiedVirtualPath = manager.Mounts().ToVirtual(destination); copiedVirtualPath.has_value()) {
        static_cast<void>(assetBrowser.SelectContentFolder(*copiedVirtualPath, manager));
    }
    return true;
}

bool EditorSceneAssetBrowserCommands::ImportFiles(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    return ImportFilesWithReport(scene, assetBrowser, sourceFiles, destinationVirtualFolder, options).ImportedCount() > 0U;
}

kb::assets::AssetImportResult EditorSceneAssetBrowserCommands::ImportFilesWithReport(
    kb::scene::Scene& scene,
    EditorAssetBrowserState& assetBrowser,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    if (sourceFiles.empty()) {
        return {};
    }

    kb::assets::AssetManager& manager = AssetManager(scene);
    if (options.mesh.importSkeletalMesh && !options.mesh.transform.IsIdentity()) {
        // The skeleton, its clips and the skinned mesh would all have to take the scale and
        // rotation; the skeletal publication does not apply them, so it refuses them by name.
        kb::assets::AssetImportResult refused{};
        for (const std::filesystem::path& sourcePath : sourceFiles) {
            refused.items.push_back(kb::assets::AssetImportItemResult{
                .sourcePath = sourcePath,
                .category = kb::assets::AssetImportCategory::Model,
                .status = kb::assets::AssetImportItemStatus::Failed,
                .error = "Import scale and rotation apply to static meshes; a skeletal import keeps the source scale and orientation.",
            });
        }
        return refused;
    }
    const kb::assets::AssetImportResult imported = options.mesh.importSkeletalMesh
        ? kb::render::RenderMeshImportArtifacts::ImportSkeletalFiles(manager, sourceFiles, destinationVirtualFolder, options)
        : kb::render::RenderMeshImportArtifacts::ImportStaticFiles(manager, sourceFiles, destinationVirtualFolder, options);
    if (imported.ImportedCount() == 0U) {
        return imported;
    }

    RefreshAssets(scene);
    for (const kb::assets::AssetImportItemResult& item : imported.items) {
        if (!item.Succeeded()) {
            continue;
        }
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(item.virtualPath); metadata != nullptr) {
            static_cast<void>(assetBrowser.SelectAsset(metadata->id, manager));
            break;
        }
    }
    return imported;
}

} // namespace kb::editor
