#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::assets {

enum class AssetLoadState {
    Unloaded,
    Loaded,
    Failed,
};

struct AssetMetadata {
    AssetId id{};
    std::string type;
    std::string importCategory;
    std::string name;
    std::filesystem::path virtualPath;
    std::filesystem::path physicalPath;
    std::uint64_t contentHash = 0;
    std::vector<AssetId> dependencies;
    bool runtimeLoadable = true;
};

[[nodiscard]] std::string NormalizeAssetPath(const std::filesystem::path& path);

} // namespace kb::assets
