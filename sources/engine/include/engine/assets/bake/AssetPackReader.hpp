#pragma once

#include "engine/assets/bake/AssetPack.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

namespace kb::assets::bake {

struct BakeTargetProfile;

// How a mounted pack gets at its bytes.
enum class AssetPackAccess : std::uint8_t {
    // Seek to a block and read only that block. One open file handle for the whole pack.
    Ranged,
    // Read the entire pack once at mount and hand blocks out of that one buffer.
    //
    // NOT an optimisation and not optional: HTTP range support is a MAY for a server
    // (RFC 9110, 14.2), so a reader that assumes it can ask for a byte range has no answer for
    // a host that ignores Range and returns 200 with the whole body. The same mode is what a
    // packaged Android build falls back to when AAsset_openFileDescriptor refuses. The pack
    // ceiling (kMaxAssetPackBytes) is what makes this a budget rather than a hope.
    WholeFile,
};

// Mounts a pack written by AssetPackWriter and hands out its blocks.
//
// It assumes the file is hostile. Every offset and length in the index is validated against
// the actual size of the file on disk BEFORE it is used, the index is checksummed against what
// the header claims, and the file's own recorded length must match what is on disk -- so a
// truncated pack, an index that describes a different file, and a block that says it is four
// gigabytes long are all refusals rather than reads.
//
// ONE handle, or one buffer, per PACK -- never one per asset. Android caps a process at 4096
// memory allocations (maxMemoryAllocationCount), so anything that scales with the number of
// assets is a wall a big project walks into; OpenCount() exists so that rule can be observed
// rather than asserted.
class AssetPackReader {
public:
    AssetPackReader() = default;
    AssetPackReader(const AssetPackReader&) = delete;
    AssetPackReader& operator=(const AssetPackReader&) = delete;
    AssetPackReader(AssetPackReader&&) = delete;
    AssetPackReader& operator=(AssetPackReader&&) = delete;
    ~AssetPackReader() = default;

    // Reads and validates the header and the index. A failure leaves nothing mounted.
    [[nodiscard]] AssetPackReadStatus Mount(const std::filesystem::path& path,
                                            AssetPackAccess access = AssetPackAccess::Ranged);

    // Mounts bytes owned by the caller without copying them. The span must remain valid until
    // Unmount() or destruction. This is the Android APK path: AAssetManager keeps one
    // uncompressed, zipaligned asset mapping alive and the reader validates and slices that
    // mapping instead of reopening the APK once per artifact.
    [[nodiscard]] AssetPackReadStatus MountMemory(std::span<const std::uint8_t> bytes);
    void Unmount() noexcept;

    [[nodiscard]] bool IsMounted() const noexcept {
        return mounted_;
    }

    [[nodiscard]] const AssetPackHeader& Header() const noexcept {
        return header_;
    }

    // True only for a mounted pack whose recorded stable profile id and full
    // settings fingerprint match `profile`. Packaging tools and runtime hosts
    // use this same check so a release cannot accept what the device refuses.
    [[nodiscard]] bool MatchesTargetProfile(const BakeTargetProfile& profile) const noexcept;

    [[nodiscard]] std::span<const AssetPackArtifactEntry> Artifacts() const noexcept {
        return artifacts_;
    }

    // The streaming fragments of this pack, in the order the writer laid them down. Every one
    // of them was matched to a block of this pack at mount, so a fragment is a second view of
    // bytes the artifact index already describes -- never a region only the fragment index
    // knows about.
    [[nodiscard]] std::span<const AssetPackFragmentEntry> Fragments() const noexcept {
        return fragments_;
    }

    // The artifact under this bake digest, or nullptr. Linear: an index lookup happens once
    // per asset load, and a map would be a second structure to keep honest for no measured
    // gain.
    [[nodiscard]] const AssetPackArtifactEntry* FindArtifact(const AssetBakeDigest& key) const noexcept;

    // Copies one block's bytes out. `artifact` must be one of Artifacts(); passing an entry
    // from somewhere else is refused, because its offsets were never validated against THIS
    // file.
    [[nodiscard]] AssetPackReadStatus ReadBlock(const AssetPackArtifactEntry& artifact,
                                                std::string_view blockName,
                                                std::vector<std::uint8_t>& out);

    // How many times this reader has opened the pack file. Mount opens it once; reading every
    // block of every artifact must leave it at one.
    [[nodiscard]] std::uint64_t OpenCount() const noexcept {
        return openCount_;
    }

private:
    [[nodiscard]] AssetPackReadStatus ReadRange(std::uint64_t offset, std::uint64_t bytes, std::vector<std::uint8_t>& out);
    [[nodiscard]] AssetPackReadStatus ValidateBlockRange(const AssetPackBlockEntry& block) const noexcept;
    [[nodiscard]] AssetPackReadStatus ValidateAndFinishMount();

    std::filesystem::path path_;
    AssetPackAccess access_ = AssetPackAccess::Ranged;
    AssetPackHeader header_{};
    std::vector<AssetPackArtifactEntry> artifacts_;
    std::vector<AssetPackFragmentEntry> fragments_;
    // Ranged mode: the single handle. WholeFile mode: the single buffer. Exactly one of the
    // two is populated while a pack is mounted.
    std::ifstream stream_;
    std::vector<std::uint8_t> bytes_;
    std::span<const std::uint8_t> borrowedBytes_{};
    std::uint64_t fileBytes_ = 0U;
    std::uint64_t openCount_ = 0U;
    bool mounted_ = false;
};

} // namespace kb::assets::bake
