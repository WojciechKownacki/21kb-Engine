#include "TerrainImageExport.hpp"

#include "TerrainAnalysis.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace kb::terrain_editor {
namespace {

void SetError(std::string* output, std::string value) {
    if (output != nullptr) *output = std::move(value);
}

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string value = path.extension().string();
    std::ranges::transform(value, value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

// Written beside the target and renamed over it, so a reader never sees half a file.
[[nodiscard]] bool WriteFileAtomically(const std::filesystem::path& path, const std::uint8_t* data, std::size_t size, std::string* error) {
    std::error_code code;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), code);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        output.close();
        if (!output) {
            SetError(error, "Could not write " + path.generic_string());
            std::filesystem::remove(temporary, code);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code) {
        SetError(error, "Could not replace " + path.generic_string() + ": " + code.message());
        std::filesystem::remove(temporary, code);
        return false;
    }
    return true;
}

// A PNG with stored (uncompressed) deflate blocks, as the engine's own PNG writer makes them: the
// file is larger than a compressed one but needs no encoder library and any bit depth.
[[nodiscard]] std::vector<std::uint8_t> EncodePng(
    std::uint32_t width, std::uint32_t height, std::uint8_t colorType, std::uint8_t bitDepth,
    const std::uint8_t* rows, std::size_t rowBytes) {
    static const std::array<std::uint32_t, 256> kCrcTable = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t index = 0U; index < 256U; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) value = (value & 1U) != 0U ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
            table[index] = value;
        }
        return table;
    }();
    std::vector<std::uint8_t> png{ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    const auto put32 = [](std::vector<std::uint8_t>& bytes, std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    const auto chunk = [&](const char (&type)[5], const std::vector<std::uint8_t>& data) {
        put32(png, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        std::uint32_t crc = 0xFFFFFFFFU;
        for (const std::uint8_t byte : body) crc = kCrcTable[(crc ^ byte) & 0xFFU] ^ (crc >> 8U);
        png.insert(png.end(), body.begin(), body.end());
        put32(png, ~crc);
    };
    std::vector<std::uint8_t> header;
    put32(header, width);
    put32(header, height);
    header.insert(header.end(), { bitDepth, colorType, 0, 0, 0 });
    chunk("IHDR", header);
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1U) * height);
    for (std::uint32_t row = 0U; row < height; ++row) {
        raw.push_back(0U); // filter: none
        raw.insert(raw.end(), rows + row * rowBytes, rows + (row + 1U) * rowBytes);
    }
    std::vector<std::uint8_t> zlib{ 0x78, 0x01 };
    std::uint32_t sumA = 1U;
    std::uint32_t sumB = 0U;
    for (const std::uint8_t byte : raw) {
        sumA = (sumA + byte) % 65521U;
        sumB = (sumB + sumA) % 65521U;
    }
    for (std::size_t at = 0U; at < raw.size() || at == 0U; at += 65535U) {
        const auto length = static_cast<std::uint16_t>(std::min<std::size_t>(65535U, raw.size() - at));
        const bool last = at + length >= raw.size();
        zlib.insert(zlib.end(), { static_cast<std::uint8_t>(last ? 1U : 0U), static_cast<std::uint8_t>(length & 0xFFU),
            static_cast<std::uint8_t>(length >> 8U), static_cast<std::uint8_t>(~length & 0xFFU), static_cast<std::uint8_t>((~length >> 8U) & 0xFFU) });
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + length));
    }
    put32(zlib, (sumB << 16U) | sumA);
    chunk("IDAT", zlib);
    chunk("IEND", {});
    return png;
}

using Color = std::array<float, 3>;

[[nodiscard]] Color Lerp(const Color& a, const Color& b, float t) noexcept {
    return { std::lerp(a[0], b[0], t), std::lerp(a[1], b[1], t), std::lerp(a[2], b[2], t) };
}

template <std::size_t N>
[[nodiscard]] Color Ramp(const std::array<Color, N>& stops, float t) noexcept {
    const float scaled = std::clamp(t, 0.0F, 1.0F) * static_cast<float>(N - 1U);
    const std::size_t index = std::min(static_cast<std::size_t>(scaled), N - 2U);
    return Lerp(stops[index], stops[index + 1U], scaled - static_cast<float>(index));
}

constexpr std::array<Color, 5> kHeightStops{ Color{ 52.0F, 92.0F, 62.0F }, Color{ 128.0F, 150.0F, 78.0F },
    Color{ 166.0F, 134.0F, 92.0F }, Color{ 150.0F, 140.0F, 134.0F }, Color{ 244.0F, 244.0F, 246.0F } };
constexpr std::array<Color, 4> kSlopeStops{ Color{ 70.0F, 160.0F, 80.0F }, Color{ 230.0F, 214.0F, 70.0F },
    Color{ 232.0F, 140.0F, 50.0F }, Color{ 200.0F, 44.0F, 40.0F } };
constexpr std::array<Color, kb::assets::TerrainAsset::MaximumMaterialLayers> kLayerColors{ Color{ 92.0F, 142.0F, 62.0F },
    Color{ 130.0F, 112.0F, 92.0F }, Color{ 196.0F, 178.0F, 128.0F }, Color{ 236.0F, 236.0F, 242.0F } };

} // namespace

std::optional<TerrainHeightFormat> TerrainHeightFormatFromPath(const std::filesystem::path& path) {
    const std::string extension = LowerExtension(path);
    if (extension == ".png") return TerrainHeightFormat::Png16;
    if (extension == ".raw" || extension == ".r16") return TerrainHeightFormat::Raw16;
    return std::nullopt;
}

std::optional<TerrainPreviewKind> ParseTerrainPreviewKind(std::string_view text) noexcept {
    if (text == "height") return TerrainPreviewKind::Height;
    if (text == "slope") return TerrainPreviewKind::Slope;
    if (text == "splat") return TerrainPreviewKind::Splat;
    return std::nullopt;
}

std::optional<TerrainHeightExport> ExportTerrainHeightmap(
    const kb::assets::TerrainAsset& terrain,
    const std::filesystem::path& path,
    TerrainHeightFormat format,
    std::optional<std::pair<float, float>> range,
    std::string* error) {
    if (!kb::assets::IsTerrainAssetValid(terrain, error)) return std::nullopt;
    TerrainHeightExport result{ .width = terrain.width, .height = terrain.height };
    if (range.has_value()) {
        result.minHeight = range->first;
        result.maxHeight = range->second;
    } else {
        const auto [lowest, highest] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
        result.minHeight = *lowest;
        result.maxHeight = *highest > *lowest ? *highest : *lowest + 1.0F;
    }
    if (!std::isfinite(result.minHeight) || !std::isfinite(result.maxHeight) || result.maxHeight <= result.minHeight) {
        SetError(error, "Heightmap range is invalid");
        return std::nullopt;
    }
    const float scale = 65535.0F / (result.maxHeight - result.minHeight);
    std::vector<std::uint8_t> bytes(terrain.heights.size() * 2U);
    for (std::size_t index = 0U; index < terrain.heights.size(); ++index) {
        const auto value = static_cast<std::uint16_t>(
            std::lround(std::clamp((terrain.heights[index] - result.minHeight) * scale, 0.0F, 65535.0F)));
        // PNG stores 16-bit samples big-endian; a RAW file is little-endian.
        bytes[index * 2U] = static_cast<std::uint8_t>(format == TerrainHeightFormat::Png16 ? value >> 8U : value & 0xFFU);
        bytes[index * 2U + 1U] = static_cast<std::uint8_t>(format == TerrainHeightFormat::Png16 ? value & 0xFFU : value >> 8U);
    }
    if (format == TerrainHeightFormat::Raw16) {
        if (terrain.width != terrain.height) {
            SetError(error, "A RAW heightmap must be square; export a non-square terrain as PNG");
            return std::nullopt;
        }
        if (!WriteFileAtomically(path, bytes.data(), bytes.size(), error)) return std::nullopt;
        return result;
    }
    const std::vector<std::uint8_t> png = EncodePng(terrain.width, terrain.height, 0U, 16U, bytes.data(), static_cast<std::size_t>(terrain.width) * 2U);
    if (!WriteFileAtomically(path, png.data(), png.size(), error)) return std::nullopt;
    return result;
}

bool WriteTerrainPreviewPng(
    const kb::assets::TerrainAsset& terrain,
    TerrainPreviewKind kind,
    std::uint32_t pixels,
    const std::filesystem::path& path,
    std::string* error) {
    if (!kb::assets::IsTerrainAssetValid(terrain, error)) return false;
    if (pixels < 16U || pixels > 4096U) {
        SetError(error, "Preview size must be from 16 to 4096 pixels");
        return false;
    }
    const bool wide = terrain.worldSizeX >= terrain.worldSizeZ;
    const auto shorter = static_cast<std::uint32_t>(std::max(
        1.0F, std::round(static_cast<float>(pixels) * (wide ? terrain.worldSizeZ / terrain.worldSizeX : terrain.worldSizeX / terrain.worldSizeZ))));
    const std::uint32_t width = wide ? pixels : shorter;
    const std::uint32_t height = wide ? shorter : pixels;
    const auto [lowest, highest] = std::minmax_element(terrain.heights.begin(), terrain.heights.end());
    const float heightRange = std::max(*highest - *lowest, 0.0001F);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4U);
    constexpr std::array<float, 3> kLight{ -0.4082F, 0.8165F, -0.4082F };
    for (std::uint32_t y = 0U; y < height; ++y) {
        const float localZ = (static_cast<float>(y) + 0.5F) / static_cast<float>(height) * terrain.worldSizeZ - terrain.worldSizeZ * 0.5F;
        for (std::uint32_t x = 0U; x < width; ++x) {
            const float localX = (static_cast<float>(x) + 0.5F) / static_cast<float>(width) * terrain.worldSizeX - terrain.worldSizeX * 0.5F;
            const TerrainSurfaceSample sample = SampleTerrainSurface(terrain, localX, localZ);
            Color color{ 20.0F, 20.0F, 20.0F };
            if (!sample.hole) {
                float shade = 1.0F;
                if (kind == TerrainPreviewKind::Height) {
                    color = Ramp(kHeightStops, (sample.height - *lowest) / heightRange);
                    const float lit = std::clamp(
                        sample.normal[0] * kLight[0] + sample.normal[1] * kLight[1] + sample.normal[2] * kLight[2], 0.0F, 1.0F);
                    shade = 0.45F + 0.75F * lit;
                } else if (kind == TerrainPreviewKind::Slope) {
                    color = Ramp(kSlopeStops, sample.slopeDegrees / 60.0F);
                } else {
                    float total = 0.0F;
                    color = { 0.0F, 0.0F, 0.0F };
                    for (std::size_t layer = 0U; layer < kLayerColors.size(); ++layer) {
                        total += sample.layerWeights[layer];
                        for (std::size_t channel = 0U; channel < 3U; ++channel) {
                            color[channel] += kLayerColors[layer][channel] * sample.layerWeights[layer];
                        }
                    }
                    if (total <= 0.0F) color = { 128.0F, 128.0F, 128.0F };
                }
                for (float& channel : color) channel = std::min(channel * shade, 255.0F);
            }
            std::uint8_t* pixel = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4U;
            pixel[0] = static_cast<std::uint8_t>(std::lround(color[0]));
            pixel[1] = static_cast<std::uint8_t>(std::lround(color[1]));
            pixel[2] = static_cast<std::uint8_t>(std::lround(color[2]));
            pixel[3] = 255U;
        }
    }
    const std::vector<std::uint8_t> png = EncodePng(width, height, 6U, 8U, rgba.data(), static_cast<std::size_t>(width) * 4U);
    return WriteFileAtomically(path, png.data(), png.size(), error);
}

} // namespace kb::terrain_editor
