#pragma once

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMountTable.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <filesystem>
#include <string>

namespace kb::assets {

class AssetFileOperations {
public:
    AssetFileOperations() = delete;

    [[nodiscard]] static bool RenameAsset(
        const AssetRegistry& registry,
        const AssetMountTable& mounts,
        AssetId id,
        std::string newName,
        std::string& error);
    [[nodiscard]] static AssetMoveResult MoveAssetIntoFolder(
        const AssetRegistry& registry,
        const AssetMountTable& mounts,
        AssetId id,
        const std::filesystem::path& destinationVirtualFolder,
        std::string& error);
    [[nodiscard]] static bool DeleteAsset(
        const AssetRegistry& registry,
        const AssetMountTable& mounts,
        AssetId id,
        std::string& error);
};

} // namespace kb::assets
