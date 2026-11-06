#pragma once

#include "engine/assets/AssetImportTypes.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <istream>
#include <ostream>

// The levels-of-detail record of a version-4 model container (see ImportedAssetLoader): manual flag,
// level count, the Auto share and deviation, the triangle threshold, then one share and deviation per
// coarser level, little-endian.
namespace kb::assets::detail {

inline constexpr std::uint32_t kImportContainerWithLodVersion = 4U;

inline void WriteLodBlock(std::ostream& output, const AssetMeshLodSettings& lod) {
    const auto put = [&output](std::uint32_t value, int bytes) {
        for (int index = 0; index < bytes; ++index) output.put(static_cast<char>((value >> (index * 8)) & 0xFFU));
    };
    put(lod.manual ? 1U : 0U, 1);
    put(lod.levelCount, 1);
    put(std::bit_cast<std::uint32_t>(lod.keepPercent), 4);
    put(std::bit_cast<std::uint32_t>(lod.errorPercent), 4);
    put(lod.minTriangles, 4);
    for (const AssetMeshLodLevel& level : lod.levels) {
        put(std::bit_cast<std::uint32_t>(level.trianglePercent), 4);
        put(std::bit_cast<std::uint32_t>(level.errorPercent), 4);
    }
}

// False for a truncated record or settings IsValid() refuses.
[[nodiscard]] inline bool ReadLodBlock(std::istream& input, AssetMeshLodSettings& lod) {
    bool ok = true;
    const auto get = [&input, &ok](int bytes) {
        std::uint32_t value = 0U;
        for (int index = 0; index < bytes; ++index) {
            const int byte = input.get();
            if (byte == std::istream::traits_type::eof()) {
                ok = false;
                return 0U;
            }
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(byte)) << (index * 8);
        }
        return value;
    };
    const std::uint32_t manual = get(1);
    lod.manual = manual != 0U;
    lod.levelCount = static_cast<std::uint8_t>(get(1));
    lod.keepPercent = std::bit_cast<float>(get(4));
    lod.errorPercent = std::bit_cast<float>(get(4));
    lod.minTriangles = get(4);
    for (AssetMeshLodLevel& level : lod.levels) {
        level.trianglePercent = std::bit_cast<float>(get(4));
        level.errorPercent = std::bit_cast<float>(get(4));
    }
    return ok && manual <= 1U && lod.IsValid();
}

} // namespace kb::assets::detail
