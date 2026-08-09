#include "scene/EditorSceneAssetBrowserCommands.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/assets/AssetImportService.hpp"
#include "engine/scene/SkeletalMeshGltfImportPlanner.hpp"
#include "engine/scene/SkeletalMeshGltfImportPublisher.hpp"
#include "engine/scene/SkeletalMeshFbxImportPlanner.hpp"
#include "engine/scene/SkeletalMeshFbxImportPublisher.hpp"
#include "scene/EditorMeshImportArtifacts.hpp"

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
    const std::unordered_set<std::uint64_t>& sameOperationAssets = {}) {
    if (!target.IsValid()) {
        return nullptr;
    }
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (metadata.id == target || sameOperationAssets.contains(metadata.id.value)) {
            continue;
        }
        for (const kb::assets::AssetId dependency : metadata.dependencies) {
            if (dependency == target) {
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
    if (const kb::assets::AssetMetadata* dependent = FirstExternalDependent(manager, target, sameOperationAssets); dependent != nullptr) {
        manager.SetError(
            "Asset " + kb::assets::ToString(target) + " cannot be " + std::string{ operation } +
            " because " + dependent->virtualPath.generic_string() + " depends on it.");
        return false;
    }
    return true;
}

void RefreshAssets(kb::scene::Scene& scene) {
    static_cast<void>(scene.Assets().Discover());
}

[[nodiscard]] bool IsGltfSkeletalSource(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".gltf" || extension == ".glb";
}

[[nodiscard]] bool IsFbxSkeletalSource(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".fbx";
}

[[nodiscard]] kb::assets::AssetImportResult ImportSkeletalGltfFiles(
    kb::scene::Scene& scene,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    kb::assets::AssetImportResult result{};
    result.items.reserve(sourceFiles.size());
    for (const std::filesystem::path& sourcePath : sourceFiles) {
        kb::assets::AssetImportItemResult item{};
        item.sourcePath = sourcePath;
        item.category = kb::assets::AssetImportCategory::Model;
        if (!IsGltfSkeletalSource(sourcePath)) {
            item.status = kb::assets::AssetImportItemStatus::Unsupported;
            item.error = "Skeletal Mesh import supports glTF (.gltf) and GLB (.glb) sources only.";
            result.items.push_back(std::move(item));
            continue;
        }

        std::string error;
        auto prepared = EditorMeshImportArtifacts::Prepare(
            manager, sourcePath, destinationVirtualFolder, options, &error);
        if (!prepared) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Skeletal glTF auxiliary asset planning failed." : std::move(error);
            result.items.push_back(std::move(item));
            continue;
        }
        kb::scene::SkeletalMeshGltfImportOptions importOptions{};
        importOptions.combineMeshes = options.mesh.combineMeshes;
        if (options.mesh.importMaterials) {
            importOptions.materialResolver = &EditorMeshImportArtifacts::ResolveMaterial;
            importOptions.materialResolverUserData = &prepared->materialAssetIds;
        }
        const auto plan = kb::scene::SkeletalMeshGltfImportPlanner::Plan(
            manager, sourcePath, destinationVirtualFolder, importOptions, &error);
        if (!plan.has_value()) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Skeletal glTF import planning failed." : std::move(error);
            result.items.push_back(std::move(item));
            continue;
        }

        const bool meshAlreadyExisted = manager.Registry().FindByPath(plan->meshVirtualPath) != nullptr;
        const auto published = kb::scene::SkeletalMeshGltfImportPublisher::PublishWithArtifacts(
            manager, *plan, prepared->artifacts, &error);
        if (!published.has_value()) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Skeletal glTF asset publication failed." : std::move(error);
            result.items.push_back(std::move(item));
            continue;
        }

        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(published->meshAssetId);
        if (metadata == nullptr) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = "Skeletal glTF import completed without a registered Skeletal Mesh asset.";
            result.items.push_back(std::move(item));
            continue;
        }
        item.id = metadata->id;
        item.assetPhysicalPath = metadata->physicalPath;
        item.virtualPath = metadata->virtualPath;
        item.assetHash = metadata->contentHash;
        item.status = meshAlreadyExisted
            ? kb::assets::AssetImportItemStatus::Reused
            : kb::assets::AssetImportItemStatus::Created;
        result.items.push_back(std::move(item));
    }
    return result;
}

[[nodiscard]] kb::assets::AssetImportResult ImportSkeletalFbxFiles(
    kb::scene::Scene& scene, std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    kb::assets::AssetImportResult result{};
    result.items.reserve(sourceFiles.size());
    for (const std::filesystem::path& sourcePath : sourceFiles) {
        kb::assets::AssetImportItemResult item{};
        item.sourcePath = sourcePath;
        item.category = kb::assets::AssetImportCategory::Model;
        if (!IsFbxSkeletalSource(sourcePath)) {
            item.status = kb::assets::AssetImportItemStatus::Unsupported;
            item.error = "Skeletal Mesh import supports FBX, glTF (.gltf), and GLB (.glb) sources only.";
            result.items.push_back(std::move(item));
            continue;
        }
        std::string error;
        auto prepared = EditorMeshImportArtifacts::Prepare(
            manager, sourcePath, destinationVirtualFolder, options, &error);
        if (!prepared) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Skeletal FBX auxiliary asset planning failed." : std::move(error);
            result.items.push_back(std::move(item));
            continue;
        }
        kb::scene::SkeletalMeshFbxImportOptions importOptions{};
        importOptions.importMaterialSlots = options.mesh.importMaterialSlots;
        importOptions.combineMeshes = options.mesh.combineMeshes;
        if (options.mesh.importMaterials) {
            importOptions.materialResolver = &EditorMeshImportArtifacts::ResolveMaterial;
            importOptions.materialResolverUserData = &prepared->materialAssetIds;
        }
        const auto plan = kb::scene::SkeletalMeshFbxImportPlanner::Plan(
            manager, sourcePath, destinationVirtualFolder, importOptions, &error);
        if (!plan) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Skeletal FBX import planning failed." : std::move(error);
            result.items.push_back(std::move(item));
            continue;
        }
        const bool meshAlreadyExisted = manager.Registry().FindByPath(plan->meshVirtualPath) != nullptr;
        const auto published = kb::scene::SkeletalMeshFbxImportPublisher::PublishWithArtifacts(
            manager, *plan, prepared->artifacts, &error);
        if (!published) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Skeletal FBX asset publication failed." : std::move(error);
            result.items.push_back(std::move(item));
            continue;
        }
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(published->meshAssetId);
        if (metadata == nullptr) {
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = "Skeletal FBX import completed without a registered Skeletal Mesh asset.";
            result.items.push_back(std::move(item));
            continue;
        }
        item.id = metadata->id;
        item.assetPhysicalPath = metadata->physicalPath;
        item.virtualPath = metadata->virtualPath;
        item.assetHash = metadata->contentHash;
        item.status = meshAlreadyExisted ? kb::assets::AssetImportItemStatus::Reused : kb::assets::AssetImportItemStatus::Created;
        result.items.push_back(std::move(item));
    }
    return result;
}

[[nodiscard]] kb::assets::AssetImportResult ImportSkeletalFiles(
    kb::scene::Scene& scene, std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    kb::assets::AssetImportResult result{};
    result.items.reserve(sourceFiles.size());
    for (const std::filesystem::path& sourcePath : sourceFiles) {
        const std::array<std::filesystem::path, 1U> oneSource{ sourcePath };
        kb::assets::AssetImportResult one = IsGltfSkeletalSource(sourcePath)
            ? ImportSkeletalGltfFiles(scene, oneSource, destinationVirtualFolder, options)
            : ImportSkeletalFbxFiles(scene, oneSource, destinationVirtualFolder, options);
        result.items.insert(result.items.end(),
            std::make_move_iterator(one.items.begin()), std::make_move_iterator(one.items.end()));
    }
    return result;
}

[[nodiscard]] kb::assets::AssetImportResult ImportStaticFiles(
    kb::scene::Scene& scene,
    std::span<const std::filesystem::path> sourceFiles,
    const std::filesystem::path& destinationVirtualFolder,
    const kb::assets::AssetImportOptions& options) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    kb::assets::AssetImportResult result{};
    result.items.reserve(sourceFiles.size());
    for (const std::filesystem::path& sourcePath : sourceFiles) {
        const bool meshSource = IsGltfSkeletalSource(sourcePath) || IsFbxSkeletalSource(sourcePath);
        std::string error;
        std::optional<EditorPreparedMeshImportArtifacts> prepared;
        if (meshSource && (options.mesh.importTextures || options.mesh.importMaterials)) {
            prepared = EditorMeshImportArtifacts::Prepare(
                manager, sourcePath, destinationVirtualFolder, options, &error);
            if (!prepared) {
                result.items.push_back({
                    .sourcePath = sourcePath,
                    .category = kb::assets::AssetImportCategory::Model,
                    .status = kb::assets::AssetImportItemStatus::Failed,
                    .error = error.empty() ? "Static mesh auxiliary asset planning failed." : std::move(error),
                });
                continue;
            }
        }
        const std::array<std::filesystem::path, 1U> oneSource{ sourcePath };
        kb::assets::AssetImportResult imported = kb::assets::AssetImportService::ImportFiles(
            manager, oneSource, destinationVirtualFolder, options);
        if (imported.items.empty()) continue;
        kb::assets::AssetImportItemResult item = std::move(imported.items.front());
        if (item.Succeeded() && prepared.has_value() &&
            !EditorMeshImportArtifacts::PublishStandalone(manager, prepared->artifacts, &error)) {
            if (item.status == kb::assets::AssetImportItemStatus::Created) {
                std::error_code removeError;
                std::filesystem::remove(item.assetPhysicalPath, removeError);
                removeError.clear();
                if (!item.metaPhysicalPath.empty()) std::filesystem::remove(item.metaPhysicalPath, removeError);
                static_cast<void>(manager.DiscoverMountedAssets());
            }
            item.status = kb::assets::AssetImportItemStatus::Failed;
            item.error = error.empty() ? "Static mesh auxiliary asset publication failed." : std::move(error);
        }
        result.items.push_back(std::move(item));
    }
    return result;
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

bool EditorSceneAssetBrowserCommands::CommitTextEdit(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser) {
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
            const std::filesystem::path renamedVirtualPath = metadata->virtualPath.parent_path() / (value + metadata->virtualPath.extension().string());
            committed = CanMutateDependencyTarget(manager, metadata->id, "renamed") && manager.RenameAsset(metadata->id, value);
            if (committed) {
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
        committed = manager.RenameFolder(oldFolder, value);
        if (committed) {
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

bool EditorSceneAssetBrowserCommands::DeleteSelected(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser) {
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
    for (const kb::assets::AssetId asset : assetsToDelete) {
        if (!CanMutateDependencyTarget(manager, asset, "deleted", deletingAssetIds)) {
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

bool EditorSceneAssetBrowserCommands::DeleteAsset(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, kb::assets::AssetId id) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    if (!CanMutateDependencyTarget(manager, id, "deleted")) {
        return false;
    }
    const bool deleted = manager.DeleteAsset(id);
    if (deleted) {
        assetBrowser.ClearSelection();
        RefreshAssets(scene);
    }
    return deleted;
}

bool EditorSceneAssetBrowserCommands::DeleteFolder(kb::scene::Scene& scene, EditorAssetBrowserState& assetBrowser, const std::filesystem::path& virtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
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
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const kb::assets::AssetMoveResult moved = manager.MoveAssetIntoFolder(id, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
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
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const bool movedOpenFolder = SameVirtualPath(assetBrowser.SelectedFolder(), sourceVirtualFolder);
    const kb::assets::AssetMoveResult moved = manager.MoveFolderIntoFolder(sourceVirtualFolder, destinationVirtualFolder);
    if (!moved.succeeded) {
        return false;
    }

    RefreshAssets(scene);
    if (!moved.virtualPath.empty()) {
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
    const std::filesystem::path& destinationVirtualFolder) {
    kb::assets::AssetManager& manager = AssetManager(scene);
    const kb::assets::AssetMetadata* metadata = manager.Registry().Find(id);
    if (metadata == nullptr) {
        return false;
    }

    const std::filesystem::path source = metadata->physicalPath.empty()
        ? manager.Mounts().Resolve(metadata->virtualPath).value_or(std::filesystem::path{})
        : metadata->physicalPath;
    const std::optional<std::filesystem::path> destinationFolder = manager.Mounts().Resolve(destinationVirtualFolder);
    if (source.empty() || !destinationFolder.has_value()) {
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || !std::filesystem::is_directory(*destinationFolder, error)) {
        return false;
    }

    const std::filesystem::path destination = UniquePathInFolder(*destinationFolder, source.filename());
    if (destination.empty()) {
        return false;
    }
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
    if (error) {
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
    std::filesystem::copy(*source, destination, std::filesystem::copy_options::recursive, error);
    if (error) {
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
    const kb::assets::AssetImportResult imported = options.mesh.importSkeletalMesh
        ? ImportSkeletalFiles(scene, sourceFiles, destinationVirtualFolder, options)
        : ImportStaticFiles(scene, sourceFiles, destinationVirtualFolder, options);
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
