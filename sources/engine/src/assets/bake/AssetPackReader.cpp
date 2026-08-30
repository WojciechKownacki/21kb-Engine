#include "engine/assets/bake/AssetPackReader.hpp"

#include "assets/bake/BakeStorePath.hpp"

#include <algorithm>
#include <limits>
#include <system_error>
#include <utility>

namespace kb::assets::bake {

AssetPackReadStatus AssetPackReader::ValidateBlockRange(const AssetPackBlockEntry& block) const noexcept {
    if (block.storedBytes > kMaxAssetPackBlockBytes) {
        return AssetPackReadStatus::BlockTooLarge;
    }
    // Written as a subtraction against the file length rather than as `offset + bytes`: a
    // hostile index can put an offset one below 2^64, where the sum wraps and every "is it
    // inside the file?" test it is used in answers yes.
    if (block.offset > fileBytes_ || block.storedBytes > fileBytes_ - block.offset) {
        return AssetPackReadStatus::BlockOutOfRange;
    }
    // A block must not begin inside the catalogue that describes it.
    if (block.offset < header_.indexOffset + header_.indexBytes) {
        return AssetPackReadStatus::BlockOutOfRange;
    }
    if (!store::IsPowerOfTwo(block.alignmentBytes) || block.alignmentBytes < header_.packageBlockAlignmentBytes ||
        block.offset % block.alignmentBytes != 0U) {
        return AssetPackReadStatus::BlockOutOfRange;
    }
    // The mapping granularity is not advice: a mapped block placed off it is a block whose
    // file offset the platform's mapping call refuses outright.
    if (block.residency == BakedAssetBlockResidency::Mapped &&
        block.offset % header_.mappedBlockAlignmentBytes != 0U) {
        return AssetPackReadStatus::BlockOutOfRange;
    }
    return AssetPackReadStatus::Success;
}

AssetPackReadStatus AssetPackReader::ReadRange(std::uint64_t offset, std::uint64_t bytes, std::vector<std::uint8_t>& out) {
    if (offset > fileBytes_ || bytes > fileBytes_ - offset) {
        return AssetPackReadStatus::BlockOutOfRange;
    }
    if (bytes > std::numeric_limits<std::size_t>::max()) {
        return AssetPackReadStatus::BlockTooLarge;
    }
    out.assign(static_cast<std::size_t>(bytes), 0U);
    if (bytes == 0U) {
        return AssetPackReadStatus::Success;
    }
    if (access_ == AssetPackAccess::WholeFile) {
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), static_cast<std::size_t>(bytes), out.begin());
        return AssetPackReadStatus::Success;
    }
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    if (stream_.gcount() != static_cast<std::streamsize>(bytes)) {
        out.clear();
        return AssetPackReadStatus::Unreadable;
    }
    return AssetPackReadStatus::Success;
}

AssetPackReadStatus AssetPackReader::Mount(const std::filesystem::path& path, AssetPackAccess access) {
    Unmount();

    path_ = store::Normalize(path);
    access_ = access;

    std::error_code sizeError;
    const std::uintmax_t sizeOnDisk = std::filesystem::file_size(path_, sizeError);
    if (sizeError) {
        return AssetPackReadStatus::Unreadable;
    }
    if (sizeOnDisk > kMaxAssetPackBytes) {
        return AssetPackReadStatus::PackTooLarge;
    }
    if (sizeOnDisk < kAssetPackHeaderBytes) {
        return AssetPackReadStatus::Unreadable;
    }

    stream_.open(path_, std::ios::binary);
    if (!stream_.is_open()) {
        return AssetPackReadStatus::Unreadable;
    }
    ++openCount_;
    fileBytes_ = static_cast<std::uint64_t>(sizeOnDisk);

    if (access_ == AssetPackAccess::WholeFile) {
        // The whole pack, once, into one buffer. Doing it here rather than per block is the
        // point: an allocation per asset is what the 4096-allocation ceiling forbids.
        bytes_.assign(static_cast<std::size_t>(fileBytes_), 0U);
        stream_.read(reinterpret_cast<char*>(bytes_.data()), static_cast<std::streamsize>(fileBytes_));
        const bool complete = stream_.gcount() == static_cast<std::streamsize>(fileBytes_);
        stream_.close();
        if (!complete) {
            Unmount();
            return AssetPackReadStatus::Unreadable;
        }
    }

    std::vector<std::uint8_t> headerBytes;
    if (const AssetPackReadStatus status = ReadRange(0U, kAssetPackHeaderBytes, headerBytes);
        status != AssetPackReadStatus::Success) {
        Unmount();
        return status;
    }
    if (const AssetPackReadStatus status = DecodeAssetPackHeader(headerBytes, header_);
        status != AssetPackReadStatus::Success) {
        Unmount();
        return status;
    }
    // The pack says how long it is. A file that is shorter has been truncated; a file that is
    // longer is not the file this header was written for, and either way every offset below
    // was computed against a length that is not the one on disk.
    if (header_.fileBytes != fileBytes_) {
        Unmount();
        return AssetPackReadStatus::SizeMismatch;
    }

    std::vector<std::uint8_t> indexBytes;
    if (const AssetPackReadStatus status = ReadRange(header_.indexOffset, header_.indexBytes, indexBytes);
        status != AssetPackReadStatus::Success) {
        Unmount();
        return status;
    }
    if (HashBakeBytes(indexBytes) != header_.indexChecksum) {
        Unmount();
        return AssetPackReadStatus::IndexCorrupt;
    }
    if (const AssetPackReadStatus status = DecodeAssetPackIndex(indexBytes, header_.artifactCount, artifacts_);
        status != AssetPackReadStatus::Success) {
        Unmount();
        return status;
    }

    // Every block is checked against the real file NOW, so nothing downstream has to wonder
    // whether the entry it was handed was ever validated.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    for (const AssetPackArtifactEntry& artifact : artifacts_) {
        ranges.reserve(ranges.size() + artifact.blocks.size());
        for (const AssetPackBlockEntry& block : artifact.blocks) {
            if (const AssetPackReadStatus status = ValidateBlockRange(block);
                status != AssetPackReadStatus::Success) {
                Unmount();
                return status;
            }
            ranges.emplace_back(block.offset, block.offset + block.storedBytes);
        }
    }
    // ...and no two of them may be the same bytes. Every range above lies inside the file, so
    // an overlap is memory-safe -- and it is still a pack that answers one content-addressed
    // key with a payload baked under another, which is the failure this store exists to make
    // impossible. The sum below cannot wrap: ValidateBlockRange has already established that
    // offset + storedBytes <= fileBytes_.
    std::ranges::sort(ranges);
    for (std::size_t index = 1U; index < ranges.size(); ++index) {
        if (ranges[index].first < ranges[index - 1U].second) {
            Unmount();
            return AssetPackReadStatus::IndexCorrupt;
        }
    }

    mounted_ = true;
    return AssetPackReadStatus::Success;
}

void AssetPackReader::Unmount() noexcept {
    stream_.close();
    stream_.clear();
    bytes_.clear();
    bytes_.shrink_to_fit();
    artifacts_.clear();
    header_ = AssetPackHeader{};
    path_.clear();
    fileBytes_ = 0U;
    mounted_ = false;
}

const AssetPackArtifactEntry* AssetPackReader::FindArtifact(const AssetBakeDigest& key) const noexcept {
    for (const AssetPackArtifactEntry& artifact : artifacts_) {
        if (artifact.key == key) {
            return &artifact;
        }
    }
    return nullptr;
}

AssetPackReadStatus AssetPackReader::ReadBlock(const AssetPackArtifactEntry& artifact,
                                               std::string_view blockName,
                                               std::vector<std::uint8_t>& out) {
    out.clear();
    if (!mounted_) {
        return AssetPackReadStatus::NotMounted;
    }
    // Identity, not equality: an entry built anywhere but Mount carries offsets this reader
    // never checked against this file.
    const bool belongsToThisPack = std::ranges::any_of(artifacts_, [&artifact](const AssetPackArtifactEntry& mounted) {
        return &mounted == &artifact;
    });
    if (!belongsToThisPack) {
        return AssetPackReadStatus::ArtifactNotFound;
    }

    const auto block = std::ranges::find_if(artifact.blocks, [blockName](const AssetPackBlockEntry& candidate) {
        return store::EqualsIgnoreAsciiCase(candidate.name, blockName);
    });
    if (block == artifact.blocks.end()) {
        return AssetPackReadStatus::BlockNotFound;
    }
    // Re-checked on the way out as well: the cost is three comparisons, and it means a future
    // caller cannot reach the file through an entry that skipped the mount-time validation.
    if (const AssetPackReadStatus status = ValidateBlockRange(*block); status != AssetPackReadStatus::Success) {
        return status;
    }
    return ReadRange(block->offset, block->storedBytes, out);
}

} // namespace kb::assets::bake
