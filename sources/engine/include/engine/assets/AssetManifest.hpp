#pragma once

#include "engine/assets/AssetRegistry.hpp"

#include <filesystem>

namespace kb::assets {

class AssetManifest {
public:
    AssetManifest() = delete;

    [[nodiscard]] static bool Save(const std::filesystem::path& path, const AssetRegistry& registry);
    [[nodiscard]] static bool Load(const std::filesystem::path& path, AssetRegistry& registry);
};

} // namespace kb::assets
