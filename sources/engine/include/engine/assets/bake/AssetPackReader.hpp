#pragma once

#include "engine/assets/bake/AssetPack.hpp"
#include "engine/assets/bake/AssetPackSeal.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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
    // ceiling (kMaxWholeFileAssetPackBytes) is what makes this a budget rather than a hope.
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
//
// A SEALED pack (AssetPackSeal.hpp) has its signature checked at mount, before its index is
// decoded, and every block's SHA-512 checked when the block is read. What the reader demands
// beyond that is its AssetPackTrust: a packaged player requires the release key, so an unsigned
// or foreign pack never mounts.
//
// A COMPRESSED block is checked as it is stored -- the seal's SHA-512 covers the compressed
// (and, when encrypted, the encrypted) bytes -- before a byte of it is decrypted or decoded, so a
// tampered compressed block never reaches the decoder of a sealed pack.
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
                                            AssetPackAccess access = AssetPackAccess::Ranged,
                                            const AssetPackTrust& trust = {});

    // Mounts bytes owned by the caller without copying them. The span must remain valid until
    // Unmount() or destruction. This is the Android APK path: AAssetManager keeps one
    // uncompressed, zipaligned asset mapping alive and the reader validates and slices that
    // mapping instead of reopening the APK once per artifact.
    [[nodiscard]] AssetPackReadStatus MountMemory(std::span<const std::uint8_t> bytes,
                                                  const AssetPackTrust& trust = {});
    void Unmount() noexcept;

    [[nodiscard]] bool IsMounted() const noexcept {
        return mounted_;
    }

    [[nodiscard]] const AssetPackHeader& Header() const noexcept {
        return header_;
    }

    // AssetPackCatalogIdentity of this pack, computed at mount from the bytes it verified.
    [[nodiscard]] const AssetBakeDigest& CatalogIdentity() const noexcept {
        return catalogIdentity_;
    }

    // The file a Ranged or WholeFile mount opened; empty for MountMemory.
    [[nodiscard]] const std::filesystem::path& Path() const noexcept {
        return path_;
    }

    // The borrowed bytes of a MountMemory mount, or the whole-file buffer; empty for a Ranged
    // mount. Valid while the pack stays mounted.
    [[nodiscard]] std::span<const std::uint8_t> ResidentBytes() const noexcept {
        if (!borrowedBytes_.empty()) {
            return borrowedBytes_;
        }
        return bytes_;
    }

    // The verified seal of a sealed pack, or nullptr for an unsigned one.
    [[nodiscard]] const AssetPackSeal* Seal() const noexcept {
        return seal_.has_value() ? &*seal_ : nullptr;
    }

    // The 64-byte message the seal's signature covers. A release manifest records it to bind
    // this exact pack to a release without hashing the pack a second time.
    [[nodiscard]] const kb::security::Sha512Digest& SealDigest() const noexcept {
        return sealDigest_;
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

    // The block's bytes exactly as they lie in the file -- compressed and, in a sealed pack,
    // encrypted -- after the same checks ReadBlock makes on the way to the payload. Sealing signs
    // these bytes.
    [[nodiscard]] AssetPackReadStatus ReadStoredBlock(const AssetPackArtifactEntry& artifact,
                                                      std::string_view blockName,
                                                      std::vector<std::uint8_t>& out);

    // The block of `artifact` (one of Artifacts()) named `blockName`, or nullptr.
    [[nodiscard]] const AssetPackBlockEntry* FindBlock(
        const AssetPackArtifactEntry& artifact,
        std::string_view blockName) const noexcept;

    // Turns stored bytes of `block` that a caller read itself -- an asynchronous reader with its
    // own file handle -- into the payload: checks the seal digest, decrypts, decompresses and
    // checks the payload digest, exactly as ReadBlock does. `block` must be an entry of this
    // mounted pack. `bytes` holds the stored bytes on entry and the payload on Success; it is
    // cleared on any refusal. Touches no mutable state, so worker threads may call it at once.
    [[nodiscard]] AssetPackReadStatus DecodeStoredBlock(
        const AssetPackBlockEntry& block,
        std::vector<std::uint8_t>& bytes) const;

    // How many times this reader has opened the pack file. Mount opens it once; reading every
    // block of every artifact must leave it at one.
    [[nodiscard]] std::uint64_t OpenCount() const noexcept {
        return openCount_;
    }

private:
    [[nodiscard]] AssetPackReadStatus ReadRange(std::uint64_t offset, std::uint64_t bytes, std::vector<std::uint8_t>& out);
    [[nodiscard]] AssetPackReadStatus LocateBlock(
        const AssetPackArtifactEntry& artifact,
        std::string_view blockName,
        const AssetPackBlockEntry*& out) const noexcept;
    [[nodiscard]] AssetPackReadStatus ValidateBlockRange(const AssetPackBlockEntry& block) const noexcept;
    [[nodiscard]] AssetPackReadStatus ValidateAndFinishMount(const AssetPackTrust& trust);
    [[nodiscard]] AssetPackReadStatus VerifySeal(
        std::span<const std::uint8_t> header,
        std::span<const std::uint8_t> index,
        std::span<const std::uint8_t> fragments,
        std::span<const std::uint8_t> seal,
        const AssetPackTrust& trust);

    std::filesystem::path path_;
    AssetPackAccess access_ = AssetPackAccess::Ranged;
    AssetPackHeader header_{};
    AssetBakeDigest catalogIdentity_{};
    std::vector<AssetPackArtifactEntry> artifacts_;
    std::vector<AssetPackFragmentEntry> fragments_;
    std::optional<AssetPackSeal> seal_;
    kb::security::Sha512Digest sealDigest_{};
    std::optional<kb::security::AeadKey> contentKey_;
    // The seal was made by the key the trust required, so the signature already vouches for
    // every index digest and a block's SHA-512 match is the whole check.
    bool sealTrusted_ = false;
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
