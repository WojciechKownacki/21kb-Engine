#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::terrain_editor {

struct TerrainHeightmapImportSettings {
    float minimumHeight = -32.0F;
    float maximumHeight = 32.0F;
    bool flipVertically = false;
};

class TerrainHeightmapImporter final {
public:
    TerrainHeightmapImporter() = delete;
    [[nodiscard]] static std::optional<kb::assets::TerrainAsset> Import(
        const std::filesystem::path& path,
        const TerrainHeightmapImportSettings& settings = {},
        std::string* error = nullptr);
};

} // namespace kb::terrain_editor
