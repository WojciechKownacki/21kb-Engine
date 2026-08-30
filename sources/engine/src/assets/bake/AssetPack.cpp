#include "engine/assets/bake/AssetPack.hpp"

#include "assets/bake/BakeStorePath.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace kb::assets::bake {
namespace {

// Byte offsets of the fixed header. Written out rather than derived from a struct: the header
// is a file format, and a compiler's idea of where a member sits is not.
constexpr std::size_t kHeaderMagicOffset = 0U;
constexpr std::size_t kHeaderFormatVersionOffset = 8U;
constexpr std::size_t kHeaderHeaderBytesOffset = 12U;
constexpr std::size_t kHeaderProfileHashOffset = 16U;
constexpr std::size_t kHeaderIndexOffsetOffset = 24U;
constexpr std::size_t kHeaderIndexBytesOffset = 32U;
constexpr std::size_t kHeaderIndexChecksumOffset = 40U;
constexpr std::size_t kHeaderArtifactCountOffset = 48U;
constexpr std::size_t kHeaderPackageAlignmentOffset = 52U;
constexpr std::size_t kHeaderMappedAlignmentOffset = 56U;
constexpr std::size_t kHeaderReservedWordOffset = 60U;
constexpr std::size_t kHeaderFileBytesOffset = 64U;
constexpr std::size_t kHeaderFragmentIndexOffsetOffset = 72U;
constexpr std::size_t kHeaderFragmentIndexBytesOffset = 80U;
constexpr std::size_t kHeaderFragmentCountOffset = 88U;
constexpr std::size_t kHeaderFragmentAlignmentOffset = 92U;
constexpr std::size_t kHeaderProfileIdLengthOffset = 96U;
constexpr std::size_t kHeaderProfileIdOffset = 97U;

void PutUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PutUInt64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PokeUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void PokeUInt64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::uint32_t PeekUInt32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t PeekUInt64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return value;
}

// Walks the index buffer. Every read is bounds-checked against what is left, because the
// buffer came off disk and may have been written by anything at all.
class IndexReader {
public:
    explicit IndexReader(std::span<const std::uint8_t> bytes) noexcept
        : bytes_{ bytes } {}

    [[nodiscard]] bool ReadUInt8(std::uint8_t& out) noexcept {
        if (Remaining() < 1U) {
            return false;
        }
        out = bytes_[offset_++];
        return true;
    }

    [[nodiscard]] bool ReadUInt32(std::uint32_t& out) noexcept {
        if (Remaining() < 4U) {
            return false;
        }
        out = PeekUInt32(bytes_, offset_);
        offset_ += 4U;
        return true;
    }

    [[nodiscard]] bool ReadUInt64(std::uint64_t& out) noexcept {
        if (Remaining() < 8U) {
            return false;
        }
        out = PeekUInt64(bytes_, offset_);
        offset_ += 8U;
        return true;
    }

    [[nodiscard]] bool ReadName(std::string& out) {
        std::uint8_t length = 0U;
        if (!ReadUInt8(length) || Remaining() < length) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        offset_ += length;
        return true;
    }

    [[nodiscard]] std::size_t Remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0U;
};

void PutFloat(std::vector<std::uint8_t>& bytes, float value) {
    PutUInt32(bytes, std::bit_cast<std::uint32_t>(value));
}

void PutName(std::vector<std::uint8_t>& bytes, std::string_view name) {
    bytes.push_back(static_cast<std::uint8_t>(name.size()));
    bytes.insert(bytes.end(), name.begin(), name.end());
}

} // namespace

std::string_view ToString(AssetPackReadStatus status) noexcept {
    switch (status) {
    case AssetPackReadStatus::Success:
        return "Success";
    case AssetPackReadStatus::Unreadable:
        return "Unreadable";
    case AssetPackReadStatus::NotAnAssetPack:
        return "NotAnAssetPack";
    case AssetPackReadStatus::UnsupportedVersion:
        return "UnsupportedVersion";
    case AssetPackReadStatus::HeaderCorrupt:
        return "HeaderCorrupt";
    case AssetPackReadStatus::SizeMismatch:
        return "SizeMismatch";
    case AssetPackReadStatus::IndexCorrupt:
        return "IndexCorrupt";
    case AssetPackReadStatus::FragmentIndexCorrupt:
        return "FragmentIndexCorrupt";
    case AssetPackReadStatus::BlockOutOfRange:
        return "BlockOutOfRange";
    case AssetPackReadStatus::BlockTooLarge:
        return "BlockTooLarge";
    case AssetPackReadStatus::PayloadCorrupt:
        return "PayloadCorrupt";
    case AssetPackReadStatus::PackTooLarge:
        return "PackTooLarge";
    case AssetPackReadStatus::ArtifactNotFound:
        return "ArtifactNotFound";
    case AssetPackReadStatus::BlockNotFound:
        return "BlockNotFound";
    case AssetPackReadStatus::NotMounted:
        return "NotMounted";
    }
    return "Unknown";
}

bool TryAlignAssetPackOffset(std::uint64_t value, std::uint32_t alignment, std::uint64_t& out) noexcept {
    if (!store::IsPowerOfTwo(alignment)) {
        return false;
    }
    const std::uint64_t mask = static_cast<std::uint64_t>(alignment) - 1U;
    const std::uint64_t padding = (alignment - static_cast<std::uint32_t>(value & mask)) & mask;
    if (value > std::numeric_limits<std::uint64_t>::max() - padding) {
        return false;
    }
    out = value + padding;
    return true;
}

std::vector<std::uint8_t> EncodeAssetPackIndex(std::span<const AssetPackArtifactEntry> artifacts) {
    std::vector<std::uint8_t> bytes;
    for (const AssetPackArtifactEntry& artifact : artifacts) {
        PutUInt64(bytes, artifact.key.high);
        PutUInt64(bytes, artifact.key.low);
        PutName(bytes, artifact.assetTypeId);
        PutUInt32(bytes, static_cast<std::uint32_t>(artifact.blocks.size()));
        for (const AssetPackBlockEntry& block : artifact.blocks) {
            PutName(bytes, block.name);
            bytes.push_back(static_cast<std::uint8_t>(block.residency));
            bytes.push_back(static_cast<std::uint8_t>(block.compression));
            // Reserved: keeps every fixed field of a block entry on its natural width and
            // leaves a byte for a per-block flag that does not force a format bump.
            bytes.push_back(0U);
            PutUInt32(bytes, block.alignmentBytes);
            PutUInt64(bytes, block.offset);
            PutUInt64(bytes, block.storedBytes);
            PutUInt64(bytes, block.uncompressedBytes);
            PutUInt64(bytes, block.payloadDigest.high);
            PutUInt64(bytes, block.payloadDigest.low);
        }
    }
    return bytes;
}

AssetPackReadStatus DecodeAssetPackIndex(
    std::span<const std::uint8_t> bytes,
    std::uint32_t artifactCount,
    std::vector<AssetPackArtifactEntry>& out) {
    // Every artifact entry costs at least this much of the index -- a 128-bit key, a
    // length-prefixed type name and a block count -- so a count the buffer could not possibly
    // hold is refused BEFORE it sizes a reservation. kMaxAssetPackIndexBytes bounds what a
    // hostile header can make a reader READ; on its own it does not bound what one can make a
    // reader ALLOCATE, because the artifact count is a 32-bit field of the header's own and a
    // count of 2^32-1 asks for hundreds of gigabytes here. The same guard the block count
    // below already had, for the same reason.
    constexpr std::size_t kMinArtifactEntryBytes = 8U + 8U + 1U + 4U;
    if (bytes.size() / kMinArtifactEntryBytes < artifactCount) {
        return AssetPackReadStatus::IndexCorrupt;
    }
    std::vector<AssetPackArtifactEntry> artifacts;
    artifacts.reserve(artifactCount);
    std::set<AssetBakeDigest> artifactKeys;

    IndexReader reader{ bytes };
    for (std::uint32_t artifactIndex = 0U; artifactIndex < artifactCount; ++artifactIndex) {
        AssetPackArtifactEntry artifact{};
        std::uint32_t blockCount = 0U;
        if (!reader.ReadUInt64(artifact.key.high) || !reader.ReadUInt64(artifact.key.low) ||
            !reader.ReadName(artifact.assetTypeId) || !reader.ReadUInt32(blockCount)) {
            return AssetPackReadStatus::IndexCorrupt;
        }
        if (!IsValidBakeCacheName(artifact.assetTypeId)) {
            return AssetPackReadStatus::IndexCorrupt;
        }
        // Every block entry costs at least this much, so a count that could not possibly fit
        // is refused before the reservation rather than after it.
        constexpr std::size_t kMinBlockEntryBytes =
            1U + 1U + 1U + 1U + 1U + 4U + 8U + 8U + 8U + 8U + 8U;
        if (reader.Remaining() / kMinBlockEntryBytes < blockCount) {
            return AssetPackReadStatus::IndexCorrupt;
        }
        artifact.blocks.reserve(blockCount);
        std::set<std::string> blockNames;
        for (std::uint32_t blockIndex = 0U; blockIndex < blockCount; ++blockIndex) {
            AssetPackBlockEntry block{};
            std::uint8_t residency = 0U;
            std::uint8_t compression = 0U;
            std::uint8_t reserved = 0U;
            if (!reader.ReadName(block.name) || !reader.ReadUInt8(residency) || !reader.ReadUInt8(compression) ||
                !reader.ReadUInt8(reserved) || !reader.ReadUInt32(block.alignmentBytes) ||
                !reader.ReadUInt64(block.offset) || !reader.ReadUInt64(block.storedBytes) ||
                !reader.ReadUInt64(block.uncompressedBytes) ||
                !reader.ReadUInt64(block.payloadDigest.high) ||
                !reader.ReadUInt64(block.payloadDigest.low)) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            if (!IsValidBakeCacheName(block.name) || reserved != 0U) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            if (residency > static_cast<std::uint8_t>(BakedAssetBlockResidency::Mapped)) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            // A method this build cannot undo must not be answered with the stored bytes; that
            // would hand a caller compressed data it thinks is a payload.
            if (compression != static_cast<std::uint8_t>(AssetPackBlockCompression::None)) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            if (block.storedBytes != block.uncompressedBytes) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            if (block.storedBytes == 0U || !store::IsPowerOfTwo(block.alignmentBytes)) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            if (block.payloadDigest.high == 0U && block.payloadDigest.low == 0U) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            block.residency = static_cast<BakedAssetBlockResidency>(residency);
            block.compression = static_cast<AssetPackBlockCompression>(compression);
            std::string canonicalBlockName = block.name;
            std::ranges::transform(canonicalBlockName, canonicalBlockName.begin(), [](char value) noexcept {
                return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
            });
            if (!blockNames.insert(std::move(canonicalBlockName)).second) {
                return AssetPackReadStatus::IndexCorrupt;
            }
            artifact.blocks.push_back(std::move(block));
        }
        // The sink protocol guarantees one; an artifact without it is not something this
        // engine wrote, and the runtime has no payload to load it from. This is also what
        // refuses a zero-block artifact: no blocks means no primary block either.
        const bool hasPrimary =
            std::ranges::any_of(artifact.blocks, [](const AssetPackBlockEntry& block) noexcept {
                return store::EqualsIgnoreAsciiCase(block.name, kBakedAssetPrimaryBlockName);
            });
        if (!hasPrimary) {
            return AssetPackReadStatus::IndexCorrupt;
        }
        if (!artifactKeys.insert(artifact.key).second) {
            return AssetPackReadStatus::IndexCorrupt;
        }
        artifacts.push_back(std::move(artifact));
    }
    // Trailing slack means the index and the count disagree about where the index ends, which
    // is the same corruption as a short read seen from the other side.
    if (reader.Remaining() != 0U) {
        return AssetPackReadStatus::IndexCorrupt;
    }

    out = std::move(artifacts);
    return AssetPackReadStatus::Success;
}

std::vector<std::uint8_t> EncodeAssetPackFragmentIndex(std::span<const AssetPackFragmentEntry> fragments) {
    if (fragments.size() > kMaxAssetPackFragmentIndexBytes / kAssetPackFragmentEntryBytes) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(fragments.size() * static_cast<std::size_t>(kAssetPackFragmentEntryBytes));
    for (const AssetPackFragmentEntry& fragment : fragments) {
        PutUInt64(bytes, fragment.offset);
        PutUInt64(bytes, fragment.bytes);
        PutUInt32(bytes, fragment.clusterCount);
        // Reserved: keeps the box on an eight-byte boundary and leaves a word for the
        // compression the format already promises a fragment may gain without a re-bake.
        PutUInt32(bytes, 0U);
        for (const float value : fragment.boundsMin) {
            PutFloat(bytes, value);
        }
        for (const float value : fragment.boundsMax) {
            PutFloat(bytes, value);
        }
    }
    return bytes;
}

std::uint64_t AssetPackIndexChecksum(
    std::span<const std::uint8_t> artifactIndex,
    std::span<const std::uint8_t> fragmentIndex) {
    if (fragmentIndex.empty()) {
        return HashBakeBytes(artifactIndex);
    }
    std::vector<std::uint8_t> catalog;
    catalog.reserve(artifactIndex.size() + fragmentIndex.size());
    catalog.insert(catalog.end(), artifactIndex.begin(), artifactIndex.end());
    catalog.insert(catalog.end(), fragmentIndex.begin(), fragmentIndex.end());
    return HashBakeBytes(catalog);
}

AssetPackReadStatus DecodeAssetPackFragmentIndex(
    std::span<const std::uint8_t> bytes,
    std::uint32_t fragmentCount,
    std::vector<AssetPackFragmentEntry>& out) {
    // Fixed width, so the length is an exact statement rather than a ceiling: a buffer that is
    // not the count's length disagrees with the header about where the fragment index ends.
    const std::uint64_t expectedBytes = static_cast<std::uint64_t>(fragmentCount) * kAssetPackFragmentEntryBytes;
    if (expectedBytes > kMaxAssetPackFragmentIndexBytes ||
        expectedBytes > std::numeric_limits<std::size_t>::max() || bytes.size() != expectedBytes) {
        return AssetPackReadStatus::FragmentIndexCorrupt;
    }
    std::vector<AssetPackFragmentEntry> fragments;
    fragments.reserve(fragmentCount);

    IndexReader reader{ bytes };
    for (std::uint32_t index = 0U; index < fragmentCount; ++index) {
        AssetPackFragmentEntry fragment{};
        std::uint32_t reserved = 0U;
        if (!reader.ReadUInt64(fragment.offset) || !reader.ReadUInt64(fragment.bytes) ||
            !reader.ReadUInt32(fragment.clusterCount) || !reader.ReadUInt32(reserved)) {
            return AssetPackReadStatus::FragmentIndexCorrupt;
        }
        if (reserved != 0U || fragment.clusterCount == 0U || fragment.bytes == 0U) {
            return AssetPackReadStatus::FragmentIndexCorrupt;
        }
        for (std::size_t axis = 0U; axis < fragment.boundsMin.size(); ++axis) {
            std::uint32_t low = 0U;
            if (!reader.ReadUInt32(low)) {
                return AssetPackReadStatus::FragmentIndexCorrupt;
            }
            fragment.boundsMin[axis] = std::bit_cast<float>(low);
        }
        for (std::size_t axis = 0U; axis < fragment.boundsMax.size(); ++axis) {
            std::uint32_t high = 0U;
            if (!reader.ReadUInt32(high)) {
                return AssetPackReadStatus::FragmentIndexCorrupt;
            }
            fragment.boundsMax[axis] = std::bit_cast<float>(high);
        }
        for (std::size_t axis = 0U; axis < fragment.boundsMin.size(); ++axis) {
            const float low = fragment.boundsMin[axis];
            const float high = fragment.boundsMax[axis];
            // NaN fails every comparison, so the ordering test refuses it too; the finiteness
            // test is separate because an infinite edge orders correctly and still makes every
            // distance to the fragment infinite.
            if (!std::isfinite(low) || !std::isfinite(high) || !(low <= high)) {
                return AssetPackReadStatus::FragmentIndexCorrupt;
            }
        }
        fragments.push_back(fragment);
    }

    out = std::move(fragments);
    return AssetPackReadStatus::Success;
}

std::vector<std::uint8_t> EncodeAssetPackHeader(const AssetPackHeader& header) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kAssetPackHeaderBytes), 0U);
    std::memcpy(bytes.data() + kHeaderMagicOffset, kAssetPackMagic.data(), kAssetPackMagic.size());
    PokeUInt32(bytes, kHeaderFormatVersionOffset, header.formatVersion);
    PokeUInt32(bytes, kHeaderHeaderBytesOffset, header.headerBytes);
    PokeUInt64(bytes, kHeaderProfileHashOffset, header.targetProfileHash);
    PokeUInt64(bytes, kHeaderIndexOffsetOffset, header.indexOffset);
    PokeUInt64(bytes, kHeaderIndexBytesOffset, header.indexBytes);
    PokeUInt64(bytes, kHeaderIndexChecksumOffset, header.indexChecksum);
    PokeUInt32(bytes, kHeaderArtifactCountOffset, header.artifactCount);
    PokeUInt32(bytes, kHeaderPackageAlignmentOffset, header.packageBlockAlignmentBytes);
    PokeUInt32(bytes, kHeaderMappedAlignmentOffset, header.mappedBlockAlignmentBytes);
    PokeUInt64(bytes, kHeaderFileBytesOffset, header.fileBytes);
    PokeUInt64(bytes, kHeaderFragmentIndexOffsetOffset, header.fragmentIndexOffset);
    PokeUInt64(bytes, kHeaderFragmentIndexBytesOffset, header.fragmentIndexBytes);
    PokeUInt32(bytes, kHeaderFragmentCountOffset, header.fragmentCount);
    PokeUInt32(bytes, kHeaderFragmentAlignmentOffset, header.fragmentAlignmentBytes);
    const std::size_t profileIdLength = std::min<std::size_t>(header.targetProfileId.size(), kMaxBakeCacheNameBytes);
    bytes[kHeaderProfileIdLengthOffset] = static_cast<std::uint8_t>(profileIdLength);
    std::memcpy(bytes.data() + kHeaderProfileIdOffset, header.targetProfileId.data(), profileIdLength);
    return bytes;
}

AssetPackReadStatus DecodeAssetPackHeader(std::span<const std::uint8_t> bytes, AssetPackHeader& out) {
    if (bytes.size() < kAssetPackHeaderBytes) {
        return AssetPackReadStatus::Unreadable;
    }
    if (std::memcmp(bytes.data() + kHeaderMagicOffset, kAssetPackMagic.data(), kAssetPackMagic.size()) != 0) {
        return AssetPackReadStatus::NotAnAssetPack;
    }

    AssetPackHeader header{};
    header.formatVersion = PeekUInt32(bytes, kHeaderFormatVersionOffset);
    if (header.formatVersion != kAssetPackFormatVersion) {
        return AssetPackReadStatus::UnsupportedVersion;
    }
    header.headerBytes = PeekUInt32(bytes, kHeaderHeaderBytesOffset);
    header.targetProfileHash = PeekUInt64(bytes, kHeaderProfileHashOffset);
    header.indexOffset = PeekUInt64(bytes, kHeaderIndexOffsetOffset);
    header.indexBytes = PeekUInt64(bytes, kHeaderIndexBytesOffset);
    header.indexChecksum = PeekUInt64(bytes, kHeaderIndexChecksumOffset);
    header.artifactCount = PeekUInt32(bytes, kHeaderArtifactCountOffset);
    header.packageBlockAlignmentBytes = PeekUInt32(bytes, kHeaderPackageAlignmentOffset);
    header.mappedBlockAlignmentBytes = PeekUInt32(bytes, kHeaderMappedAlignmentOffset);
    header.fileBytes = PeekUInt64(bytes, kHeaderFileBytesOffset);
    header.fragmentIndexOffset = PeekUInt64(bytes, kHeaderFragmentIndexOffsetOffset);
    header.fragmentIndexBytes = PeekUInt64(bytes, kHeaderFragmentIndexBytesOffset);
    header.fragmentCount = PeekUInt32(bytes, kHeaderFragmentCountOffset);
    header.fragmentAlignmentBytes = PeekUInt32(bytes, kHeaderFragmentAlignmentOffset);

    const std::uint8_t profileIdLength = bytes[kHeaderProfileIdLengthOffset];
    if (profileIdLength > kMaxBakeCacheNameBytes) {
        return AssetPackReadStatus::HeaderCorrupt;
    }
    header.targetProfileId.assign(
        reinterpret_cast<const char*>(bytes.data() + kHeaderProfileIdOffset), profileIdLength);
    if (!IsValidBakeCacheName(header.targetProfileId)) {
        return AssetPackReadStatus::HeaderCorrupt;
    }
    // Nothing but a zero fingerprint would let a pack claim a profile whose content nobody can
    // invalidate, which is the defect AssetBakeKey::targetProfileHash exists to close.
    if (header.targetProfileHash == 0U) {
        return AssetPackReadStatus::HeaderCorrupt;
    }
    if (header.headerBytes != kAssetPackHeaderBytes || header.indexOffset != kAssetPackHeaderBytes) {
        return AssetPackReadStatus::HeaderCorrupt;
    }
    if (!store::IsPowerOfTwo(header.packageBlockAlignmentBytes) ||
        !store::IsPowerOfTwo(header.mappedBlockAlignmentBytes) ||
        header.mappedBlockAlignmentBytes % header.packageBlockAlignmentBytes != 0U) {
        return AssetPackReadStatus::HeaderCorrupt;
    }
    // The four fragment fields describe one thing, so they are all set or all clear; a pack
    // that fills some of them was written by something that knows a layout this build does not.
    const bool hasFragments = header.fragmentCount != 0U;
    if (!hasFragments) {
        if (header.fragmentIndexOffset != 0U || header.fragmentIndexBytes != 0U ||
            header.fragmentAlignmentBytes != 0U) {
            return AssetPackReadStatus::HeaderCorrupt;
        }
    } else {
        // The format puts the fragment index immediately after the artifact index. It is not a
        // free offset, so a fragment index found anywhere else is not this layout.
        if (header.fragmentIndexOffset != header.indexOffset + header.indexBytes) {
            return AssetPackReadStatus::FragmentIndexCorrupt;
        }
        if (header.fragmentIndexBytes !=
            static_cast<std::uint64_t>(header.fragmentCount) * kAssetPackFragmentEntryBytes) {
            return AssetPackReadStatus::FragmentIndexCorrupt;
        }
        if (header.fragmentIndexBytes > kMaxAssetPackFragmentIndexBytes) {
            return AssetPackReadStatus::FragmentIndexCorrupt;
        }
        // A fragment is a block, and a block already sits at the package alignment, so nothing
        // finer can be claimed for one.
        if (!store::IsPowerOfTwo(header.fragmentAlignmentBytes) ||
            header.fragmentAlignmentBytes % header.packageBlockAlignmentBytes != 0U) {
            return AssetPackReadStatus::FragmentIndexCorrupt;
        }
    }
    if (PeekUInt32(bytes, kHeaderReservedWordOffset) != 0U) {
        return AssetPackReadStatus::HeaderCorrupt;
    }
    if (header.indexBytes > kMaxAssetPackIndexBytes) {
        return AssetPackReadStatus::IndexCorrupt;
    }
    if (header.fileBytes > kMaxAssetPackBytes) {
        return AssetPackReadStatus::PackTooLarge;
    }
    if (header.fileBytes < kAssetPackHeaderBytes + header.indexBytes) {
        return AssetPackReadStatus::IndexCorrupt;
    }
    if (header.fileBytes - kAssetPackHeaderBytes - header.indexBytes < header.fragmentIndexBytes) {
        return AssetPackReadStatus::FragmentIndexCorrupt;
    }

    out = std::move(header);
    return AssetPackReadStatus::Success;
}

} // namespace kb::assets::bake
