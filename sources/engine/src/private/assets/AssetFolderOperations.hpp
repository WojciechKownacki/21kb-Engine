#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMountTable.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::assets {

class AssetFolderOperations {
public:
    AssetFolderOperations() = delete;

    [[nodiscard]] static bool CreateFolder(
        const AssetMountTable& mounts,
        const std::filesystem::path& virtualFolder,
        std::string& error);
    [[nodiscard]] static std::optional<std::filesystem::path> CreateUniqueFolder(
        const AssetMountTable& mounts,
        const std::vector<std::filesystem::path>& existingVirtualFolders,
        const std::filesystem::path& parentVirtualFolder,
        std::string baseName,
        std::string& error);
    [[nodiscard]] static bool RenameFolder(
        const AssetMountTable& mounts,
        const std::filesystem::path& virtualFolder,
        std::string newName,
        std::string& error);
    [[nodiscard]] static bool DeleteFolder(
        const AssetMountTable& mounts,
        const std::filesystem::path& virtualFolder,
        std::string& error);
    [[nodiscard]] static AssetMoveResult MoveFolderIntoFolder(
        const AssetMountTable& mounts,
        const std::filesystem::path& sourceVirtualFolder,
        const std::filesystem::path& destinationVirtualFolder,
        std::string& error);
};

} // namespace kb::assets
