#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::assets {

class TerrainAssetIO final {
public:
    TerrainAssetIO() = delete;

    [[nodiscard]] static std::optional<TerrainAsset> Load(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const TerrainAsset& terrain,
        std::string* error = nullptr);
};

} // namespace kb::assets
