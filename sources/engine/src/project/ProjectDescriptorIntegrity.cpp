#include "project/ProjectDescriptorIntegrity.hpp"

#include <array>
#include <fstream>

namespace kb::project {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320U;

[[nodiscard]] std::array<std::uint32_t, 256> BuildCrc32Table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0U; index < 256U; ++index) {
        std::uint32_t value = index;
        for (std::uint32_t bit = 0U; bit < 8U; ++bit) {
            value = (value & 1U) != 0U ? (kCrc32Polynomial ^ (value >> 1U)) : (value >> 1U);
        }
        table[index] = value;
    }
    return table;
}

[[nodiscard]] std::uint32_t UpdateCrc32(std::uint32_t crc, unsigned char value) noexcept {
    static const std::array<std::uint32_t, 256> table = BuildCrc32Table();
    return table[(crc ^ value) & 0xFFU] ^ (crc >> 8U);
}

} // namespace

ProjectDescriptorIntegrity ProjectDescriptorIntegrityService::ComputeFile(const std::filesystem::path& path) noexcept {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        return {};
    }

    ProjectDescriptorIntegrity integrity{
        .byteSize = 0U,
        .contentHashFnv1a64 = kFnvOffset,
        .contentChecksumCrc32 = 0xFFFFFFFFU,
    };
    char value = 0;
    while (input.get(value)) {
        const unsigned char byte = static_cast<unsigned char>(value);
        ++integrity.byteSize;
        integrity.contentHashFnv1a64 ^= byte;
        integrity.contentHashFnv1a64 *= kFnvPrime;
        integrity.contentChecksumCrc32 = UpdateCrc32(integrity.contentChecksumCrc32, byte);
    }
    integrity.contentChecksumCrc32 ^= 0xFFFFFFFFU;
    if (integrity.contentHashFnv1a64 == 0U) {
        integrity.contentHashFnv1a64 = kFnvPrime;
    }
    return integrity;
}

} // namespace kb::project
