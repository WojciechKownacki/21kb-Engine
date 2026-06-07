#pragma once

#include "engine/assets/AssetImportTypes.hpp"

#include <filesystem>
#include <optional>

namespace kb::assets {

class ImportedAssetHeaderReader {
public:
    ImportedAssetHeaderReader() = delete;

    [[nodiscard]] static std::optional<AssetImportCategory> ReadCategory(const std::filesystem::path& path);
};

} // namespace kb::assets
