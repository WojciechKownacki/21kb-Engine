#pragma once

#include <cstddef>
#include <cstdint>

namespace kb::ecs {

enum class ChunkSizeProfile : std::uint8_t {
    Chunk16KB = 0,
    Chunk32KB = 1,
    Chunk64KB = 2,
    Chunk128KB = 3,
    Chunk256KB = 4,
    Chunk512KB = 5,
    Count = 6,
};

inline constexpr ChunkSizeProfile kDefaultChunkSizeProfile = ChunkSizeProfile::Chunk32KB;

[[nodiscard]] constexpr std::size_t ChunkPayloadBytes(ChunkSizeProfile profile) noexcept {
    switch (profile) {
    case ChunkSizeProfile::Chunk16KB:
        return 16 * 1024;
    case ChunkSizeProfile::Chunk32KB:
        return 32 * 1024;
    case ChunkSizeProfile::Chunk64KB:
        return 64 * 1024;
    case ChunkSizeProfile::Chunk128KB:
        return 128 * 1024;
    case ChunkSizeProfile::Chunk256KB:
        return 256 * 1024;
    case ChunkSizeProfile::Chunk512KB:
        return 512 * 1024;
    case ChunkSizeProfile::Count:
        return 0;
    }
    return 0;
}

} // namespace kb::ecs
