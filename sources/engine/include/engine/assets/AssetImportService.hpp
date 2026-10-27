#pragma once

#include "engine/assets/AssetImportTypes.hpp"

#include <filesystem>
#include <span>

namespace kb::assets {

class AssetManager;

class AssetImportService {
public:
    AssetImportService() = delete;

    [[nodiscard]] static AssetImportResult ImportFiles(
        AssetManager& manager,
        std::span<const std::filesystem::path> sourceFiles,
        const std::filesystem::path& destinationVirtualFolder,
        const AssetImportOptions& options = {});

    // Replaces an imported asset's container with one made from `sourcePath` (a changed version of the same source), in place: the asset keeps
    // its id and path, so every reference to it keeps working. The model settings come from `options`. With `options.dryRun` nothing is written
    // and the item says what would happen. Status Updated when the container changed, Reused when the new source gives the same container,
    // Failed with a reason otherwise. Backing up the old container first is the caller's (AssetImportBackups).
    [[nodiscard]] static AssetImportItemResult ReimportFile(
        AssetManager& manager,
        AssetId id,
        const std::filesystem::path& sourcePath,
        const AssetImportOptions& options = {});
};

} // namespace kb::assets
