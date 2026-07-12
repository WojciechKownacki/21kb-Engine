#pragma once

#include "engine/assets/AssetImportTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::assets {

struct ImportedAsset {
    AssetImportCategory category = AssetImportCategory::Unknown;
    std::string sourceName;
    std::string sourceExtension;
    std::uint64_t sourceSize = 0;
    std::uint64_t sourceHash = 0;
    std::uint16_t importOptions = 0;
    std::vector<std::byte> payload;
};

} // namespace kb::assets
