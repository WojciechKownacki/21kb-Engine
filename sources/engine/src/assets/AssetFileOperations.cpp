#include "assets/AssetFileOperations.hpp"

#include "assets/AssetFileSystem.hpp"
#include "assets/AssetPathUtilities.hpp"

#include <string>
#include <system_error>

namespace kb::assets {
namespace {

[[nodiscard]] std::filesystem::path MetaPathForAssetPath(std::filesystem::path assetPath) {
    assetPath.replace_extension(".meta");
    return assetPath;
}

[[nodiscard]] bool ExistingMetaSidecar(const std::filesystem::path& assetPath, std::filesystem::path& metaPath) {
    metaPath = MetaPathForAssetPath(assetPath);
    std::error_code error;
    return std::filesystem::is_regular_file(metaPath, error) && !error;
}

[[nodiscard]] std::filesystem::path UniqueAssetPathWithMeta(
    const std::filesystem::path& folder,
    const std::filesystem::path& filename) {
    const std::string stem = filename.stem().string();
    const std::string extension = filename.extension().string();
    for (int index = 0; index < 10000; ++index) {
        const std::string suffix = index == 0 ? std::string{} : "_" + std::to_string(index);
        const std::filesystem::path candidate = (folder / (stem + suffix + extension)).lexically_normal();
        const std::filesystem::path meta = MetaPathForAssetPath(candidate);
        std::error_code error;
        const bool assetExists = std::filesystem::exists(candidate, error);
        error.clear();
        const bool metaExists = std::filesystem::exists(meta, error);
        if (!assetExists && !metaExists) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] bool MoveMetaSidecarIfPresent(
    const std::filesystem::path& sourceAsset,
    const std::filesystem::path& destinationAsset) {
    std::filesystem::path sourceMeta;
    if (!ExistingMetaSidecar(sourceAsset, sourceMeta)) {
        return true;
    }

    const std::filesystem::path destinationMeta = MetaPathForAssetPath(destinationAsset);
    std::error_code error;
    if (std::filesystem::exists(destinationMeta, error)) {
        return false;
    }

    std::filesystem::rename(sourceMeta, destinationMeta, error);
    if (!error) {
        return true;
    }

    error.clear();
    std::filesystem::copy_file(sourceMeta, destinationMeta, std::filesystem::copy_options::none, error);
    if (error) {
        return false;
    }

    error.clear();
    std::filesystem::remove(sourceMeta, error);
    return !error;
}

} // namespace

bool AssetFileOperations::RenameAsset(
    const AssetRegistry& registry,
    const AssetMountTable& mounts,
    AssetId id,
    std::string newName,
    std::string& errorMessage) {
    const AssetMetadata* metadata = registry.Find(id);
    if (metadata == nullptr) {
        errorMessage = "Asset is not registered";
        return false;
    }
    if (!AssetPathUtilities::IsValidEntryName(newName)) {
        errorMessage = "Invalid asset name";
        return false;
    }

    const std::filesystem::path physical = AssetPathUtilities::ResolvePhysicalPath(mounts, *metadata);
    if (physical.empty()) {
        errorMessage = "Asset path could not be resolved";
        return false;
    }

    std::filesystem::path destination = physical.parent_path() / (std::move(newName) + physical.extension().string());
    const std::filesystem::path destinationMeta = MetaPathForAssetPath(destination);
    std::error_code error;
    if (!std::filesystem::is_regular_file(physical, error) || std::filesystem::exists(destination, error) || std::filesystem::exists(destinationMeta, error)) {
        errorMessage = "Asset rename destination is invalid";
        return false;
    }

    std::filesystem::rename(physical, destination, error);
    if (error) {
        errorMessage = "Asset could not be renamed";
        return false;
    }
    if (!MoveMetaSidecarIfPresent(physical, destination)) {
        errorMessage = "Asset meta could not be renamed";
        return false;
    }
    return true;
}

AssetMoveResult AssetFileOperations::MoveAssetIntoFolder(
    const AssetRegistry& registry,
    const AssetMountTable& mounts,
    AssetId id,
    const std::filesystem::path& destinationVirtualFolder,
    std::string& errorMessage) {
    const AssetMetadata* metadata = registry.Find(id);
    if (metadata == nullptr) {
        errorMessage = "Asset is not registered";
        return {};
    }

    const std::filesystem::path source = AssetPathUtilities::ResolvePhysicalPath(mounts, *metadata);
    if (source.empty()) {
        errorMessage = "Asset path could not be resolved";
        return {};
    }

    const std::optional<std::filesystem::path> destinationFolder = AssetPathUtilities::ResolveMountedFolderRoot(mounts, destinationVirtualFolder);
    if (!destinationFolder.has_value()) {
        errorMessage = "Asset destination folder mount could not be resolved";
        return {};
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error)) {
        errorMessage = "Asset file does not exist";
        return {};
    }
    if (!std::filesystem::is_directory(*destinationFolder, error) || error) {
        errorMessage = "Asset destination folder does not exist";
        return {};
    }

    std::filesystem::path destination = (*destinationFolder / source.filename()).lexically_normal();
    if (source.lexically_normal() == destination.lexically_normal()) {
        return AssetMoveResult{ .succeeded = true, .virtualPath = metadata->virtualPath };
    }

    destination = UniqueAssetPathWithMeta(*destinationFolder, source.filename());
    if (destination.empty()) {
        errorMessage = "Asset move destination could not be made unique";
        return {};
    }

    if (!AssetFileSystem::MoveFileReplacingNothing(source, destination)) {
        errorMessage = "Asset file could not be moved";
        return {};
    }
    if (!MoveMetaSidecarIfPresent(source, destination)) {
        errorMessage = "Asset meta could not be moved";
        return {};
    }

    const std::optional<std::filesystem::path> movedVirtualPath = mounts.ToVirtual(destination);
    return AssetMoveResult{
        .succeeded = true,
        .virtualPath = movedVirtualPath.value_or(destinationVirtualFolder / destination.filename()),
    };
}

bool AssetFileOperations::DeleteAsset(
    const AssetRegistry& registry,
    const AssetMountTable& mounts,
    AssetId id,
    std::string& errorMessage) {
    const AssetMetadata* metadata = registry.Find(id);
    if (metadata == nullptr) {
        errorMessage = "Asset is not registered";
        return false;
    }

    const std::filesystem::path physical = AssetPathUtilities::ResolvePhysicalPath(mounts, *metadata);
    if (physical.empty()) {
        errorMessage = "Asset path could not be resolved";
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(physical, error)) {
        errorMessage = "Asset file does not exist";
        return false;
    }

    std::filesystem::remove(physical, error);
    if (error) {
        errorMessage = "Asset file could not be deleted";
        return false;
    }
    std::filesystem::path metaPath;
    if (ExistingMetaSidecar(physical, metaPath)) {
        error.clear();
        std::filesystem::remove(metaPath, error);
        if (error) {
            errorMessage = "Asset meta could not be deleted";
            return false;
        }
    }
    return true;
}

} // namespace kb::assets
