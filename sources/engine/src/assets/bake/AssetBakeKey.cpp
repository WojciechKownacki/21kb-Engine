#include "engine/assets/bake/AssetBakeKey.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace kb::assets::bake {
namespace {

// Bumping this moves every key in the store at once. It exists so the canonical
// encoding below can change (a new field, a different field order) without the
// new keys ever colliding with the old ones.
constexpr std::uint8_t kAssetBakeKeyEncodingVersion = 2U;

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
// Second lane: a different offset basis and a different odd multiplier, so the
// two 64-bit halves of a digest are not two views of one hash.
constexpr std::uint64_t kSecondaryOffsetBasis = 9241386435364257181ULL;
constexpr std::uint64_t kSecondaryPrime = 0x880355F21E6D1965ULL;
constexpr std::uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ULL;

// splitmix64 finalizer: turns the weak avalanche of a multiplicative hash into
// a well-mixed 64-bit value. Pure integer arithmetic, so it is identical on
// every platform.
[[nodiscard]] constexpr std::uint64_t Avalanche(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value;
}

[[nodiscard]] AssetBakeDigest DigestBytes(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t lowLane = kFnvOffsetBasis;
    std::uint64_t highLane = kSecondaryOffsetBasis;
    for (const std::uint8_t byte : bytes) {
        lowLane = (lowLane ^ byte) * kFnvPrime;
        highLane = (highLane ^ byte) * kSecondaryPrime;
    }
    const auto length = static_cast<std::uint64_t>(bytes.size());
    return AssetBakeDigest{
        .high = Avalanche(highLane + (length * kGoldenGamma)),
        .low = Avalanche(lowLane ^ length),
    };
}

[[nodiscard]] constexpr char ToLowerAscii(char character) noexcept {
    return (character >= 'A' && character <= 'Z') ? static_cast<char>(character - 'A' + 'a') : character;
}

[[nodiscard]] bool EqualsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
        if (ToLowerAscii(lhs[index]) != ToLowerAscii(rhs[index])) {
            return false;
        }
    }
    return true;
}

// Win32 refuses these as file names whatever extension follows them, and a
// package built on Linux must not be able to smuggle one onto Windows.
[[nodiscard]] bool IsReservedDeviceName(std::string_view stem) noexcept {
    constexpr std::array<std::string_view, 4U> kThreeLetterDevices{ "con", "prn", "aux", "nul" };
    for (const std::string_view device : kThreeLetterDevices) {
        if (EqualsIgnoreAsciiCase(stem, device)) {
            return true;
        }
    }
    if (stem.size() != 4U || stem[3] < '0' || stem[3] > '9') {
        return false;
    }
    const std::string_view prefix = stem.substr(0U, 3U);
    return EqualsIgnoreAsciiCase(prefix, "com") || EqualsIgnoreAsciiCase(prefix, "lpt");
}

} // namespace

std::string AssetBakeDigest::ToString() const {
    constexpr std::string_view kHexDigits = "0123456789abcdef";
    constexpr std::size_t kHalfDigits = 16U;
    std::string text(kHalfDigits * 2U, '0');
    for (std::size_t index = 0U; index < kHalfDigits; ++index) {
        const auto shift = static_cast<std::uint32_t>((kHalfDigits - 1U - index) * 4U);
        text[index] = kHexDigits[static_cast<std::size_t>((high >> shift) & 0xFULL)];
        text[kHalfDigits + index] = kHexDigits[static_cast<std::size_t>((low >> shift) & 0xFULL)];
    }
    return text;
}

std::uint64_t HashBakeBytes(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = kFnvOffsetBasis;
    for (const std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * kFnvPrime;
    }
    return Avalanche(hash ^ static_cast<std::uint64_t>(bytes.size()));
}

std::uint64_t HashBakeText(std::string_view text) noexcept {
    return HashBakeBytes(
        std::span<const std::uint8_t>{ reinterpret_cast<const std::uint8_t*>(text.data()), text.size() });
}

AssetBakeDigest HashBakeDigest(std::span<const std::uint8_t> bytes) noexcept {
    return DigestBytes(bytes);
}

bool IsValidBakeCacheName(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaxBakeCacheNameBytes) {
        return false;
    }
    for (const char character : name) {
        const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-';
        if (!allowed) {
            return false;
        }
    }
    // A leading '.' hides the entry on POSIX, a leading '-' reads as an option
    // to every command-line tool, and Win32 silently strips a trailing '.'.
    if (name.front() == '.' || name.front() == '-' || name.back() == '.') {
        return false;
    }
    return !IsReservedDeviceName(name.substr(0U, name.find('.')));
}

bool AssetBakeKey::IsValid() const noexcept {
    return IsValidBakeCacheName(bakerId) && !bakerVersion.empty() &&
        bakerVersion.size() <= kMaxBakeCacheNameBytes && IsValidBakeCacheName(targetProfileId) &&
        targetProfileHash != 0U;
}

AssetBakeDigest AssetBakeKey::Digest() const {
    // Canonicalise the dependency set before hashing it. Callers routinely
    // collect dependencies out of an unordered container, and without this the
    // key would depend on that container's traversal order -- which differs
    // between machines, runs and standard-library versions.
    std::vector<AssetBakeDigest> canonicalDependencies = dependencies;
    std::ranges::sort(canonicalDependencies);
    const auto duplicates = std::ranges::unique(canonicalDependencies);
    canonicalDependencies.erase(duplicates.begin(), duplicates.end());

    std::vector<std::uint8_t> stream;
    stream.reserve(64U + bakerId.size() + bakerVersion.size() + targetProfileId.size() +
        (canonicalDependencies.size() * (2U * sizeof(std::uint64_t))));

    // Every field is length-prefixed or fixed-width and written little-endian
    // byte by byte, so no two different keys can produce the same stream and no
    // two platforms can produce different streams for the same key.
    kb::scene::SceneAssetBinaryIO::WriteUInt8(stream, kAssetBakeKeyEncodingVersion);
    kb::scene::SceneAssetBinaryIO::WriteUInt64(stream, sourceContentHash);
    kb::scene::SceneAssetBinaryIO::WriteString(stream, bakerId);
    kb::scene::SceneAssetBinaryIO::WriteString(stream, bakerVersion);
    kb::scene::SceneAssetBinaryIO::WriteString(stream, targetProfileId);
    kb::scene::SceneAssetBinaryIO::WriteUInt64(stream, targetProfileHash);
    kb::scene::SceneAssetBinaryIO::WriteUInt64(stream, settingsHash);
    kb::scene::SceneAssetBinaryIO::WriteUInt32(stream, static_cast<std::uint32_t>(canonicalDependencies.size()));
    for (const AssetBakeDigest& dependency : canonicalDependencies) {
        kb::scene::SceneAssetBinaryIO::WriteUInt64(stream, dependency.high);
        kb::scene::SceneAssetBinaryIO::WriteUInt64(stream, dependency.low);
    }

    return DigestBytes(stream);
}

std::string AssetBakeKey::ToString() const {
    return Digest().ToString();
}

} // namespace kb::assets::bake
