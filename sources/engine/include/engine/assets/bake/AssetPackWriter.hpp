#pragma once

#include "engine/assets/bake/AssetPack.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/assets/bake/BakedAssetSink.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace kb::assets::bake {

// Release-mode sink: the SAME contract as LooseBakedAssetSink, a different output. Every
// artifact a baker hands over lands in one container file laid out by AssetPack.hpp instead of
// in a directory of its own. No baker changes to use it -- that is the entire point of the
// seam.
//
// One addition to the interface, forced by the format rather than chosen: a pack cannot be
// published one artifact at a time, because its index sits at the FRONT of the file and is not
// known until the last artifact has been taken. CommitAsset therefore accepts the artifact
// into the pack, and Finish() publishes the pack. Nothing on disk is readable before Finish
// returns Success. A baker never calls Finish; the tool that owns the sink does, exactly as it
// is the tool and not the baker that chose which sink to build.
//
// Everything else matches the loose sink's guarantees:
//
//  * PUBLICATION IS ONE RENAME of the finished staging file onto the destination. It is never
//    written through the destination path, so a reader cannot observe a half-written pack
//    under a name that claims to be a pack, and a failure anywhere before the rename leaves
//    the destination untouched.
//  * The staging files live beside the destination, which keeps the rename a rename: Win32's
//    MoveFileEx is allowed to fall back to a copy across volumes, and a copy is not atomic.
//  * The path budget is checked against the WORST case before anything is created, and the
//    destination is addressed through an extended-length path on Windows.
//  * An artifact still open at destruction is aborted, never published.
//
// Two staging files, and the reason is the front index: block payloads are appended to a
// payload file as they arrive (so a pack is never held in memory), and Finish assembles the
// header, the index and the payload into the staging pack. That costs one extra pass over the
// bytes, which is what putting the catalogue at the head of the file is worth -- see
// AssetPack.hpp. Debris from a killed process is bounded without a sweep: the staging names
// are derived from the destination, so the next run truncates them.
class AssetPackWriter final : public IBakedAssetSink {
public:
    // `profile` is read here and not retained: the pack records its identifier, its
    // fingerprint and its two alignments, and BakeTargetProfile::identifier is a non-owning
    // view that must not outlive the call.
    AssetPackWriter(std::filesystem::path packPath, const BakeTargetProfile& profile);
    ~AssetPackWriter() override;

    [[nodiscard]] BakedAssetSinkStatus BeginAsset(const BakedAssetDescriptor& descriptor) override;
    [[nodiscard]] BakedAssetSinkStatus WritePrimaryBlock(std::span<const std::uint8_t> bytes,
                                                         std::uint32_t alignmentBytes) override;
    [[nodiscard]] BakedAssetSinkStatus WriteAuxiliaryBlock(const BakedAssetBlock& block,
                                                           std::span<const std::uint8_t> bytes) override;
    // Accepts the artifact into the pack. Success means it will be in the published pack, not
    // that anything is readable yet; see the note on Finish above.
    [[nodiscard]] BakedAssetSinkStatus CommitAsset() override;
    void AbortAsset() noexcept override;

    // Assembles and publishes the pack. Refuses while an artifact is still open, because
    // publishing then would silently drop it. After Success the writer takes no more work.
    [[nodiscard]] BakedAssetSinkStatus Finish();

    // Where the published pack lives, normalised the way the writer addresses it.
    [[nodiscard]] const std::filesystem::path& PackPath() const noexcept {
        return packPath_;
    }

    // The file Finish assembles and then renames onto PackPath(). Exposed because "the
    // published pack is the same file object this was" is the observation that separates a
    // rename from a copy, and an observation nobody can make is not a guarantee.
    [[nodiscard]] const std::filesystem::path& StagingPackPath() const noexcept {
        return stagingPackPath_;
    }

private:
    struct PendingBlock {
        std::string name;
        BakedAssetBlockResidency residency = BakedAssetBlockResidency::Resident;
        std::uint32_t alignmentBytes = 1U;
        std::uint64_t payloadOffset = 0U;
        std::uint64_t bytes = 0U;
    };

    struct PendingArtifact {
        AssetBakeDigest key{};
        std::string assetTypeId;
        std::vector<PendingBlock> blocks;
    };

    [[nodiscard]] BakedAssetSinkStatus EnsureStagingOpen();
    // The staging payload carries a stamp naming the writer that created it, and this is what
    // reads it back. Staging names are derived from the destination, so this is the only thing
    // between "a second writer aimed at this package" and a published package whose blocks
    // hold another bake's bytes.
    [[nodiscard]] BakedAssetSinkStatus VerifyPayloadStamp();
    [[nodiscard]] BakedAssetSinkStatus AppendPayload(std::span<const std::uint8_t> bytes, std::uint64_t& offsetOut);
    [[nodiscard]] BakedAssetSinkStatus AssembleStagingPack();
    void DiscardStaging() noexcept;

    std::filesystem::path packPath_;
    std::filesystem::path stagingPackPath_;
    std::filesystem::path payloadPath_;
    std::string targetProfileId_;
    std::uint64_t targetProfileHash_ = 0U;
    std::uint32_t packageAlignmentBytes_ = 0U;
    std::uint32_t mappedAlignmentBytes_ = 0U;
    bool profileIsValid_ = false;

    std::fstream payload_;
    std::vector<PendingArtifact> artifacts_;
    PendingArtifact openArtifact_;
    std::uint64_t payloadBytes_ = 0U;
    std::uint64_t payloadStamp_ = 0U;
    std::uint64_t openArtifactPayloadStart_ = 0U;
    bool stagingOpen_ = false;
    bool open_ = false;
    bool primaryWritten_ = false;
    bool finished_ = false;
};

} // namespace kb::assets::bake
