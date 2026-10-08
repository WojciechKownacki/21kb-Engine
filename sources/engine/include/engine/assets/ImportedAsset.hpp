#pragma once

#include "engine/assets/AssetImportTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::assets {

// A file the source referenced and the import carried along, so the asset still loads once the
// source folder is gone: the external buffers of a .gltf, keyed by their relative path.
struct ImportedAssetResource {
    std::string uri;
    std::vector<std::byte> bytes;
};

struct ImportedAsset {
    AssetImportCategory category = AssetImportCategory::Unknown;
    std::string sourceName;
    std::string sourceExtension;
    std::uint64_t sourceSize = 0;
    std::uint64_t sourceHash = 0;
    std::uint16_t importOptions = 0;
    std::vector<std::byte> payload;
    std::vector<ImportedAssetResource> resources;
};

} // namespace kb::assets
