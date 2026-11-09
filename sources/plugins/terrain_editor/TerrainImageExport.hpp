#pragma once

#include "engine/assets/TerrainAsset.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kb::terrain_editor {

enum class TerrainHeightFormat : std::uint8_t {
    Png16, // 16-bit greyscale PNG
    Raw16, // little-endian unsigned 16-bit samples, no header
};

enum class TerrainPreviewKind : std::uint8_t {
    Height, // hypsometric colours with relief shading
    Slope,  // green flat to red steep
    Splat,  // the material layers blended
};

struct TerrainHeightExport {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    // The heights that 0 and 65535 stand for; import with these to get the terrain back.
    float minHeight = 0.0F;
    float maxHeight = 0.0F;
};

[[nodiscard]] std::optional<TerrainHeightFormat> TerrainHeightFormatFromPath(const std::filesystem::path& path);
[[nodiscard]] std::optional<TerrainPreviewKind> ParseTerrainPreviewKind(std::string_view text) noexcept;

// Writes the heights as a 16-bit image. Without `range` the lowest and highest height are used
// (a flat terrain gets a one unit range). Heights outside `range` clamp.
[[nodiscard]] std::optional<TerrainHeightExport> ExportTerrainHeightmap(
    const kb::assets::TerrainAsset& terrain,
    const std::filesystem::path& path,
    TerrainHeightFormat format,
    std::optional<std::pair<float, float>> range = std::nullopt,
    std::string* error = nullptr);

// Renders a top-down PNG of the terrain on the CPU: `pixels` across the longer side.
[[nodiscard]] bool WriteTerrainPreviewPng(
    const kb::assets::TerrainAsset& terrain,
    TerrainPreviewKind kind,
    std::uint32_t pixels,
    const std::filesystem::path& path,
    std::string* error = nullptr);

} // namespace kb::terrain_editor
