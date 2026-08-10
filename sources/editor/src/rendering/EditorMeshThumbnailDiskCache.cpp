#include "rendering/EditorMeshThumbnailDiskCache.hpp"

#include "project/EditorProjectPaths.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <system_error>

namespace kb::editor {
namespace {

constexpr std::uint32_t kDiskCacheVersion = 4;
constexpr std::array<char, 12> kDiskCacheMagic{ '2', '1', 'K', 'B', 'T', 'H', 'U', 'M', 'B', '\r', '\n', '\0' };

void WriteU32(std::ostream& output, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        output.put(static_cast<char>((value >> shift) & 0xFFU));
    }
}

void WriteU64(std::ostream& output, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        output.put(static_cast<char>((value >> shift) & 0xFFULL));
    }
}

void WriteF32(std::ostream& output, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    WriteU32(output, bits);
}

[[nodiscard]] bool ReadU32(std::istream& input, std::uint32_t& value) {
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int byte = input.get();
        if (byte == EOF) {
            return false;
        }
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << shift;
    }
    return true;
}

[[nodiscard]] bool ReadU64(std::istream& input, std::uint64_t& value) {
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        const int byte = input.get();
        if (byte == EOF) {
            return false;
        }
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << shift;
    }
    return true;
}

[[nodiscard]] bool ReadF32(std::istream& input, float& value) {
    std::uint32_t bits = 0;
    if (!ReadU32(input, bits)) {
        return false;
    }
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

[[nodiscard]] std::string Hex64(std::uint64_t value) {
    std::ostringstream output;
    output << std::hex << std::nouppercase;
    output.width(16);
    output.fill('0');
    output << value;
    return output.str();
}

[[nodiscard]] std::filesystem::path ThumbnailCacheDirectory() {
    return EditorProjectPaths::ProjectRoot() / "Saved" / "Cache" / "Thumbnails";
}

[[nodiscard]] std::filesystem::path ThumbnailCachePath(const kb::assets::AssetMetadata& metadata) {
    const std::string filename = Hex64(metadata.id.value)
        + "_"
        + Hex64(metadata.contentHash)
        + "_v"
        + std::to_string(kDiskCacheVersion)
        + ".21kbthumb";
    return ThumbnailCacheDirectory() / filename;
}

void WriteImage(std::ostream& output, const EditorMeshThumbnailImage& image) {
    WriteU32(output, static_cast<std::uint32_t>(image.width));
    WriteU32(output, static_cast<std::uint32_t>(image.height));
    WriteU64(output, static_cast<std::uint64_t>(image.bgra.size()));
    if (!image.bgra.empty()) {
        output.write(reinterpret_cast<const char*>(image.bgra.data()), static_cast<std::streamsize>(image.bgra.size() * sizeof(std::uint32_t)));
    }
}

[[nodiscard]] bool ReadImage(std::istream& input, EditorMeshThumbnailImage& image, int expectedSize) {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t pixelCount = 0;
    if (!ReadU32(input, width) || !ReadU32(input, height) || !ReadU64(input, pixelCount)) {
        return false;
    }
    if (width != static_cast<std::uint32_t>(expectedSize)
        || height != static_cast<std::uint32_t>(expectedSize)
        || pixelCount != static_cast<std::uint64_t>(expectedSize * expectedSize)) {
        return false;
    }

    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.bgra.resize(static_cast<std::size_t>(pixelCount));
    input.read(reinterpret_cast<char*>(image.bgra.data()), static_cast<std::streamsize>(image.bgra.size() * sizeof(std::uint32_t)));
    return input.good();
}

[[nodiscard]] bool ReadOptionalImage(
    std::istream& input,
    EditorMeshThumbnailImage& image,
    int expectedSize) {
    const std::streampos start = input.tellg();
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t pixelCount = 0;
    if (!ReadU32(input, width) || !ReadU32(input, height) || !ReadU64(input, pixelCount)) {
        return false;
    }
    if (width == 0U && height == 0U && pixelCount == 0U) {
        image = {};
        return true;
    }
    input.clear();
    input.seekg(start);
    return input.good() && ReadImage(input, image, expectedSize);
}

void WriteStats(std::ostream& output, const EditorMeshThumbnailStats& stats) {
    WriteU32(output, stats.vertexCount);
    WriteU32(output, stats.indexCount);
    WriteU32(output, stats.triangleCount);
    WriteU32(output, stats.materialSlotCount);
    WriteF32(output, stats.boundsCenter[0]);
    WriteF32(output, stats.boundsCenter[1]);
    WriteF32(output, stats.boundsCenter[2]);
    WriteF32(output, stats.boundsRadius);
}

[[nodiscard]] bool ReadStats(std::istream& input, EditorMeshThumbnailStats& stats) {
    return ReadU32(input, stats.vertexCount)
        && ReadU32(input, stats.indexCount)
        && ReadU32(input, stats.triangleCount)
        && ReadU32(input, stats.materialSlotCount)
        && ReadF32(input, stats.boundsCenter[0])
        && ReadF32(input, stats.boundsCenter[1])
        && ReadF32(input, stats.boundsCenter[2])
        && ReadF32(input, stats.boundsRadius);
}

} // namespace

bool EditorMeshThumbnailDiskCache::Load(
    const kb::assets::AssetMetadata& metadata,
    EditorMeshThumbnailImage& thumbnail,
    EditorMeshThumbnailImage& preview,
    EditorMeshThumbnailStats& stats) {
    std::ifstream input{ ThumbnailCachePath(metadata), std::ios::binary };
    if (!input.is_open()) {
        return false;
    }

    std::array<char, kDiskCacheMagic.size()> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    std::uint32_t version = 0;
    std::uint64_t assetId = 0;
    std::uint64_t contentHash = 0;
    if (!input.good()
        || magic != kDiskCacheMagic
        || !ReadU32(input, version)
        || version != kDiskCacheVersion
        || !ReadU64(input, assetId)
        || !ReadU64(input, contentHash)
        || assetId != metadata.id.value
        || contentHash != metadata.contentHash
        || !ReadStats(input, stats)
        || !ReadImage(input, thumbnail, kEditorMeshThumbnailSize)
        || !ReadOptionalImage(input, preview, kEditorMeshPreviewSize)) {
        return false;
    }
    return true;
}

void EditorMeshThumbnailDiskCache::Save(
    const kb::assets::AssetMetadata& metadata,
    const EditorMeshThumbnailImage& thumbnail,
    const EditorMeshThumbnailImage& preview,
    const EditorMeshThumbnailStats& stats) {
    const std::filesystem::path cachePath = ThumbnailCachePath(metadata);
    std::error_code error;
    std::filesystem::create_directories(cachePath.parent_path(), error);
    if (error) {
        return;
    }

    const std::filesystem::path tempPath = cachePath.string() + ".tmp";
    {
        std::ofstream output{ tempPath, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            return;
        }
        output.write(kDiskCacheMagic.data(), static_cast<std::streamsize>(kDiskCacheMagic.size()));
        WriteU32(output, kDiskCacheVersion);
        WriteU64(output, metadata.id.value);
        WriteU64(output, metadata.contentHash);
        WriteStats(output, stats);
        WriteImage(output, thumbnail);
        WriteImage(output, preview);
        if (!output.good()) {
            output.close();
            std::filesystem::remove(tempPath, error);
            return;
        }
    }

    std::filesystem::rename(tempPath, cachePath, error);
    if (error) {
        std::filesystem::remove(cachePath, error);
        error.clear();
        std::filesystem::rename(tempPath, cachePath, error);
    }
    if (error) {
        std::filesystem::remove(tempPath, error);
    }
}

} // namespace kb::editor
