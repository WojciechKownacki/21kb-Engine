#include "engine/assets/bake/AssetPackWriter.hpp"

#include "assets/bake/BakeStorePath.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <system_error>
#include <utility>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #include <fcntl.h>
    #include <sys/file.h>
    #include <unistd.h>
#endif

namespace kb::assets::bake {
namespace {

// Suffixes of the two files Finish works with. Derived from the destination so that they sit
// on the same volume: std::filesystem::rename lowers to MoveFileEx, which is allowed to fall
// back to a COPY across volumes, and a copy is not a publication.
constexpr std::wstring_view kStagingPackSuffix = L".kbpackstaging";
constexpr std::wstring_view kStagingPayloadSuffix = L".kbpackpayload";
constexpr std::wstring_view kLockSuffix = L".kbpacklock";

// Chunk the assembly pass copies with. Big enough that the copy is bound by the disk rather
// than by the loop, small enough that it is not a memory decision.
constexpr std::size_t kAssemblyChunkBytes = 1024U * 1024U;

// The staging payload opens with a stamp: a constant, and a value unique to the writer that
// created the file. The staging names are DERIVED FROM THE DESTINATION -- deliberately, so a
// killed run's debris is what the next run truncates -- and the price of that is that a second
// writer aimed at the same package truncates the first one's payload and writes its own bytes
// where the first one's offsets point. Measured: the published package then mounts, checksums
// and hands back bytes belonging to neither writer under a key that names one of them, which
// is the silent-wrong-artifact failure the whole store exists to prevent. Sixteen bytes at the
// front of a staging file turn it into a refusal.
constexpr std::uint64_t kPayloadStampMagic = 0x3231'4B42'5041'594CULL;
constexpr std::uint64_t kPayloadStampBytes = 16U;

[[nodiscard]] std::uint64_t NextPayloadStamp() noexcept {
    static std::atomic<std::uint64_t> counter{ 0U };
    const std::uint64_t ticks =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t sequence = counter.fetch_add(1U, std::memory_order_relaxed) + 1U;
    // The counter separates writers inside one process and the clock separates processes; the
    // address separates two processes that managed to start in the same clock tick, which is
    // what makes the answer "no" rather than "very unlikely".
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&counter));
    // Never zero: a zero stamp is what a file of zeros would present, and that has to read as
    // "not this writer's payload" rather than as a match.
    return ((ticks * 0x9E37'79B9'7F4A'7C15ULL) ^ (sequence * 0xD1B5'4A32'D192'ED03ULL) ^
        (address * 0xBF58'476D'1CE4'E5B9ULL)) | 1U;
}

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
    lockPath_ = WithSuffix(packPath_, kLockSuffix);
}

AssetPackWriter::~AssetPackWriter() {
    // An artifact still open here means the bake died mid-way; an unfinished pack means the
    // owner never asked for one. Neither may reach the destination.
    AbortAsset();
    payload_.close();
    if (!finished_) {
        DiscardStaging();
    }
    ReleaseDestinationLock();
}

bool AssetPackWriter::AcquireDestinationLock() {
    if (lockHeld_) {
        return true;
    }
#if defined(_WIN32)
    HANDLE const handle = CreateFileW(
        lockPath_.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0U,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    lockHandle_ = reinterpret_cast<std::intptr_t>(handle);
#else
    const int descriptor = open(lockPath_.c_str(), O_CREAT | O_RDWR, 0666);
    if (descriptor < 0) {
        return false;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        close(descriptor);
        return false;
    }
    lockHandle_ = descriptor;
#endif
    lockHeld_ = true;
    return true;
}

void AssetPackWriter::ReleaseDestinationLock() noexcept {
    if (!lockHeld_) {
        return;
    }
#if defined(_WIN32)
    static_cast<void>(CloseHandle(reinterpret_cast<HANDLE>(lockHandle_)));
#else
    const int descriptor = static_cast<int>(lockHandle_);
    static_cast<void>(::flock(descriptor, LOCK_UN));
    static_cast<void>(::close(descriptor));
#endif
    lockHandle_ = -1;
    lockHeld_ = false;
    std::error_code removeError;
    std::filesystem::remove(lockPath_, removeError);
}

BakedAssetSinkStatus AssetPackWriter::EnsureStagingOpen() {
    if (stagingOpen_) {
        return BakedAssetSinkStatus::Success;
    }
    // Checked BEFORE anything is created, and against the longest staging name, so
    // that a destination the platform cannot open is refused rather than half-written.
    const std::size_t worstCase = std::max({
        stagingPackPath_.native().size(), payloadPath_.native().size(), lockPath_.native().size() });
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

    if (!AcquireDestinationLock()) {
        return BakedAssetSinkStatus::StagingConflict;
    }

    // Truncating rather than requiring absence: the staging names are derived from the
    // destination, so debris a killed process left behind is exactly what a new run overwrites.
    payload_.open(payloadPath_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    if (!payload_.is_open()) {
        ReleaseDestinationLock();
        return BakedAssetSinkStatus::WriteFailed;
    }
    {
        std::ofstream stagingPack{ stagingPackPath_, std::ios::binary | std::ios::trunc };
        if (!stagingPack.is_open()) {
            payload_.close();
            std::filesystem::remove(payloadPath_, error);
            ReleaseDestinationLock();
            return BakedAssetSinkStatus::WriteFailed;
        }
    }

    // Block payloads start after the stamp, so payloadBytes_ is an absolute position in the
    // staging payload from here on and every recorded block offset already clears it.
    payloadStamp_ = NextPayloadStamp();
    std::array<std::uint8_t, kPayloadStampBytes> stamp{};
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        stamp[index] = static_cast<std::uint8_t>((kPayloadStampMagic >> (index * 8U)) & 0xFFU);
        stamp[8U + index] = static_cast<std::uint8_t>((payloadStamp_ >> (index * 8U)) & 0xFFU);
    }
    payload_.seekp(0, std::ios::beg);
    payload_.write(reinterpret_cast<const char*>(stamp.data()), static_cast<std::streamsize>(stamp.size()));
    if (!payload_) {
        payload_.close();
        std::error_code cleanupError;
        std::filesystem::remove(stagingPackPath_, cleanupError);
        std::filesystem::remove(payloadPath_, cleanupError);
        ReleaseDestinationLock();
        return BakedAssetSinkStatus::WriteFailed;
    }

    payloadBytes_ = kPayloadStampBytes;
    stagingOpen_ = true;
    return BakedAssetSinkStatus::Success;
}

// Is the staging payload still the one this writer opened? Called once, at assembly, before a
// single payload byte is believed.
BakedAssetSinkStatus AssetPackWriter::VerifyPayloadStamp() {
    payload_.flush();
    payload_.clear();
    payload_.seekg(0, std::ios::beg);
    std::array<char, kPayloadStampBytes> stamp{};
    payload_.read(stamp.data(), static_cast<std::streamsize>(stamp.size()));
    if (payload_.gcount() != static_cast<std::streamsize>(stamp.size())) {
        payload_.clear();
        return BakedAssetSinkStatus::StagingConflict;
    }
    std::uint64_t magic = 0U;
    std::uint64_t written = 0U;
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        magic |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(stamp[index])) << (index * 8U);
        written |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(stamp[8U + index])) << (index * 8U);
    }
    payload_.clear();
    if (magic != kPayloadStampMagic || written != payloadStamp_) {
        return BakedAssetSinkStatus::StagingConflict;
    }
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
    if (descriptor.key.targetProfileId != targetProfileId_ ||
        descriptor.key.targetProfileHash != targetProfileHash_) {
        return BakedAssetSinkStatus::InvalidProfile;
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
        .payloadDigest = HashBakeDigest(bytes),
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
    if (block.fragment.has_value() &&
        (block.residency != BakedAssetBlockResidency::Streaming ||
            !IsValidBakedAssetBlockFragment(*block.fragment))) {
        return BakedAssetSinkStatus::InvalidFragment;
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
        .payloadDigest = HashBakeDigest(bytes),
        .fragment = block.fragment,
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

    const auto existing = artifactTypes_.find(openArtifact_.key);
    if (existing != artifactTypes_.end()) {
        // The store is content-addressed, so the same key is the same artifact and taking it
        // twice is not an error -- but only if it is the same artifact. A key claimed by two
        // asset types is a caller mistake that would put an ambiguous entry in the catalogue.
        if (existing->second != openArtifact_.assetTypeId) {
            return BakedAssetSinkStatus::InvalidAssetType;
        }
        payloadBytes_ = openArtifactPayloadStart_;
        openArtifact_ = PendingArtifact{};
        primaryWritten_ = false;
        open_ = false;
        return BakedAssetSinkStatus::Success;
    }

    artifactTypes_.emplace(openArtifact_.key, openArtifact_.assetTypeId);
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
    if (const BakedAssetSinkStatus stamp = VerifyPayloadStamp(); stamp != BakedAssetSinkStatus::Success) {
        return stamp;
    }
    // A pack is content-addressed output: task completion order must not affect its bytes.
    // Block order remains baker-defined because streaming fragment order is semantic.
    std::ranges::sort(artifacts_, {}, &PendingArtifact::key);
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
                .payloadDigest = block.payloadDigest,
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
    // The fragment index is fixed-width too, so its length is known here, before any block has
    // an offset -- which is what lets it sit between the artifact index and the payload rather
    // than at the end where a streaming reader would have to go looking for it.
    std::uint64_t fragmentCount = 0U;
    for (const PendingArtifact& artifact : artifacts_) {
        for (const PendingBlock& block : artifact.blocks) {
            fragmentCount += block.fragment.has_value() ? 1U : 0U;
        }
    }
    if (fragmentCount > std::numeric_limits<std::uint32_t>::max()) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    const std::uint64_t fragmentIndexBytes = fragmentCount * kAssetPackFragmentEntryBytes;
    if (fragmentIndexBytes > kMaxAssetPackFragmentIndexBytes) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    std::uint64_t cursor = 0U;
    if (!TryAlignAssetPackOffset(
            kAssetPackHeaderBytes + indexBytes + fragmentIndexBytes, packageAlignmentBytes_, cursor)) {
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

    // Built only now: a fragment names the block's FINAL place in the file, so that a reader
    // can match the two and refuse a fragment index that describes anything but whole blocks.
    std::vector<AssetPackFragmentEntry> fragments;
    fragments.reserve(static_cast<std::size_t>(fragmentCount));
    for (std::size_t artifactIndex = 0U; artifactIndex < artifacts_.size(); ++artifactIndex) {
        const PendingArtifact& artifact = artifacts_[artifactIndex];
        for (std::size_t blockIndex = 0U; blockIndex < artifact.blocks.size(); ++blockIndex) {
            const PendingBlock& block = artifact.blocks[blockIndex];
            if (!block.fragment.has_value()) {
                continue;
            }
            const AssetPackBlockEntry& placed = entries[artifactIndex].blocks[blockIndex];
            fragments.push_back(AssetPackFragmentEntry{
                .offset = placed.offset,
                .bytes = placed.storedBytes,
                .clusterCount = block.fragment->clusterCount,
                .boundsMin = block.fragment->boundsMin,
                .boundsMax = block.fragment->boundsMax,
            });
        }
    }
    const std::vector<std::uint8_t> fragmentIndex = EncodeAssetPackFragmentIndex(fragments);
    if (fragmentIndex.size() != fragmentIndexBytes) {
        return BakedAssetSinkStatus::WriteFailed;
    }

    AssetPackHeader header{};
    header.targetProfileId = targetProfileId_;
    header.targetProfileHash = targetProfileHash_;
    header.indexOffset = kAssetPackHeaderBytes;
    header.indexBytes = indexBytes;
    header.indexChecksum = AssetPackIndexChecksum(index, fragmentIndex);
    header.artifactCount = static_cast<std::uint32_t>(entries.size());
    header.packageBlockAlignmentBytes = packageAlignmentBytes_;
    header.mappedBlockAlignmentBytes = mappedAlignmentBytes_;
    header.fileBytes = cursor;
    if (fragmentCount != 0U) {
        header.fragmentIndexOffset = kAssetPackHeaderBytes + indexBytes;
        header.fragmentIndexBytes = fragmentIndexBytes;
        header.fragmentCount = static_cast<std::uint32_t>(fragmentCount);
        // Every fragment is a block, so it already sits at the package alignment and nothing
        // coarser was asked of it. Recording the value the placement actually satisfied, rather
        // than a constant, is what keeps this field a statement about the file.
        header.fragmentAlignmentBytes = packageAlignmentBytes_;
    }
    const std::vector<std::uint8_t> headerBytes = EncodeAssetPackHeader(header);

    // Reopened in place rather than recreated: the staging file's identity is what the
    // rename below moves, and it is the observation that tells a rename from a copy.
    std::ofstream output{ stagingPackPath_, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    output.write(reinterpret_cast<const char*>(headerBytes.data()), static_cast<std::streamsize>(headerBytes.size()));
    output.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size()));
    output.write(
        reinterpret_cast<const char*>(fragmentIndex.data()), static_cast<std::streamsize>(fragmentIndex.size()));

    // Padding never exceeds the coarsest alignment a block can ask for, so a small run of
    // zeros written repeatedly costs nothing and keeps a megabyte off the stack.
    const std::array<char, 4096U> zeros{};
    std::vector<char> buffer(kAssemblyChunkBytes);
    std::uint64_t written = kAssetPackHeaderBytes + indexBytes + fragmentIndexBytes;
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
    ReleaseDestinationLock();
    return BakedAssetSinkStatus::Success;
}

void AssetPackWriter::DiscardStaging() noexcept {
    if (stagingOpen_) {
        std::error_code error;
        std::filesystem::remove(stagingPackPath_, error);
        std::filesystem::remove(payloadPath_, error);
        stagingOpen_ = false;
    }
    ReleaseDestinationLock();
}

} // namespace kb::assets::bake
