#pragma once

#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/BakedAssetSink.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// On-disk layout of a baked asset package: the release-mode counterpart of the loose bake
// store, written by AssetPackWriter and mounted by AssetPackReader. This header describes the
// FORMAT and nothing else, so both sides encode and decode the index through the same code.
//
//     +0        fixed header, kAssetPackHeaderBytes long
//     +256      index: every artifact, its blocks, and where each block lies
//     ...       block payloads, each at an offset that satisfies its alignment
//
// THE INDEX IS AT THE FRONT, which is a deliberate departure from the usual trailing-directory
// container. The reason is the browser: one HTTP range request against the head of the file
// yields the whole catalogue, whereas a trailing index forces a round trip to find out where
// the trailing index is. A memory mapping is indifferent to which end the index sits at, so
// nothing is lost on the mobile side to gain that.
//
// Five rules the format exists to keep:
//
//  1. COMPRESSION IS PER BLOCK, never per file. RFC 9110 (14.1.2) counts a byte range against
//     the bytes AFTER content coding, so a host that puts gzip or br on the container makes
//     every offset in it unaddressable. The compression method therefore lives in the index,
//     one field per block, and the container itself must be served and stored uncompressed.
//     The method is recorded even though this stage implements none, because adding one later
//     must not mean re-baking content.
//  2. EVERY BLOCK IS ALIGNED to the target profile's packageBlockAlignmentBytes (256 in every
//     shipped profile). Vulkan's required-limits table caps the uniform, storage and texel
//     buffer offset alignments at 256, so a 256-aligned block offset is a legal buffer offset
//     on any conformant device; WebGPU independently requires a buffer-to-texture copy's
//     bytesPerRow to be a multiple of 256.
//  3. A MAPPED BLOCK IS ALIGNED FURTHER, to mappedBlockAlignmentBytes -- the granularity the
//     platform's mapping call accepts as a file offset (64 KiB for MapViewOfFile, 16 KiB on a
//     16 KiB-page Android device). Residency Mapped is a placement hint, not a promise that a
//     mapping will exist: Emscripten's mmap copies, so a reader must never assume it mapped.
//  4. A BLOCK HAS A CEILING (kMaxAssetPackBlockBytes) so that one range request fits a wasm32
//     budget, and the pack itself has one (kMaxAssetPackBytes) so that "read the whole file"
//     is a budgeted fallback rather than an assumption.
//  5. THE STREAMING FRAGMENT BOUNDARY HAS ITS FIELDS FROM DAY ONE. A cluster group may not
//     straddle a page, offsets inside a page are relative to it, and load priority is computed
//     from the page's bounds -- all of which the baker decides. Adding that later would mean
//     re-baking every asset, so the header carries the fragment index's offset, size, count and
//     alignment now. They are zero in every pack this stage writes; nothing implements
//     clustering yet.
namespace kb::assets::bake {

// First bytes of every pack. Eight bytes, no terminator.
inline constexpr std::string_view kAssetPackMagic = "21KBPACK";

// Bumped whenever the layout below stops being readable by the previous reader.
inline constexpr std::uint32_t kAssetPackFormatVersion = 1U;

// The fixed header occupies a whole alignment unit, so the index starts at a block-aligned
// offset like everything else in the file.
inline constexpr std::uint64_t kAssetPackHeaderBytes = 256U;

// Extension of a published pack.
inline constexpr std::string_view kAssetPackFileExtension = ".kbpack";

// How one block's bytes are stored. Recorded per block in the index; see rule 1 above. Only
// None is implemented, and a reader must refuse a method it does not know rather than hand
// back bytes it did not decode.
enum class AssetPackBlockCompression : std::uint8_t {
    // Stored as written. storedBytes == uncompressedBytes.
    None = 0U,
};

// Largest single block a pack may hold. It is the guaranteed floor of Vulkan's
// maxStorageBufferRange (2^27), so a block can always be bound as one storage buffer, and it
// is also a bounded fraction of a wasm32 heap: Emscripten's MAXIMUM_MEMORY defaults to 2 GB
// and growing the heap COPIES it, so a block that arrives as one range response is resident
// twice at the worst moment.
inline constexpr std::uint64_t kMaxAssetPackBlockBytes = 128ULL * 1024ULL * 1024ULL;

// Largest pack a reader will mount. 1.5 GB is the ceiling for a unit that has to be shippable
// as an install-time asset pack, and it is what makes the mandatory "fetch the whole file"
// fallback a budget rather than a hope.
inline constexpr std::uint64_t kMaxAssetPackBytes = 1536ULL * 1024ULL * 1024ULL;

// Ceiling on the index itself, so a hostile header cannot make a reader allocate at will
// before a single byte of it has been validated. At the encoding below this is room for
// something over a million blocks -- far past any pack that fits kMaxAssetPackBytes.
inline constexpr std::uint64_t kMaxAssetPackIndexBytes = 64ULL * 1024ULL * 1024ULL;

// Where one block's bytes are, and what they are. Offsets and sizes are absolute file
// positions and byte counts; nothing in a pack is relative to anything but the file.
struct AssetPackBlockEntry {
    std::string name;
    BakedAssetBlockResidency residency = BakedAssetBlockResidency::Resident;
    AssetPackBlockCompression compression = AssetPackBlockCompression::None;
    // The alignment the block was actually placed at, which is at least the profile's package
    // alignment and, for a Mapped block, at least its mapped alignment.
    std::uint32_t alignmentBytes = 1U;
    std::uint64_t offset = 0U;
    std::uint64_t storedBytes = 0U;
    std::uint64_t uncompressedBytes = 0U;
};

struct AssetPackArtifactEntry {
    AssetBakeDigest key{};
    std::string assetTypeId;
    std::vector<AssetPackBlockEntry> blocks;
};

struct AssetPackHeader {
    std::uint32_t formatVersion = kAssetPackFormatVersion;
    std::uint32_t headerBytes = static_cast<std::uint32_t>(kAssetPackHeaderBytes);
    // BakeTargetProfile::identifier and BakeTargetProfileFingerprint of the profile this pack
    // was baked for. The name alone does not change when the profile behind it does, which is
    // exactly why the key carries the fingerprint too.
    std::string targetProfileId;
    std::uint64_t targetProfileHash = 0U;
    std::uint64_t indexOffset = kAssetPackHeaderBytes;
    std::uint64_t indexBytes = 0U;
    // HashBakeBytes over the index region. An index that disagrees with the file it describes
    // is the one corruption a reader cannot detect from the offsets alone.
    std::uint64_t indexChecksum = 0U;
    std::uint32_t artifactCount = 0U;
    std::uint32_t packageBlockAlignmentBytes = 0U;
    std::uint32_t mappedBlockAlignmentBytes = 0U;
    // Total length of the pack. Recorded so a truncated or extended file is refused without
    // the reader having to trust its own stat against a length it never wrote down.
    std::uint64_t fileBytes = 0U;
    // Reserved for the streaming fragment boundary (rule 5). All zero until a cluster baker
    // exists; a reader refuses a pack that claims a fragment index it cannot read.
    std::uint64_t fragmentIndexOffset = 0U;
    std::uint64_t fragmentIndexBytes = 0U;
    std::uint32_t fragmentCount = 0U;
    std::uint32_t fragmentAlignmentBytes = 0U;
};

enum class AssetPackReadStatus : std::uint8_t {
    Success,
    // The file could not be opened, or a read against it came up short.
    Unreadable,
    // The first bytes are not kAssetPackMagic.
    NotAnAssetPack,
    // A format version this reader does not implement.
    UnsupportedVersion,
    // The fixed header is internally inconsistent (wrong header size, an index that does not
    // start where the format puts it, a reserved field claiming something unimplemented).
    HeaderCorrupt,
    // The file is not as long as its own header says.
    SizeMismatch,
    // The index does not decode, does not checksum, or describes an artifact that cannot be.
    IndexCorrupt,
    // A block's offset or length runs past the end of the file, or the two overflow together.
    BlockOutOfRange,
    // A block claims more than kMaxAssetPackBlockBytes.
    BlockTooLarge,
    // The pack claims more than kMaxAssetPackBytes.
    PackTooLarge,
    // Nothing in this pack carries that key.
    ArtifactNotFound,
    // The artifact carries no block by that name.
    BlockNotFound,
    // Nothing is mounted.
    NotMounted,
};

[[nodiscard]] std::string_view ToString(AssetPackReadStatus status) noexcept;

// Serialises the index region. Every field is little-endian and every name is length
// prefixed, so the encoding is byte-identical on every compiler, CPU and endianness we
// target. Block offsets are written as fixed-width fields, which is what lets the writer
// measure the index before it knows where the blocks will land.
[[nodiscard]] std::vector<std::uint8_t> EncodeAssetPackIndex(std::span<const AssetPackArtifactEntry> artifacts);

// The exact inverse, with every length checked against what is left of the buffer. Refuses an
// index with trailing slack, a duplicate key, a duplicate block name, an artifact with no
// blocks or no primary block, a name that is not a portable bake-cache name, or an unknown
// compression method -- all before any offset is used to reach into the file.
[[nodiscard]] AssetPackReadStatus DecodeAssetPackIndex(
    std::span<const std::uint8_t> bytes,
    std::uint32_t artifactCount,
    std::vector<AssetPackArtifactEntry>& out);

// The fixed header, written into exactly kAssetPackHeaderBytes bytes.
[[nodiscard]] std::vector<std::uint8_t> EncodeAssetPackHeader(const AssetPackHeader& header);

// Reads it back and rejects everything the format forbids on sight, so a caller never acts on
// a header field it has not validated.
[[nodiscard]] AssetPackReadStatus DecodeAssetPackHeader(std::span<const std::uint8_t> bytes, AssetPackHeader& out);

// Rounds `value` up to the next multiple of `alignment`, which must be a power of two.
// Returns false rather than wrapping: a hostile index can name an offset one below 2^64.
[[nodiscard]] bool TryAlignAssetPackOffset(std::uint64_t value, std::uint32_t alignment, std::uint64_t& out) noexcept;

} // namespace kb::assets::bake
