#include "engine/assets/TerrainAssetIO.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace kb::assets {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagic{ 'K', 'B', 'T', 'E', 'R', 'R', 'N', 0U };
constexpr std::uint64_t kHeaderBytes = 8U + (5U * sizeof(std::uint32_t)) + (2U * sizeof(float));

void SetError(std::string* output, std::string value) {
    if (output != nullptr) *output = std::move(value);
}

template <typename T>
void AppendLittleEndian(std::vector<std::uint8_t>& bytes, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    std::array<std::uint8_t, sizeof(T)> encoded{};
    std::memcpy(encoded.data(), &value, sizeof(T));
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(encoded.begin(), encoded.end());
    }
    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
}

template <typename T>
[[nodiscard]] bool ReadLittleEndian(std::span<const std::uint8_t> bytes, std::size_t& cursor, T& output) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (cursor > bytes.size() || sizeof(T) > bytes.size() - cursor) return false;
    std::array<std::uint8_t, sizeof(T)> encoded{};
    std::copy_n(bytes.data() + cursor, sizeof(T), encoded.data());
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(encoded.begin(), encoded.end());
    }
    std::memcpy(&output, encoded.data(), sizeof(T));
    cursor += sizeof(T);
    return true;
}

} // namespace

std::optional<TerrainAsset> TerrainAssetIO::Load(const std::filesystem::path& path, std::string* error) {
    std::ifstream input{ path, std::ios::binary | std::ios::ate };
    if (!input.is_open()) {
        SetError(error, "Terrain asset could not be opened");
        return std::nullopt;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(kHeaderBytes) || fileSize > static_cast<std::streamoff>(512U * 1024U * 1024U)) {
        SetError(error, "Terrain asset has an invalid file size");
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), fileSize);
    if (!input.good()) {
        SetError(error, "Terrain asset could not be read completely");
        return std::nullopt;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        SetError(error, "Terrain asset magic is invalid");
        return std::nullopt;
    }

    std::size_t cursor = kMagic.size();
    std::uint32_t version = 0U;
    TerrainAsset terrain{};
    if (!ReadLittleEndian(bytes, cursor, version) || version != TerrainAsset::CurrentVersion ||
        !ReadLittleEndian(bytes, cursor, terrain.width) || !ReadLittleEndian(bytes, cursor, terrain.height) ||
        !ReadLittleEndian(bytes, cursor, terrain.chunkQuads) || !ReadLittleEndian(bytes, cursor, terrain.lodCount) ||
        !ReadLittleEndian(bytes, cursor, terrain.worldSizeX) || !ReadLittleEndian(bytes, cursor, terrain.worldSizeZ) ||
        !IsTerrainResolutionValid(terrain.width) || !IsTerrainResolutionValid(terrain.height)) {
        SetError(error, "Terrain asset header is invalid or unsupported");
        return std::nullopt;
    }
    const std::uint64_t vertexCount = static_cast<std::uint64_t>(terrain.width) * terrain.height;
    const std::uint64_t cellCount = static_cast<std::uint64_t>(terrain.width - 1U) * (terrain.height - 1U);
    const std::uint64_t expectedSize = kHeaderBytes + vertexCount * sizeof(float) + cellCount;
    if (expectedSize != bytes.size()) {
        SetError(error, "Terrain asset payload size does not match its dimensions");
        return std::nullopt;
    }
    terrain.heights.resize(static_cast<std::size_t>(vertexCount));
    for (float& height : terrain.heights) {
        if (!ReadLittleEndian(bytes, cursor, height)) {
            SetError(error, "Terrain height payload is truncated");
            return std::nullopt;
        }
    }
    terrain.holes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor), bytes.end());
    if (!IsTerrainAssetValid(terrain, error)) return std::nullopt;
    return terrain;
}

bool TerrainAssetIO::Save(const std::filesystem::path& path, const TerrainAsset& terrain, std::string* error) {
    if (!IsTerrainAssetValid(terrain, error)) return false;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(kHeaderBytes) + terrain.heights.size() * sizeof(float) + terrain.holes.size());
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    AppendLittleEndian(bytes, TerrainAsset::CurrentVersion);
    AppendLittleEndian(bytes, terrain.width);
    AppendLittleEndian(bytes, terrain.height);
    AppendLittleEndian(bytes, terrain.chunkQuads);
    AppendLittleEndian(bytes, terrain.lodCount);
    AppendLittleEndian(bytes, terrain.worldSizeX);
    AppendLittleEndian(bytes, terrain.worldSizeZ);
    for (const float height : terrain.heights) AppendLittleEndian(bytes, height);
    bytes.insert(bytes.end(), terrain.holes.begin(), terrain.holes.end());

    std::error_code directoryError;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        SetError(error, "Terrain asset directory could not be created");
        return false;
    }
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        if (!output.is_open()) {
            SetError(error, "Terrain asset temporary file could not be opened");
            return false;
        }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output.good()) {
            SetError(error, "Terrain asset could not be written completely");
            return false;
        }
    }
    std::error_code renameError;
    std::filesystem::rename(temporary, path, renameError);
    if (renameError) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        renameError.clear();
        std::filesystem::rename(temporary, path, renameError);
    }
    if (renameError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        SetError(error, "Terrain asset could not replace its previous version");
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

} // namespace kb::assets
