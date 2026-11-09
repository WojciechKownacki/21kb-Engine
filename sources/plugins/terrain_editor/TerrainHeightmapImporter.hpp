#pragma once

#include "TerrainBrush.hpp"
#include "engine/assets/TerrainAsset.hpp"

#include <filesystem>
#include <memory>
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
    // A greyscale image (a PNG of any depth, or a square RAW) as the height image of a Stamp brush.
    [[nodiscard]] static std::shared_ptr<const TerrainStampImage> LoadStampImage(
        const std::filesystem::path& path,
        std::string* error = nullptr);
};

} // namespace kb::terrain_editor
