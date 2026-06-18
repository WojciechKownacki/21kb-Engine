#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace kb::ecs {

enum class ChunkSizeProfile : std::uint8_t {
    Chunk4KB = 0,
    Chunk8KB = 1,
    Chunk16KB = 2,
    Chunk32KB = 3,
    Chunk64KB = 4,
    Chunk128KB = 5,
    Chunk256KB = 6,
    Chunk512KB = 7,
    Count = 8,
};

enum class ChunkSizeProfileParseError : std::uint8_t {
    None,
    Empty,
    UnsupportedValue,
};

struct ChunkSizeProfileParseResult {
    ChunkSizeProfile profile = ChunkSizeProfile::Chunk32KB;
    ChunkSizeProfileParseError error = ChunkSizeProfileParseError::None;
    std::string_view diagnostic;

    [[nodiscard]] constexpr bool HasValue() const noexcept {
        return error == ChunkSizeProfileParseError::None;
    }
};

inline constexpr std::size_t kMinChunkPayloadBytes = 4 * 1024;
inline constexpr std::size_t kMaxChunkPayloadBytes = 512 * 1024;
inline constexpr ChunkSizeProfile kDefaultChunkSizeProfile = ChunkSizeProfile::Chunk32KB;

[[nodiscard]] constexpr bool IsPowerOfTwoChunkPayloadBytes(std::size_t payloadBytes) noexcept {
    return payloadBytes != 0U && (payloadBytes & (payloadBytes - 1U)) == 0U;
}

[[nodiscard]] constexpr bool IsValidCustomChunkPayloadBytes(std::size_t payloadBytes) noexcept {
    return payloadBytes >= kMinChunkPayloadBytes && payloadBytes <= kMaxChunkPayloadBytes && IsPowerOfTwoChunkPayloadBytes(payloadBytes);
}

[[nodiscard]] constexpr std::size_t ChunkPayloadBytes(ChunkSizeProfile profile) noexcept {
    switch (profile) {
    case ChunkSizeProfile::Chunk4KB:
        return 4 * 1024;
    case ChunkSizeProfile::Chunk8KB:
        return 8 * 1024;
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

[[nodiscard]] constexpr std::string_view ChunkSizeProfileName(ChunkSizeProfile profile) noexcept {
    switch (profile) {
    case ChunkSizeProfile::Chunk4KB:
        return "4KB";
    case ChunkSizeProfile::Chunk8KB:
        return "8KB";
    case ChunkSizeProfile::Chunk16KB:
        return "16KB";
    case ChunkSizeProfile::Chunk32KB:
        return "32KB";
    case ChunkSizeProfile::Chunk64KB:
        return "64KB";
    case ChunkSizeProfile::Chunk128KB:
        return "128KB";
    case ChunkSizeProfile::Chunk256KB:
        return "256KB";
    case ChunkSizeProfile::Chunk512KB:
        return "512KB";
    case ChunkSizeProfile::Count:
        return {};
    }
    return {};
}

[[nodiscard]] constexpr std::optional<ChunkSizeProfile> ChunkSizeProfileFromPayloadBytes(std::size_t payloadBytes) noexcept {
    if (!IsValidCustomChunkPayloadBytes(payloadBytes)) {
        return std::nullopt;
    }
    switch (payloadBytes) {
    case 4 * 1024:
        return ChunkSizeProfile::Chunk4KB;
    case 8 * 1024:
        return ChunkSizeProfile::Chunk8KB;
    case 16 * 1024:
        return ChunkSizeProfile::Chunk16KB;
    case 32 * 1024:
        return ChunkSizeProfile::Chunk32KB;
    case 64 * 1024:
        return ChunkSizeProfile::Chunk64KB;
    case 128 * 1024:
        return ChunkSizeProfile::Chunk128KB;
    case 256 * 1024:
        return ChunkSizeProfile::Chunk256KB;
    case 512 * 1024:
        return ChunkSizeProfile::Chunk512KB;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] constexpr char NormalizeChunkSizeProfileCharacter(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] constexpr bool ChunkSizeProfileTokenEquals(std::string_view value, std::string_view expected) noexcept {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (NormalizeChunkSizeProfileCharacter(value[index]) != NormalizeChunkSizeProfileCharacter(expected[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr ChunkSizeProfileParseResult ParseChunkSizeProfileWithDiagnostics(std::string_view value) noexcept {
    if (value.empty()) {
        return ChunkSizeProfileParseResult{
            .error = ChunkSizeProfileParseError::Empty,
            .diagnostic = "chunk size is empty; expected one of 4KB, 8KB, 16KB, 32KB, 64KB, 128KB, 256KB, 512KB",
        };
    }
    if (ChunkSizeProfileTokenEquals(value, "4KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk4KB };
    }
    if (ChunkSizeProfileTokenEquals(value, "8KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk8KB };
    }
    if (ChunkSizeProfileTokenEquals(value, "16KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk16KB };
    }
    if (ChunkSizeProfileTokenEquals(value, "32KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk32KB };
    }
    if (ChunkSizeProfileTokenEquals(value, "64KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk64KB };
    }
    if (ChunkSizeProfileTokenEquals(value, "128KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk128KB };
    }
    if (ChunkSizeProfileTokenEquals(value, "256KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk256KB };
    }
    if (ChunkSizeProfileTokenEquals(value, "512KB")) {
        return ChunkSizeProfileParseResult{ .profile = ChunkSizeProfile::Chunk512KB };
    }
    return ChunkSizeProfileParseResult{
        .error = ChunkSizeProfileParseError::UnsupportedValue,
        .diagnostic = "unsupported chunk size; expected power-of-two payloads: 4KB, 8KB, 16KB, 32KB, 64KB, 128KB, 256KB, 512KB",
    };
}

[[nodiscard]] constexpr std::optional<ChunkSizeProfile> ParseChunkSizeProfile(std::string_view value) noexcept {
    const ChunkSizeProfileParseResult result = ParseChunkSizeProfileWithDiagnostics(value);
    if (!result.HasValue()) {
        return std::nullopt;
    }
    return result.profile;
}

} // namespace kb::ecs
