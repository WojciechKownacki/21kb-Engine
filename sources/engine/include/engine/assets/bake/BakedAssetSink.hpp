#pragma once

#include "engine/assets/bake/AssetBakeKey.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets::bake {

// Reserved name of the one always-resident block every baked artifact has.
inline constexpr std::string_view kBakedAssetPrimaryBlockName = "primary";

// File extension a loose-mode block is written with.
inline constexpr std::string_view kBakedAssetBlockExtension = ".kbblock";

// Longest path, in native path characters, the loose sink will ever create.
// Linux PATH_MAX is 4096 counting the terminator and Win32 accepts 32767 for an
// extended-length path, so 4095 is the portable ceiling. It is checked against
// the WORST case (the store root plus a block name at kMaxBakeCacheNameBytes)
// before the sink creates a single directory: a store root that does not fit is
// refused with PathTooLong rather than half-written into a place no reader can
// open again.
inline constexpr std::size_t kMaxBakeStorePathLength = 4095U;

// How long a staging directory may sit untouched before the next bake treats it
// as debris left by a killed process and removes it. Every block write
// refreshes its containing directory's modification time, so a live bake never
// goes this long without touching its own staging directory.
inline constexpr std::chrono::hours kOrphanedStagingMaxAge{ 24 };

// How the runtime is expected to get at a block. A sink that packs artifacts
// into a container uses this to decide placement; it is descriptive metadata,
// never a promise that the sink itself streams or maps anything.
enum class BakedAssetBlockResidency : std::uint8_t {
    // Read in full together with the asset.
    Resident,
    // Read on demand, after the asset is already usable (mip tail, high LOD,
    // the body of a long audio clip).
    Streaming,
    // Read through a memory mapping. `alignmentBytes` must then be the target
    // profile's mappedBlockAlignmentBytes, not its packageBlockAlignmentBytes.
    Mapped,
};

// Declares the block to be one streaming fragment: a self-contained run of whole
// cluster groups whose every internal offset is relative to the block's first byte.
//
// A group may not straddle a fragment, so the baker -- the only party that knows
// where a group ends -- states the boundary here rather than leaving a container
// to cut the bytes wherever they happen to fall. The bounds are the world-space
// box of the geometry in the fragment, which is what a loading priority is
// computed from; the count says how many clusters the group contains.
struct BakedAssetBlockFragment {
    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    std::uint32_t clusterCount = 0U;
};

// A fragment declaration a sink may act on: a real group count and a box that is
// finite and not inside out. Checked by both sinks, because an artifact must be
// describable through either of them.
[[nodiscard]] bool IsValidBakedAssetBlockFragment(const BakedAssetBlockFragment& fragment) noexcept;

struct BakedAssetBlock {
    // Unique within the artifact and a portable file name
    // (IsValidBakeCacheName). Compared case-insensitively against the other
    // blocks so a pair of names that only differ in case cannot collide once
    // the artifact reaches a case-insensitive file system.
    std::string_view name{};
    BakedAssetBlockResidency residency = BakedAssetBlockResidency::Resident;
    // Placement alignment the block needs inside a container, from the target
    // profile. Must be a power of two. A loose sink gives every block its own
    // file, whose first byte satisfies any alignment, so it validates the value
    // and does not otherwise act on it.
    std::uint32_t alignmentBytes = 1U;
    // Set when the block is a streaming fragment. A container sink builds the
    // pack's fragment index out of these; a loose sink places nothing, so it
    // validates the declaration and does not otherwise act on it, exactly as it
    // treats `alignmentBytes`.
    std::optional<BakedAssetBlockFragment> fragment{};
};

struct BakedAssetDescriptor {
    AssetBakeKey key;
    // Runtime type the artifact loads as ("SkeletalMesh", "Texture2D", ...).
    // Becomes a path component, so IsValidBakeCacheName.
    std::string assetTypeId;
};

enum class BakedAssetSinkStatus : std::uint8_t {
    Success,
    InvalidKey,
    InvalidAssetType,
    InvalidBlockName,
    InvalidAlignment,
    EmptyBlock,
    AssetAlreadyOpen,
    NoAssetOpen,
    DuplicateBlock,
    MissingPrimaryBlock,
    // The store root is so long that a block path built under it would pass
    // kMaxBakeStorePathLength. Reported by BeginAsset, before anything is
    // created, because a path that cannot be opened cannot be repaired either.
    PathTooLong,
    WriteFailed,
    // The target profile a container sink was built for is not bakeable
    // (IsValidBakeTargetProfile said no). A loose sink never reports it: it
    // stores no profile, because it places nothing.
    InvalidProfile,
    // The block is larger than a container may hold in one piece
    // (kMaxAssetPackBlockBytes). Reported before a byte of it is written.
    BlockTooLarge,
    // A container sink was asked to take more work after it published its
    // package. Only a container sink reports it; a loose sink publishes per
    // artifact and is never finished.
    PackAlreadyFinished,
    // Something else took the staging files this container sink was building
    // its package in. The staging names are derived from the destination, so a
    // second writer aimed at the same package truncates them -- and the bytes
    // that come back at assembly time are then somebody else's. Reported
    // instead of publishing a package whose blocks do not belong to the keys
    // that name them; the destination is left untouched.
    StagingConflict,
    // The block declares itself a streaming fragment with no cluster groups in
    // it, or with a box that is not finite or is inside out. A fragment index
    // built from that describes a page nothing can be prioritised against.
    InvalidFragment,
};

[[nodiscard]] std::string_view ToString(BakedAssetSinkStatus status) noexcept;

// The seam of the whole bake pipeline: a baker never writes a file, it hands
// finished bytes to a sink. The sink owns the layout, the naming and the
// publication, which is what lets a later stage swap the loose developer sink
// below for a container writer without a single baker changing.
//
// Protocol, per artifact:
//   BeginAsset -> WritePrimaryBlock (exactly once)
//              -> WriteAuxiliaryBlock (zero or more, distinct names)
//              -> CommitAsset | AbortAsset
// Exactly one artifact is open at a time. Every call returns a status; nothing
// fails silently, and a failed CommitAsset leaves the artifact open so the
// caller can decide between a retry and an abort. AbortAsset must leave the
// store exactly as it was before BeginAsset, and an implementation must abort
// an artifact left open at destruction.
class IBakedAssetSink {
public:
    IBakedAssetSink() = default;
    IBakedAssetSink(const IBakedAssetSink&) = delete;
    IBakedAssetSink& operator=(const IBakedAssetSink&) = delete;
    IBakedAssetSink(IBakedAssetSink&&) = delete;
    IBakedAssetSink& operator=(IBakedAssetSink&&) = delete;
    virtual ~IBakedAssetSink() = default;

    [[nodiscard]] virtual BakedAssetSinkStatus BeginAsset(const BakedAssetDescriptor& descriptor) = 0;

    // The always-resident payload. `alignmentBytes` is the target profile's
    // packageBlockAlignmentBytes (or mappedBlockAlignmentBytes for an artifact
    // the runtime maps) and must be a power of two.
    [[nodiscard]] virtual BakedAssetSinkStatus WritePrimaryBlock(std::span<const std::uint8_t> bytes,
                                                                 std::uint32_t alignmentBytes) = 0;

    [[nodiscard]] virtual BakedAssetSinkStatus WriteAuxiliaryBlock(const BakedAssetBlock& block,
                                                                   std::span<const std::uint8_t> bytes) = 0;

    // Publishes the artifact. It is readable only after this returns Success.
    [[nodiscard]] virtual BakedAssetSinkStatus CommitAsset() = 0;

    // Discards everything written since BeginAsset. Safe to call with no
    // artifact open.
    virtual void AbortAsset() noexcept = 0;
};

// Developer-mode sink: one directory per artifact, one file per block, under
//
//     <root>/<assetTypeId>/<targetProfileId>/<key digest>/<block>.kbblock
//
// The digest directory makes the store content-addressed, so two bakes of the
// same key name the same place and a stale artifact is simply never looked up
// again. Blocks are staged in a hidden sibling directory and published by a
// single rename of that directory, so a reader never observes a half-written
// artifact and an abort leaves nothing behind.
//
// Three consequences of that mechanism are contract, not incidental behaviour:
//
//  * Publication MOVES the staging directory itself. Everything it holds
//    arrives at the destination in one file-system operation; nothing is
//    published block by block, because that would make a half-published
//    artifact observable under a key that claims to be complete.
//  * Only a COMPLETE artifact at the destination counts as already published.
//    An empty directory, a directory missing a block, or a plain file sitting
//    on the path is debris, not an artifact this sink ever wrote, and it is
//    cleared and replaced instead of being taken at face value -- otherwise a
//    content-addressed key would stay poisoned for good.
//  * A process KILLED between BeginAsset and CommitAsset never runs its abort,
//    so it does leave its staging directory behind. The next bake sweeps every
//    staging entry older than kOrphanedStagingMaxAge and removes the hidden
//    staging root as soon as it runs empty, so the debris is bounded and
//    self-clearing rather than permanent.
//
// On Windows the store root is normalised to an extended-length path, because a
// block path built from names at kMaxBakeCacheNameBytes passes Win32's MAX_PATH
// long before it reaches kMaxBakeStorePathLength.
class LooseBakedAssetSink final : public IBakedAssetSink {
public:
    explicit LooseBakedAssetSink(std::filesystem::path root);
    ~LooseBakedAssetSink() override;

    [[nodiscard]] BakedAssetSinkStatus BeginAsset(const BakedAssetDescriptor& descriptor) override;
    [[nodiscard]] BakedAssetSinkStatus WritePrimaryBlock(std::span<const std::uint8_t> bytes,
                                                         std::uint32_t alignmentBytes) override;
    [[nodiscard]] BakedAssetSinkStatus WriteAuxiliaryBlock(const BakedAssetBlock& block,
                                                           std::span<const std::uint8_t> bytes) override;
    [[nodiscard]] BakedAssetSinkStatus CommitAsset() override;
    void AbortAsset() noexcept override;

    // Where a committed artifact lives. Pure path arithmetic: it touches no
    // file system and says nothing about whether the artifact exists. Returns
    // an empty path for a descriptor this sink would refuse -- an invalid key,
    // an asset type that is not a portable name, or a path whose worst-case
    // block would pass kMaxBakeStorePathLength.
    [[nodiscard]] std::filesystem::path AssetDirectory(const BakedAssetDescriptor& descriptor) const;

    // Where one committed block of that artifact lives. Empty for a name this
    // sink would refuse.
    [[nodiscard]] std::filesystem::path BlockPath(const BakedAssetDescriptor& descriptor,
                                                  std::string_view blockName) const;

private:
    void DiscardStaging() noexcept;

    std::filesystem::path root_;
    std::filesystem::path stagingDirectory_;
    std::filesystem::path publishDirectory_;
    std::vector<std::string> writtenBlocks_;
    std::uint64_t stagingCounter_ = 0U;
    bool open_ = false;
    bool primaryWritten_ = false;
};

} // namespace kb::assets::bake
