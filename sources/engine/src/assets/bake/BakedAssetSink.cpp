#include "engine/assets/bake/BakedAssetSink.hpp"

#include "scene/asset/io/SceneAssetBinaryIO.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace kb::assets::bake {
namespace {

// Hidden so a developer browsing the loose cache never mistakes a half-written
// artifact for a published one, and so a stale directory left by a killed bake
// is obviously not part of the store.
constexpr std::string_view kStagingDirectoryName = ".kbbakestaging";

// Enough attempts to step past the staging directories of every bake process
// that could plausibly be publishing the same key at the same moment.
constexpr std::uint64_t kMaxStagingAttempts = 4096U;

// A staging directory is named "<32 hex digits>.<counter>"; a 64-bit counter
// never prints more than 20 decimal digits.
constexpr std::size_t kMaxStagingEntryNameLength = 32U + 1U + 20U;

// WriteBytesAtomically stages every block as "<block>.kbblock.tmp" and renames
// it into place, so the path budget has to cover that suffix as well.
constexpr std::size_t kAtomicWriteSuffixLength = 4U;

[[nodiscard]] constexpr char ToLowerAscii(char character) noexcept {
    return (character >= 'A' && character <= 'Z') ? static_cast<char>(character - 'A' + 'a') : character;
}

[[nodiscard]] bool EqualsIgnoreAsciiCase(std::string_view lhs, std::string_view rhs) noexcept {
    return std::ranges::equal(lhs, rhs, [](char left, char right) noexcept {
        return ToLowerAscii(left) == ToLowerAscii(right);
    });
}

[[nodiscard]] constexpr bool IsPowerOfTwo(std::uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] std::filesystem::path BlockFileName(std::string_view blockName) {
    return std::filesystem::path{ std::string{ blockName } + std::string{ kBakedAssetBlockExtension } };
}

// Win32 resolves an ordinary path against MAX_PATH (260 characters); only the
// extended-length prefix lifts that ceiling to 32767. A loose-cache path built
// from names at kMaxBakeCacheNameBytes spends about 240 characters before the
// store root is even counted, and an ordinary path then fails in the worst
// possible way: the directory creates and the rename report success, the block
// inside cannot be opened, and remove_all cannot delete it again. Normalising
// the root once makes every path derived from it extended-length, including the
// ones AssetDirectory and BlockPath hand back to a reader.
[[nodiscard]] std::filesystem::path NormalizeStoreRoot(std::filesystem::path root) {
#if defined(_WIN32)
    if (root.empty() || root.native().starts_with(LR"(\\?\)") || root.native().starts_with(LR"(\\.\)")) {
        return root;
    }
    std::error_code error;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, error);
    if (error) {
        return root;
    }
    // An extended-length path is passed to the object manager verbatim: no '/'
    // separators, no '.' or '..' components and no trailing separator, or the
    // final name resolves to something other than the directory meant.
    const std::filesystem::path normalized = absoluteRoot.lexically_normal();
    std::wstring native = normalized.native();
    std::ranges::replace(native, L'/', L'\\');
    const std::size_t rootLength = normalized.root_path().native().size();
    while (native.size() > rootLength && native.back() == L'\\') {
        native.pop_back();
    }
    if (native.starts_with(LR"(\\)")) {
        return std::filesystem::path{ std::wstring{ LR"(\\?\UNC)" } + native.substr(1U) };
    }
    return std::filesystem::path{ std::wstring{ LR"(\\?\)" } + native };
#else
    return root;
#endif
}

// Longest path the sink can be asked to create inside `directory`: a separator,
// a block name at the documented maximum, the block extension, and the
// temporary suffix the atomic write adds on the way.
[[nodiscard]] std::size_t WorstCaseBlockPathLength(const std::filesystem::path& directory) {
    return directory.native().size() + 1U + kMaxBakeCacheNameBytes + kBakedAssetBlockExtension.size() +
        kAtomicWriteSuffixLength;
}

// Same budget for the staging side, which adds one more component: the
// per-attempt staging directory named after the digest.
[[nodiscard]] std::size_t WorstCaseStagingPathLength(const std::filesystem::path& root) {
    return WorstCaseBlockPathLength(root / kStagingDirectoryName) + 1U + kMaxStagingEntryNameLength;
}

// What "already published" has to mean. `exists` is not it: an empty directory,
// a directory that lost a block, or a plain file left on the path all pass
// `exists` while holding none of the bytes the key promises. Treating one of
// those as a finished artifact drops the blocks we just staged and poisons a
// content-addressed key permanently, so completeness is checked block by block
// against what this bake actually wrote.
[[nodiscard]] bool PublishedArtifactIsComplete(const std::filesystem::path& directory,
                                               const std::vector<std::string>& blocks) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        return false;
    }
    for (const std::string& block : blocks) {
        const std::filesystem::path blockPath = directory / BlockFileName(block);
        std::error_code blockError;
        if (!std::filesystem::is_regular_file(blockPath, blockError)) {
            return false;
        }
        const std::uintmax_t size = std::filesystem::file_size(blockPath, blockError);
        if (blockError || size == 0U) {
            return false;
        }
    }
    return true;
}

// Clears whatever is sitting on the publish path so the staging directory can
// be renamed onto it. Only ever called once the entry has been shown NOT to be
// a complete artifact, and publication is a single rename, so nothing this
// removes was ever a readable artifact.
[[nodiscard]] bool ClearPublishDestination(const std::filesystem::path& destination) {
    std::error_code statusError;
    const std::filesystem::file_status status = std::filesystem::symlink_status(destination, statusError);
    if (status.type() == std::filesystem::file_type::not_found) {
        return true;
    }
    if (statusError) {
        return false;
    }
    std::error_code removeError;
    std::filesystem::remove_all(destination, removeError);
    return !removeError;
}

// A process killed mid-bake never runs its abort, so its staging directory
// outlives it and no successful bake of the same key would otherwise touch it
// again. Nothing in the store records which staging directory belongs to a live
// bake, but one that has not been written to for a whole day is not live: every
// block write refreshes its directory's modification time. Sweeping on the way
// in bounds the debris and lets DiscardStaging prune the hidden root once it
// runs empty.
void SweepOrphanedStaging(const std::filesystem::path& stagingRoot) {
    std::error_code error;
    std::filesystem::directory_iterator iterator{ stagingRoot, error };
    if (error) {
        return;
    }

    const std::filesystem::file_time_type cutoff = std::filesystem::file_time_type::clock::now() -
        std::chrono::duration_cast<std::filesystem::file_time_type::duration>(kOrphanedStagingMaxAge);

    // Collected first: removing entries while the directory enumeration is open
    // is not something either target file system guarantees.
    std::vector<std::filesystem::path> orphans;
    const std::filesystem::directory_iterator end{};
    while (iterator != end) {
        const std::filesystem::path candidate = iterator->path();
        std::error_code stampError;
        const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(candidate, stampError);
        if (!stampError && stamp < cutoff) {
            orphans.push_back(candidate);
        }
        iterator.increment(error);
        if (error) {
            break;
        }
    }

    for (const std::filesystem::path& orphan : orphans) {
        std::error_code removeError;
        std::filesystem::remove_all(orphan, removeError);
    }
}

} // namespace

std::string_view ToString(BakedAssetSinkStatus status) noexcept {
    switch (status) {
    case BakedAssetSinkStatus::Success:
        return "Success";
    case BakedAssetSinkStatus::InvalidKey:
        return "InvalidKey";
    case BakedAssetSinkStatus::InvalidAssetType:
        return "InvalidAssetType";
    case BakedAssetSinkStatus::InvalidBlockName:
        return "InvalidBlockName";
    case BakedAssetSinkStatus::InvalidAlignment:
        return "InvalidAlignment";
    case BakedAssetSinkStatus::EmptyBlock:
        return "EmptyBlock";
    case BakedAssetSinkStatus::AssetAlreadyOpen:
        return "AssetAlreadyOpen";
    case BakedAssetSinkStatus::NoAssetOpen:
        return "NoAssetOpen";
    case BakedAssetSinkStatus::DuplicateBlock:
        return "DuplicateBlock";
    case BakedAssetSinkStatus::MissingPrimaryBlock:
        return "MissingPrimaryBlock";
    case BakedAssetSinkStatus::PathTooLong:
        return "PathTooLong";
    case BakedAssetSinkStatus::WriteFailed:
        return "WriteFailed";
    }
    return "Unknown";
}

LooseBakedAssetSink::LooseBakedAssetSink(std::filesystem::path root)
    : root_{ NormalizeStoreRoot(std::move(root)) } {}

LooseBakedAssetSink::~LooseBakedAssetSink() {
    // An artifact still open here means the bake died mid-way. Publishing it
    // would put a corrupt entry under a key that claims to be complete.
    AbortAsset();
}

std::filesystem::path LooseBakedAssetSink::AssetDirectory(const BakedAssetDescriptor& descriptor) const {
    if (!descriptor.key.IsValid() || !IsValidBakeCacheName(descriptor.assetTypeId)) {
        return {};
    }
    std::filesystem::path directory =
        root_ / descriptor.assetTypeId / descriptor.key.targetProfileId / descriptor.key.ToString();
    if (WorstCaseBlockPathLength(directory) > kMaxBakeStorePathLength) {
        return {};
    }
    return directory;
}

std::filesystem::path LooseBakedAssetSink::BlockPath(const BakedAssetDescriptor& descriptor,
                                                     std::string_view blockName) const {
    if (!IsValidBakeCacheName(blockName)) {
        return {};
    }
    const std::filesystem::path directory = AssetDirectory(descriptor);
    if (directory.empty()) {
        return {};
    }
    return directory / BlockFileName(blockName);
}

BakedAssetSinkStatus LooseBakedAssetSink::BeginAsset(const BakedAssetDescriptor& descriptor) {
    if (open_) {
        return BakedAssetSinkStatus::AssetAlreadyOpen;
    }
    if (!descriptor.key.IsValid()) {
        return BakedAssetSinkStatus::InvalidKey;
    }
    if (!IsValidBakeCacheName(descriptor.assetTypeId)) {
        return BakedAssetSinkStatus::InvalidAssetType;
    }

    // Checked BEFORE the first directory is created, and against the worst case
    // rather than against the names this artifact happens to use: once
    // BeginAsset succeeds, no legal block name can produce a path the platform
    // refuses, so a later write never has to fail for length.
    const std::filesystem::path publish = AssetDirectory(descriptor);
    if (publish.empty() || WorstCaseStagingPathLength(root_) > kMaxBakeStorePathLength) {
        return BakedAssetSinkStatus::PathTooLong;
    }

    const std::filesystem::path stagingRoot = root_ / kStagingDirectoryName;
    std::error_code error;
    std::filesystem::create_directories(stagingRoot, error);
    if (error) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    SweepOrphanedStaging(stagingRoot);

    // A fresh, exclusively created directory per attempt: two bakers publishing
    // the same key concurrently must not stage into the same place.
    const std::string digest = descriptor.key.ToString();
    std::filesystem::path staging;
    for (std::uint64_t attempt = 0U; attempt < kMaxStagingAttempts; ++attempt) {
        const std::filesystem::path candidate =
            stagingRoot / (digest + "." + std::to_string(stagingCounter_ + attempt));
        std::error_code createError;
        if (std::filesystem::create_directory(candidate, createError)) {
            staging = candidate;
            stagingCounter_ += attempt + 1U;
            break;
        }
        if (createError) {
            return BakedAssetSinkStatus::WriteFailed;
        }
    }
    if (staging.empty()) {
        return BakedAssetSinkStatus::WriteFailed;
    }

    stagingDirectory_ = staging;
    publishDirectory_ = publish;
    writtenBlocks_.clear();
    primaryWritten_ = false;
    open_ = true;
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus LooseBakedAssetSink::WritePrimaryBlock(std::span<const std::uint8_t> bytes,
                                                            std::uint32_t alignmentBytes) {
    if (!open_) {
        return BakedAssetSinkStatus::NoAssetOpen;
    }
    if (primaryWritten_) {
        return BakedAssetSinkStatus::DuplicateBlock;
    }
    if (!IsPowerOfTwo(alignmentBytes)) {
        return BakedAssetSinkStatus::InvalidAlignment;
    }
    if (bytes.empty()) {
        return BakedAssetSinkStatus::EmptyBlock;
    }
    // Alignment is deliberately not acted on: every block here starts at offset
    // zero of its own file, which satisfies any alignment. A container sink is
    // the one that has to place the block, and it gets the same value.
    if (!kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(
            stagingDirectory_ / BlockFileName(kBakedAssetPrimaryBlockName), bytes)) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    writtenBlocks_.emplace_back(kBakedAssetPrimaryBlockName);
    primaryWritten_ = true;
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus LooseBakedAssetSink::WriteAuxiliaryBlock(const BakedAssetBlock& block,
                                                              std::span<const std::uint8_t> bytes) {
    if (!open_) {
        return BakedAssetSinkStatus::NoAssetOpen;
    }
    if (!IsValidBakeCacheName(block.name) || EqualsIgnoreAsciiCase(block.name, kBakedAssetPrimaryBlockName)) {
        return BakedAssetSinkStatus::InvalidBlockName;
    }
    if (!IsPowerOfTwo(block.alignmentBytes)) {
        return BakedAssetSinkStatus::InvalidAlignment;
    }
    // Case-insensitive: two blocks whose names differ only in case would become
    // one file the moment the artifact lands on a case-insensitive file system.
    const bool duplicate = std::ranges::any_of(writtenBlocks_, [&block](const std::string& written) noexcept {
        return EqualsIgnoreAsciiCase(written, block.name);
    });
    if (duplicate) {
        return BakedAssetSinkStatus::DuplicateBlock;
    }
    // An empty block is a baker bug, not a zero-length payload: it would publish
    // a file whose only possible reading is "this artifact is truncated".
    if (bytes.empty()) {
        return BakedAssetSinkStatus::EmptyBlock;
    }
    if (!kb::scene::SceneAssetBinaryIO::WriteBytesAtomically(stagingDirectory_ / BlockFileName(block.name), bytes)) {
        return BakedAssetSinkStatus::WriteFailed;
    }
    writtenBlocks_.emplace_back(block.name);
    return BakedAssetSinkStatus::Success;
}

BakedAssetSinkStatus LooseBakedAssetSink::CommitAsset() {
    if (!open_) {
        return BakedAssetSinkStatus::NoAssetOpen;
    }
    if (!primaryWritten_) {
        // The artifact stays open: a missing payload is a caller mistake it can
        // still repair, and silently discarding the auxiliary blocks it already
        // handed over would hide that mistake.
        return BakedAssetSinkStatus::MissingPrimaryBlock;
    }

    std::error_code error;
    std::filesystem::create_directories(publishDirectory_.parent_path(), error);
    if (error) {
        return BakedAssetSinkStatus::WriteFailed;
    }

    if (PublishedArtifactIsComplete(publishDirectory_, writtenBlocks_)) {
        // The store is content-addressed: a COMPLETE artifact already sitting
        // under this key was produced from the same inputs by the same baker,
        // so it is the same artifact. Keep the published copy and drop ours.
        DiscardStaging();
        return BakedAssetSinkStatus::Success;
    }

    // Anything else on the path -- an empty directory, one missing a block, or
    // a plain file -- was never published by this sink, because publication
    // renames a directory that already holds every block. Clear it, or the key
    // would answer "already baked" forever while holding nothing readable.
    if (!ClearPublishDestination(publishDirectory_)) {
        return BakedAssetSinkStatus::WriteFailed;
    }

    // ONE operation, on the directory as a whole. Publishing block by block
    // would make a partial artifact observable under a key that claims to be
    // complete, which is the entire reason for staging in the first place.
    std::error_code renameError;
    std::filesystem::rename(stagingDirectory_, publishDirectory_, renameError);
    if (renameError) {
        // Lost a race against another baker publishing the same key between the
        // check above and here; that publication is as good as ours, provided
        // it is actually finished.
        if (PublishedArtifactIsComplete(publishDirectory_, writtenBlocks_)) {
            DiscardStaging();
            return BakedAssetSinkStatus::Success;
        }
        return BakedAssetSinkStatus::WriteFailed;
    }

    DiscardStaging();
    return BakedAssetSinkStatus::Success;
}

void LooseBakedAssetSink::AbortAsset() noexcept {
    if (!open_) {
        return;
    }
    DiscardStaging();
}

void LooseBakedAssetSink::DiscardStaging() noexcept {
    std::error_code error;
    std::filesystem::remove_all(stagingDirectory_, error);
    // Prune the staging root as soon as it runs empty, so an aborted bake
    // leaves the store byte-for-byte as it was. remove() only succeeds on an
    // empty directory, which is exactly the condition we want: a staging
    // directory another bake is still filling, or one too young for
    // SweepOrphanedStaging to have taken, keeps the root alive.
    std::filesystem::remove(root_ / kStagingDirectoryName, error);

    stagingDirectory_.clear();
    publishDirectory_.clear();
    writtenBlocks_.clear();
    primaryWritten_ = false;
    open_ = false;
}

} // namespace kb::assets::bake
