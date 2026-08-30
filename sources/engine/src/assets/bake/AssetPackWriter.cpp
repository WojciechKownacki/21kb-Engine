#include "engine/assets/bake/AssetPackWriter.hpp"

#include "assets/bake/BakeStorePath.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <system_error>
#include <utility>

namespace kb::assets::bake {
namespace {

// Suffixes of the two files Finish works with. Derived from the destination so that they sit
// on the same volume: std::filesystem::rename lowers to MoveFileEx, which is allowed to fall
// back to a COPY across volumes, and a copy is not a publication.
constexpr std::wstring_view kStagingPackSuffix = L".kbpackstaging";
constexpr std::wstring_view kStagingPayloadSuffix = L".kbpackpayload";

// Chunk the assembly pass copies with. Big enough that the copy is bound by the disk rather
// than by the loop, small enough that it is not a memory decision.
constexpr std::size_t kAssemblyChunkBytes = 1024U * 1024U;

[[nodiscard]] std::filesystem::path WithSuffix(const std::filesystem::path& path, std::wstring_view suffix) {
#if defined(_WIN32)
    return std::filesystem::path{ path.native() + std::wstring{ suffix } };
#else
    std::string narrow;
    narrow.reserve(suffix.size());
    for (const wchar_t character : suffix) {
        narrow.push_back(static_cast<char>(character));
    }
    return std::filesystem::path{ path.native() + narrow };
#endif
}

// A block is placed at the coarser of what the baker asked for and what the profile demands.
// The profile is a FLOOR, not a suggestion: an under-aligned block is not a padding saving,
// it is a block that cannot be bound or copied from where it lies. A Mapped block additionally
// takes the mapping granularity, which is the only alignment a mapping call will accept as a
// file offset.
[[nodiscard]] std::uint32_t EffectiveAlignment(
    std::uint32_t requested,
    BakedAssetBlockResidency residency,
    std::uint32_t packageAlignment,
    std::uint32_t mappedAlignment) noexcept {
    std::uint32_t alignment = std::max(requested, packageAlignment);
    if (residency == BakedAssetBlockResidency::Mapped) {
        alignment = std::max(alignment, mappedAlignment);
    }
    return alignment;
}

} // namespace

AssetPackWriter::AssetPackWriter(std::filesystem::path packPath, const BakeTargetProfile& profile)
    : packPath_{ store::Normalize(std::move(packPath)) }
    , targetProfileId_{ profile.identifier }
    , targetProfileHash_{ BakeTargetProfileFingerprint(profile) }
    , packageAlignmentBytes_{ profile.packageBlockAlignmentBytes }
    , mappedAlignmentBytes_{ profile.mappedBlockAlignmentBytes }
    , profileIsValid_{ IsValidBakeTargetProfile(profile) } {
    stagingPackPath_ = WithSuffix(packPath_, kStagingPackSuffix);
    payloadPath_ = WithSuffix(packPath_, kStagingPayloadSuffix);
}

AssetPackWriter::~AssetPackWriter() {
    // An artifact still open here means the bake died mid-way; an unfinished pack means the
    // owner never asked for one. Neither may reach the destination.
    AbortAsset();
    payload_.close();
    if (!finished_) {
        DiscardStaging();
    }
}

BakedAssetSinkStatus AssetPackWriter::EnsureStagingOpen() {
    if (stagingOpen_) {
        return BakedAssetSinkStatus::Success;
    }
    // Checked BEFORE anything is created, and against the longer of the two staging names, so
    // that a destination the platform cannot open is refused rather than half-written.
    const std::size_t worstCase =
        std::max(stagingPackPath_.native().size(), payloadPath_.native().size());
    if (packPath_.empty() || worstCase > kMaxBakeStorePathLength) {
        return BakedAssetSinkStatus::PathTooLong;
    }

    std::error_code error;
    if (!packPath_.parent_path().empty()) {
        std::filesystem::create_directories(packPath_.parent_path(), error);
        if (error) {
            return BakedAssetSinkStatus::WriteFailed;
        }
    }

    // Truncating rather than requiring absence: the staging names are derived from the
    // destination, so debris a killed process left behind is exactly what a new run overwrites.
    payload_.open(payloadPath_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!payload_.is_open()) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    {
        std::ofstream stagingPack{ stagingPackPath_, std::ios::binary | std::ios::trunc };
        if (!stagingPack.is_open()) {
            payload_.close();
            return BakedAssetSinkStatus::WriteFailed;
        }
    }

    payloadBytes_ = 0U;
    stagingOpen_ = true;
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus AssetPackWriter::AppendPayload(std::span<const std::uint8_t> bytes, std::uint64_t& offsetOut) {
    payload_.clear();
    payload_.seekp(static_cast<std::streamoff>(payloadBytes_), std::ios::beg);
    payload_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!payload_) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    offsetOut = payloadBytes_;
    payloadBytes_ += bytes.size();
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus AssetPackWriter::BeginAsset(const BakedAssetDescriptor& descriptor) {
    if (finished_) {
        return BakedAssetSinkStatus::PackAlreadyFinished;
    }
    if (open_) {
        return BakedAssetSinkStatus::AssetAlreadyOpen;
    }
    if (!profileIsValid_) {
        return BakedAssetSinkStatus::InvalidProfile;
    }
    if (!descriptor.key.IsValid()) {
        return BakedAssetSinkStatus::InvalidKey;
    }
    if (!IsValidBakeCacheName(descriptor.assetTypeId)) {
        return BakedAssetSinkStatus::InvalidAssetType;
    }
    if (const BakedAssetSinkStatus staging = EnsureStagingOpen(); staging != BakedAssetSinkStatus::Success) {
        return staging;
    }

    openArtifact_ = PendingArtifact{};
    openArtifact_.key = descriptor.key.Digest();
    openArtifact_.assetTypeId = descriptor.assetTypeId;
    openArtifactPayloadStart_ = payloadBytes_;
    primaryWritten_ = false;
    open_ = true;
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus AssetPackWriter::WritePrimaryBlock(std::span<const std::uint8_t> bytes,
                                                        std::uint32_t alignmentBytes) {
    if (!open_) {
        return BakedAssetSinkStatus::NoAssetOpen;
    }
    if (primaryWritten_) {
        return BakedAssetSinkStatus::DuplicateBlock;
    }
    if (!store::IsPowerOfTwo(alignmentBytes)) {
        return BakedAssetSinkStatus::InvalidAlignment;
    }
    if (bytes.empty()) {
        return BakedAssetSinkStatus::EmptyBlock;
    }
    if (bytes.size() > kMaxAssetPackBlockBytes) {
        return BakedAssetSinkStatus::BlockTooLarge;
    }

    std::uint64_t payloadOffset = 0U;
    if (const BakedAssetSinkStatus status = AppendPayload(bytes, payloadOffset);
        status != BakedAssetSinkStatus::Success) {
        return status;
    }
    openArtifact_.blocks.push_back(PendingBlock{
        .name = std::string{ kBakedAssetPrimaryBlockName },
        .residency = BakedAssetBlockResidency::Resident,
        .alignmentBytes = alignmentBytes,
        .payloadOffset = payloadOffset,
        .bytes = bytes.size(),
    });
    primaryWritten_ = true;
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus AssetPackWriter::WriteAuxiliaryBlock(const BakedAssetBlock& block,
                                                          std::span<const std::uint8_t> bytes) {
    if (!open_) {
        return BakedAssetSinkStatus::NoAssetOpen;
    }
    if (!IsValidBakeCacheName(block.name) ||
        store::EqualsIgnoreAsciiCase(block.name, kBakedAssetPrimaryBlockName)) {
        return BakedAssetSinkStatus::InvalidBlockName;
    }
    if (!store::IsPowerOfTwo(block.alignmentBytes)) {
        return BakedAssetSinkStatus::InvalidAlignment;
    }
    // Case-insensitive, exactly as in the loose store: the same artifact must name the same
    // blocks whichever sink it was published through.
    const bool duplicate =
        std::ranges::any_of(openArtifact_.blocks, [&block](const PendingBlock& written) noexcept {
            return store::EqualsIgnoreAsciiCase(written.name, block.name);
        });
    if (duplicate) {
        return BakedAssetSinkStatus::DuplicateBlock;
    }
    if (bytes.empty()) {
        return BakedAssetSinkStatus::EmptyBlock;
    }
    if (bytes.size() > kMaxAssetPackBlockBytes) {
        return BakedAssetSinkStatus::BlockTooLarge;
    }

    std::uint64_t payloadOffset = 0U;
    if (const BakedAssetSinkStatus status = AppendPayload(bytes, payloadOffset);
        status != BakedAssetSinkStatus::Success) {
        return status;
    }
    openArtifact_.blocks.push_back(PendingBlock{
        .name = std::string{ block.name },
        .residency = block.residency,
        .alignmentBytes = block.alignmentBytes,
        .payloadOffset = payloadOffset,
        .bytes = bytes.size(),
    });
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus AssetPackWriter::CommitAsset() {
    if (!open_) {
        return BakedAssetSinkStatus::NoAssetOpen;
    }
    if (!primaryWritten_) {
        // The artifact stays open: a missing payload is a caller mistake it can still repair.
        return BakedAssetSinkStatus::MissingPrimaryBlock;
    }

    const auto existing = std::ranges::find_if(artifacts_, [this](const PendingArtifact& artifact) {
        return artifact.key == openArtifact_.key;
    });
    if (existing != artifacts_.end()) {
        // The store is content-addressed, so the same key is the same artifact and taking it
        // twice is not an error -- but only if it is the same artifact. A key claimed by two
        // asset types is a caller mistake that would put an ambiguous entry in the catalogue.
        if (existing->assetTypeId != openArtifact_.assetTypeId) {
            return BakedAssetSinkStatus::InvalidAssetType;
        }
        payloadBytes_ = openArtifactPayloadStart_;
        openArtifact_ = PendingArtifact{};
        primaryWritten_ = false;
        open_ = false;
        return BakedAssetSinkStatus::Success;
    }

    artifacts_.push_back(std::move(openArtifact_));
    openArtifact_ = PendingArtifact{};
    primaryWritten_ = false;
    open_ = false;
    return BakedAssetSinkStatus::Success;
}

void AssetPackWriter::AbortAsset() noexcept {
    if (!open_) {
        return;
    }
    // The payload cursor goes back to where the artifact began, so its bytes are simply
    // overwritten by the next one and never reach the index. Nothing outside this writer has
    // seen them: the pack does not exist until Finish.
    payloadBytes_ = openArtifactPayloadStart_;
    openArtifact_ = PendingArtifact{};
    primaryWritten_ = false;
    open_ = false;
}

BakedAssetSinkStatus AssetPackWriter::AssembleStagingPack() {
    std::vector<AssetPackArtifactEntry> entries;
    entries.reserve(artifacts_.size());
    for (const PendingArtifact& artifact : artifacts_) {
        AssetPackArtifactEntry entry{};
        entry.key = artifact.key;
        entry.assetTypeId = artifact.assetTypeId;
        entry.blocks.reserve(artifact.blocks.size());
        for (const PendingBlock& block : artifact.blocks) {
            entry.blocks.push_back(AssetPackBlockEntry{
                .name = block.name,
                .residency = block.residency,
                .compression = AssetPackBlockCompression::None,
                .alignmentBytes = EffectiveAlignment(
                    block.alignmentBytes, block.residency, packageAlignmentBytes_, mappedAlignmentBytes_),
                .offset = 0U,
                .storedBytes = block.bytes,
                .uncompressedBytes = block.bytes,
            });
        }
        entries.push_back(std::move(entry));
    }

    // Every offset is a fixed-width field, so the index measures the same before and after the
    // offsets are known. That is what lets the catalogue sit at the FRONT of the file without
    // a second layout pass.
    const std::uint64_t indexBytes = EncodeAssetPackIndex(entries).size();
    if (indexBytes > kMaxAssetPackIndexBytes) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    std::uint64_t cursor = 0U;
    if (!TryAlignAssetPackOffset(kAssetPackHeaderBytes + indexBytes, packageAlignmentBytes_, cursor)) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    const std::uint64_t dataStart = cursor;
    for (AssetPackArtifactEntry& entry : entries) {
        for (AssetPackBlockEntry& block : entry.blocks) {
            if (!TryAlignAssetPackOffset(cursor, block.alignmentBytes, block.offset)) {
                return BakedAssetSinkStatus::WriteFailed;
            }
            if (block.storedBytes > std::numeric_limits<std::uint64_t>::max() - block.offset) {
                return BakedAssetSinkStatus::WriteFailed;
            }
            cursor = block.offset + block.storedBytes;
        }
    }
    if (cursor > kMaxAssetPackBytes) {
        return BakedAssetSinkStatus::WriteFailed;
    }

    const std::vector<std::uint8_t> index = EncodeAssetPackIndex(entries);
    if (index.size() != indexBytes) {
        return BakedAssetSinkStatus::WriteFailed;
    }

    AssetPackHeader header{};
    header.targetProfileId = targetProfileId_;
    header.targetProfileHash = targetProfileHash_;
    header.indexOffset = kAssetPackHeaderBytes;
    header.indexBytes = indexBytes;
    header.indexChecksum = HashBakeBytes(index);
    header.artifactCount = static_cast<std::uint32_t>(entries.size());
    header.packageBlockAlignmentBytes = packageAlignmentBytes_;
    header.mappedBlockAlignmentBytes = mappedAlignmentBytes_;
    header.fileBytes = cursor;
    const std::vector<std::uint8_t> headerBytes = EncodeAssetPackHeader(header);

    // Reopened in place rather than recreated: the staging file's identity is what the
    // rename below moves, and it is the observation that tells a rename from a copy.
    std::ofstream output{ stagingPackPath_, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    output.write(reinterpret_cast<const char*>(headerBytes.data()), static_cast<std::streamsize>(headerBytes.size()));
    output.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size()));

    // Padding never exceeds the coarsest alignment a block can ask for, so a small run of
    // zeros written repeatedly costs nothing and keeps a megabyte off the stack.
    const std::array<char, 4096U> zeros{};
    std::vector<char> buffer(kAssemblyChunkBytes);
    std::uint64_t written = kAssetPackHeaderBytes + indexBytes;
    auto padTo = [&](std::uint64_t target) {
        while (written < target && output) {
            const std::uint64_t chunk = std::min<std::uint64_t>(target - written, zeros.size());
            output.write(zeros.data(), static_cast<std::streamsize>(chunk));
            written += chunk;
        }
    };
    padTo(dataStart);

    // The payload file has been written through and is about to be read through; the seek
    // between the two is what the stream requires, and the flush is what makes the bytes
    // visible to it.
    payload_.flush();
    payload_.clear();
    for (std::size_t artifactIndex = 0U; artifactIndex < artifacts_.size() && output; ++artifactIndex) {
        const PendingArtifact& artifact = artifacts_[artifactIndex];
        const AssetPackArtifactEntry& entry = entries[artifactIndex];
        for (std::size_t blockIndex = 0U; blockIndex < artifact.blocks.size() && output; ++blockIndex) {
            const PendingBlock& block = artifact.blocks[blockIndex];
            padTo(entry.blocks[blockIndex].offset);
            payload_.clear();
            payload_.seekg(static_cast<std::streamoff>(block.payloadOffset), std::ios::beg);
            std::uint64_t remaining = block.bytes;
            while (remaining > 0U && output && payload_) {
                const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                payload_.read(buffer.data(), static_cast<std::streamsize>(chunk));
                if (payload_.gcount() != static_cast<std::streamsize>(chunk)) {
                    return BakedAssetSinkStatus::WriteFailed;
                }
                output.write(buffer.data(), static_cast<std::streamsize>(chunk));
                written += chunk;
                remaining -= chunk;
            }
        }
    }
    output.flush();
    if (!output || !payload_ || written != cursor) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    output.close();
    return output.good() ? BakedAssetSinkStatus::Success : BakedAssetSinkStatus::WriteFailed;
}

BakedAssetSinkStatus AssetPackWriter::Finish() {
    if (finished_) {
        return BakedAssetSinkStatus::PackAlreadyFinished;
    }
    if (open_) {
        // Publishing now would drop the open artifact without a word; the caller has to say
        // whether it meant to commit it or to abandon it.
        return BakedAssetSinkStatus::AssetAlreadyOpen;
    }
    if (!profileIsValid_) {
        return BakedAssetSinkStatus::InvalidProfile;
    }
    if (const BakedAssetSinkStatus staging = EnsureStagingOpen(); staging != BakedAssetSinkStatus::Success) {
        return staging;
    }
    if (const BakedAssetSinkStatus assembled = AssembleStagingPack(); assembled != BakedAssetSinkStatus::Success) {
        // The destination has not been touched, so a failed Finish leaves the previous pack --
        // or no pack at all -- exactly as it was.
        return assembled;
    }

    payload_.close();

    // ONE operation. The pack is never written through the destination path, so no reader can
    // observe a name that claims to be a pack while it is still being filled.
    std::error_code renameError;
    std::filesystem::rename(stagingPackPath_, packPath_, renameError);
    if (renameError) {
        return BakedAssetSinkStatus::WriteFailed;
    }

    std::error_code removeError;
    std::filesystem::remove(payloadPath_, removeError);
    finished_ = true;
    stagingOpen_ = false;
    return BakedAssetSinkStatus::Success;
}

void AssetPackWriter::DiscardStaging() noexcept {
    if (!stagingOpen_) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(stagingPackPath_, error);
    std::filesystem::remove(payloadPath_, error);
    stagingOpen_ = false;
}

} // namespace kb::assets::bake
