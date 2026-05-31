#include "assets/AssetFolderOperations.hpp"

#include "assets/AssetFileSystem.hpp"
#include "assets/AssetPathUtilities.hpp"

#include <set>
#include <system_error>

namespace kb::assets {

bool AssetFolderOperations::CreateFolder(
    const AssetMountTable& mounts,
    const std::filesystem::path& virtualFolder,
    std::string& errorMessage) {
    const std::filesystem::path parent = AssetPathUtilities::ParentVirtualPath(virtualFolder);
    if (parent.empty() || !AssetPathUtilities::IsValidEntryName(virtualFolder.filename().string())) {
        errorMessage = "Invalid asset folder name";
        return false;
    }

    const std::optional<std::filesystem::path> physical = mounts.Resolve(virtualFolder);
    if (!physical.has_value()) {
        errorMessage = "Asset folder mount could not be resolved";
        return false;
    }

    std::error_code error;
    if (std::filesystem::exists(*physical, error)) {
        errorMessage = "Asset folder already exists";
        return false;
    }
    std::filesystem::create_directories(*physical, error);
    if (error) {
        errorMessage = "Asset folder could not be created";
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> AssetFolderOperations::CreateUniqueFolder(
    const AssetMountTable& mounts,
    const std::vector<std::filesystem::path>& existingVirtualFolders,
    const std::filesystem::path& parentVirtualFolder,
    std::string baseName,
    std::string& errorMessage) {
    if (!AssetPathUtilities::IsValidEntryName(baseName)) {
        errorMessage = "Invalid asset folder name";
        return std::nullopt;
    }

    const std::optional<std::filesystem::path> parentPhysical = AssetPathUtilities::ResolveMountedFolderRoot(mounts, parentVirtualFolder);
    if (!parentPhysical.has_value()) {
        errorMessage = "Asset folder mount could not be resolved";
        return std::nullopt;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(*parentPhysical, error) || error) {
        errorMessage = "Asset folder parent does not exist";
        return std::nullopt;
    }

    std::set<std::string> existingVirtualFolderSet;
    for (const std::filesystem::path& folder : existingVirtualFolders) {
        existingVirtualFolderSet.insert(NormalizeAssetPath(folder));
    }

    for (int suffix = 0; suffix < 10000; ++suffix) {
        const std::string name = suffix == 0 ? baseName : baseName + "_" + std::to_string(suffix + 1);
        const std::filesystem::path candidateVirtual = parentVirtualFolder / name;
        const std::filesystem::path candidatePhysical = (*parentPhysical / name).lexically_normal();
        error.clear();
        if (existingVirtualFolderSet.contains(NormalizeAssetPath(candidateVirtual)) || std::filesystem::exists(candidatePhysical, error)) {
            continue;
        }

        error.clear();
        std::filesystem::create_directories(candidatePhysical, error);
        if (error) {
            errorMessage = "Asset folder could not be created";
            return std::nullopt;
        }
        return std::filesystem::path{ NormalizeAssetPath(candidateVirtual) };
    }

    errorMessage = "Asset folder name could not be made unique";
    return std::nullopt;
}

bool AssetFolderOperations::RenameFolder(
    const AssetMountTable& mounts,
    const std::filesystem::path& virtualFolder,
    std::string newName,
    std::string& errorMessage) {
    if (!AssetPathUtilities::IsValidEntryName(newName) || AssetPathUtilities::IsMountRoot(virtualFolder)) {
        errorMessage = "Invalid asset folder rename";
        return false;
    }

    const std::optional<std::filesystem::path> physical = mounts.Resolve(virtualFolder);
    if (!physical.has_value()) {
        errorMessage = "Asset folder mount could not be resolved";
        return false;
    }

    std::filesystem::path destination = physical->parent_path() / std::move(newName);
    std::error_code error;
    if (!std::filesystem::is_directory(*physical, error) || std::filesystem::exists(destination, error)) {
        errorMessage = "Asset folder rename destination is invalid";
        return false;
    }

    std::filesystem::rename(*physical, destination, error);
    if (error) {
        errorMessage = "Asset folder could not be renamed";
        return false;
    }
    return true;
}

bool AssetFolderOperations::DeleteFolder(
    const AssetMountTable& mounts,
    const std::filesystem::path& virtualFolder,
    std::string& errorMessage) {
    if (AssetPathUtilities::IsMountRoot(virtualFolder)) {
        errorMessage = "Asset mount root cannot be deleted";
        return false;
    }

    const std::optional<std::filesystem::path> physical = mounts.Resolve(virtualFolder);
    if (!physical.has_value()) {
        errorMessage = "Asset folder mount could not be resolved";
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(*physical, error)) {
        errorMessage = "Asset folder does not exist";
        return false;
    }
    std::filesystem::remove_all(*physical, error);
    if (error) {
        errorMessage = "Asset folder could not be deleted";
        return false;
    }
    return true;
}

AssetMoveResult AssetFolderOperations::MoveFolderIntoFolder(
    const AssetMountTable& mounts,
    const std::filesystem::path& sourceVirtualFolder,
    const std::filesystem::path& destinationVirtualFolder,
    std::string& errorMessage) {
    const std::string sourceNormalized = NormalizeAssetPath(sourceVirtualFolder);
    const std::string destinationNormalized = NormalizeAssetPath(destinationVirtualFolder);
    if (sourceNormalized.empty() || destinationNormalized.empty() || AssetPathUtilities::IsMountRoot(sourceVirtualFolder)) {
        errorMessage = "Invalid asset folder move";
        return {};
    }
    if (destinationNormalized == sourceNormalized) {
        return AssetMoveResult{ .succeeded = true, .virtualPath = std::filesystem::path{ sourceNormalized } };
    }
    if (AssetPathUtilities::IsSameOrDescendantVirtualPath(sourceVirtualFolder, destinationVirtualFolder)) {
        errorMessage = "Asset folder cannot be moved into itself";
        return {};
    }

    const std::optional<std::filesystem::path> source = mounts.Resolve(sourceVirtualFolder);
    const std::optional<std::filesystem::path> destinationFolder = AssetPathUtilities::ResolveMountedFolderRoot(mounts, destinationVirtualFolder);
    if (!source.has_value() || !destinationFolder.has_value()) {
        errorMessage = "Asset folder mount could not be resolved";
        return {};
    }

    std::error_code error;
    if (!std::filesystem::is_directory(*source, error) || error) {
        errorMessage = "Asset source folder does not exist";
        return {};
    }
    if (!std::filesystem::is_directory(*destinationFolder, error) || error) {
        errorMessage = "Asset destination folder does not exist";
        return {};
    }

    std::filesystem::path destination = (*destinationFolder / source->filename()).lexically_normal();
    if (source->lexically_normal() == destination.lexically_normal()) {
        return AssetMoveResult{ .succeeded = true, .virtualPath = std::filesystem::path{ sourceNormalized } };
    }

    destination = AssetFileSystem::UniqueFolderPathInFolder(*destinationFolder, source->filename());
    if (destination.empty()) {
        errorMessage = "Asset folder move destination could not be made unique";
        return {};
    }

    if (!AssetFileSystem::MoveFolderReplacingNothing(*source, destination)) {
        errorMessage = "Asset folder could not be moved";
        return {};
    }

    const std::optional<std::filesystem::path> movedVirtualPath = mounts.ToVirtual(destination);
    return AssetMoveResult{
        .succeeded = true,
        .virtualPath = movedVirtualPath.value_or(destinationVirtualFolder / destination.filename()),
    };
}

} // namespace kb::assets
