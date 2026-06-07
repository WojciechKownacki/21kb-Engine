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
        const std::filesystem::path& destinationVirtualFolder);
};

} // namespace kb::assets
