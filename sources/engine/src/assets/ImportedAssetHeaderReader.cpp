#include "engine/assets/ImportedAssetHeaderReader.hpp"

#include <array>
#include <fstream>

namespace kb::assets {
namespace {

constexpr std::array<char, 8> AssetMagic{ '2', '1', 'K', 'B', 'A', 'S', 'T', '\0' };
constexpr std::uint32_t SupportedVersion = 1U;

[[nodiscard]] bool ReadU16(std::istream& input, std::uint16_t& value) {
    value = 0;
    for (int shift = 0; shift < 16; shift += 8) {
        const int byte = input.get();
        if (byte == EOF) {
            return false;
        }
        value |= static_cast<std::uint16_t>(static_cast<unsigned char>(byte) << shift);
    }
    return true;
}

[[nodiscard]] bool ReadU32(std::istream& input, std::uint32_t& value) {
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int byte = input.get();
        if (byte == EOF) {
            return false;
        }
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte) << shift);
    }
    return true;
}

} // namespace

std::optional<AssetImportCategory> ImportedAssetHeaderReader::ReadCategory(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::array<char, AssetMagic.size()> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    std::uint32_t version = 0;
    std::uint16_t category = 0;
    std::uint16_t flags = 0;
    if (magic != AssetMagic
        || !ReadU32(input, version)
        || version != SupportedVersion
        || !ReadU16(input, category)
        || !ReadU16(input, flags)) {
        return std::nullopt;
    }
    static_cast<void>(flags);
    return static_cast<AssetImportCategory>(category);
}

} // namespace kb::assets
