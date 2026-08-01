#include "TerrainHeightmapImporter.hpp"

#include <lodepng/lodepng.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace kb::terrain_editor {
namespace {

struct HeightImage { std::uint32_t width = 0U; std::uint32_t height = 0U; std::vector<float> samples; };

void SetError(std::string* output, std::string value) { if (output != nullptr) *output = std::move(value); }

[[nodiscard]] std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary | std::ios::ate };
    if (!input.is_open()) return {};
    const std::streamoff size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(256U * 1024U * 1024U)) return {};
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(data.data()), size);
    return input.good() ? data : std::vector<std::uint8_t>{};
}

[[nodiscard]] std::optional<HeightImage> DecodePng(const std::vector<std::uint8_t>& bytes, std::string* error) {
    unsigned char* decoded = nullptr;
    unsigned width = 0U;
    unsigned height = 0U;
    const unsigned result = lodepng_decode_memory(&decoded, &width, &height, bytes.data(), bytes.size(), LCT_GREY, 16U);
    if (result != 0U || decoded == nullptr || width == 0U || height == 0U) {
        SetError(error, "PNG heightmap decode failed: " + std::string{ lodepng_error_text(result) });
        if (decoded != nullptr) free(decoded);
        return std::nullopt;
    }
    HeightImage image{ .width = width, .height = height };
    image.samples.resize(static_cast<std::size_t>(width) * height);
    for (std::size_t index = 0U; index < image.samples.size(); ++index) {
        const std::uint16_t value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(decoded[index * 2U]) << 8U) | decoded[index * 2U + 1U]);
        image.samples[index] = static_cast<float>(value) / 65535.0F;
    }
    free(decoded);
    return image;
}

[[nodiscard]] std::optional<HeightImage> DecodeRaw(const std::vector<std::uint8_t>& bytes, std::string* error) {
    const auto squareSide = [](std::size_t count) -> std::uint32_t {
        const std::uint32_t side = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(count)));
        return static_cast<std::size_t>(side) * side == count ? side : 0U;
    };
    const std::uint32_t side16 = bytes.size() % 2U == 0U ? squareSide(bytes.size() / 2U) : 0U;
    const std::uint32_t side8 = squareSide(bytes.size());
    const bool sixteenBit = side16 != 0U;
    const std::uint32_t side = sixteenBit ? side16 : side8;
    if (side == 0U) {
        SetError(error, "RAW heightmap must be a square 8-bit or little-endian 16-bit image");
        return std::nullopt;
    }
    HeightImage image{ .width = side, .height = side };
    image.samples.resize(static_cast<std::size_t>(side) * side);
    for (std::size_t index = 0U; index < image.samples.size(); ++index) {
        if (sixteenBit) {
            const std::uint16_t value = static_cast<std::uint16_t>(bytes[index * 2U] | (static_cast<std::uint16_t>(bytes[index * 2U + 1U]) << 8U));
            image.samples[index] = static_cast<float>(value) / 65535.0F;
        } else {
            image.samples[index] = static_cast<float>(bytes[index]) / 255.0F;
        }
    }
    return image;
}

[[nodiscard]] std::uint32_t ClosestTerrainResolution(std::uint32_t source) noexcept {
    std::uint32_t best = kb::assets::TerrainAsset::MinimumResolution;
    std::uint32_t bestDistance = UINT32_MAX;
    for (std::uint32_t candidate = kb::assets::TerrainAsset::MinimumResolution;
         candidate <= kb::assets::TerrainAsset::MaximumResolution;
         candidate = (candidate - 1U) * 2U + 1U) {
        const std::uint32_t distance = candidate > source ? candidate - source : source - candidate;
        if (distance < bestDistance) { best = candidate; bestDistance = distance; }
    }
    return best;
}

[[nodiscard]] float SampleBilinear(const HeightImage& image, float u, float v) noexcept {
    const float x = u * static_cast<float>(image.width - 1U);
    const float y = v * static_cast<float>(image.height - 1U);
    const std::uint32_t x0 = static_cast<std::uint32_t>(x);
    const std::uint32_t y0 = static_cast<std::uint32_t>(y);
    const std::uint32_t x1 = std::min(x0 + 1U, image.width - 1U);
    const std::uint32_t y1 = std::min(y0 + 1U, image.height - 1U);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto at = [&image](std::uint32_t sx, std::uint32_t sy) { return image.samples[static_cast<std::size_t>(sy) * image.width + sx]; };
    return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
}

} // namespace

std::optional<kb::assets::TerrainAsset> TerrainHeightmapImporter::Import(
    const std::filesystem::path& path,
    const TerrainHeightmapImportSettings& settings,
    std::string* error) {
    if (!std::isfinite(settings.minimumHeight) || !std::isfinite(settings.maximumHeight) || settings.maximumHeight <= settings.minimumHeight) {
        SetError(error, "Heightmap range is invalid");
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes = ReadFile(path);
    if (bytes.empty()) { SetError(error, "Heightmap file could not be read"); return std::nullopt; }
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::optional<HeightImage> image = extension == ".png" ? DecodePng(bytes, error) : DecodeRaw(bytes, error);
    if (!image.has_value()) return std::nullopt;
    const std::uint32_t width = ClosestTerrainResolution(image->width);
    const std::uint32_t height = ClosestTerrainResolution(image->height);
    kb::assets::TerrainAsset terrain = kb::assets::MakeFlatTerrainAsset(width, static_cast<float>(width - 1U), static_cast<float>(height - 1U));
    terrain.height = height;
    terrain.heights.resize(static_cast<std::size_t>(width) * height);
    terrain.holes.assign(static_cast<std::size_t>(width - 1U) * (height - 1U), 0U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        const float v0 = static_cast<float>(y) / static_cast<float>(height - 1U);
        const float v = settings.flipVertically ? 1.0F - v0 : v0;
        for (std::uint32_t x = 0U; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width - 1U);
            terrain.heights[static_cast<std::size_t>(y) * width + x] =
                std::lerp(settings.minimumHeight, settings.maximumHeight, SampleBilinear(*image, u, v));
        }
    }
    if (!kb::assets::IsTerrainAssetValid(terrain, error)) return std::nullopt;
    return terrain;
}

} // namespace kb::terrain_editor
