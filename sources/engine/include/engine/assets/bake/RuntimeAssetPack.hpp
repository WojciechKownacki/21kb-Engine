#pragma once

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/AssetPackSet.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace kb::assets::bake {

struct RuntimeAssetPayloadBlock {
    std::string name;
    BakedAssetBlockResidency residency = BakedAssetBlockResidency::Resident;
    std::uint32_t alignmentBytes = 1U;
    std::optional<BakedAssetBlockFragment> fragment;
    std::vector<std::uint8_t> bytes;
};

struct RuntimeAssetPayload {
    AssetBakeDigest digest{};
    RuntimeArtifactEncoding encoding = RuntimeArtifactEncoding::SourceBytes;
    std::string qualifier;
    std::vector<RuntimeAssetPayloadBlock> blocks;
};

enum class RuntimeAssetPackStatus : std::uint8_t {
    Success,
    ContainerRejected,
    ProfileMismatch,
    ManifestMissing,
    ManifestDuplicate,
    ManifestCorrupt,
    ReferenceMissing,
    ReferenceTypeMismatch,
    DependencyMissing,
    OrphanArtifact,
    ArtifactCorrupt,
    SourceCorrupt,
    NotMounted,
    // The pack set index is unreadable or malformed, or a pack's own header disagrees with the
    // role, label, patch level or position the index gives it.
    PackSetInvalid,
    // A chunk or patch was cooked against another base pack than the one mounted.
    PackSetBaseMismatch,
};

[[nodiscard]] std::string_view ToString(RuntimeAssetPackStatus status) noexcept;

// One pack of a set, in mount order. `role`, `label` and `patchLevel` are what the caller (the
// pack set index) expects; the pack's sealed header must say the same.
struct RuntimeAssetPackMount {
    std::filesystem::path path;
    AssetPackRole role = AssetPackRole::Base;
    std::string label;
    std::uint32_t patchLevel = 0U;
    // The pack's own content key, wrapped under the trust's content key (the release's anchor
    // key), for a pack encrypted under another key. Without one the pack is read with the
    // trust's content key itself.
    std::optional<WrappedAssetPackKey> wrappedContentKey;
};

// Where one block of a mounted artifact lies: which pack of the set, and its validated entry.
// The entry belongs to that pack's reader and stays valid while the set stays mounted.
struct RuntimeAssetBlockLocation {
    std::uint32_t container = 0U;
    const AssetPackBlockEntry* block = nullptr;
};

// Validated, multi-asset runtime view of one .kbpack -- or of a whole pack set: a base pack, its
// chunk packs and its patch packs, mounted in order and seen as one catalogue.
//
// Mount is all-or-nothing: after any refusal neither a manifest nor a partially validated reader
// remains observable. `trust` is passed to every container reader: a packaged player requires
// its release key there, so every pack of a set -- a patch included -- must be sealed by it.
//
// Within a set, assets are looked up in one merged manifest. A chunk adds assets and may not
// redefine one the base or another chunk already has. A patch replaces every asset it lists
// (same id or same virtual path) together with its artifacts, replaces auxiliary files of the
// same path, removes the assets and files its tombstones name, and supplies the project settings;
// later patches win over earlier ones.
// Dependencies and the default map are checked across the whole set. A chunk or patch must
// name the mounted base's catalogue identity, so packs from another cook never mix.
class RuntimeAssetPack final {
public:
    [[nodiscard]] RuntimeAssetPackStatus Mount(
        const std::filesystem::path& path,
        const BakeTargetProfile& profile,
        AssetPackAccess access = AssetPackAccess::Ranged,
        const AssetPackTrust& trust = {});
    [[nodiscard]] RuntimeAssetPackStatus MountMemory(
        std::span<const std::uint8_t> bytes,
        const BakeTargetProfile& profile,
        const AssetPackTrust& trust = {});
    // Mounts the packs in order as one set; packs.front() must be the base.
    [[nodiscard]] RuntimeAssetPackStatus MountSet(
        std::span<const RuntimeAssetPackMount> packs,
        const BakeTargetProfile& profile,
        AssetPackAccess access = AssetPackAccess::Ranged,
        const AssetPackTrust& trust = {});
    // Reads a pack set index (AssetPackSet.hpp) and mounts what it names.
    [[nodiscard]] RuntimeAssetPackStatus MountSetIndex(
        const std::filesystem::path& indexPath,
        const BakeTargetProfile& profile,
        AssetPackAccess access = AssetPackAccess::Ranged,
        const AssetPackTrust& trust = {});
    void Unmount() noexcept;

    [[nodiscard]] bool IsMounted() const noexcept;
    // The merged manifest of everything mounted.
    [[nodiscard]] const RuntimeAssetManifest& Manifest() const noexcept;
    [[nodiscard]] const RuntimeAssetManifestEntry* FindAsset(AssetId id) const noexcept;
    [[nodiscard]] const RuntimeAssetManifestEntry* FindAsset(std::string_view virtualPath) const noexcept;
    // Which pack of the set supplies this asset, or nullopt.
    [[nodiscard]] std::optional<std::uint32_t> AssetContainer(AssetId id) const noexcept;

    [[nodiscard]] AssetPackReadStatus ReadArtifactBlock(
        const AssetBakeDigest& digest,
        std::string_view blockName,
        std::vector<std::uint8_t>& out);
    [[nodiscard]] RuntimeAssetPackStatus ReadSourceFile(
        const AssetBakeDigest& digest,
        std::vector<std::uint8_t>& out);
    [[nodiscard]] RuntimeAssetPackStatus ReadAssetPayload(
        AssetId asset,
        RuntimeArtifactEncoding encoding,
        std::string_view qualifier,
        RuntimeAssetPayload& out);
    // Like ReadAssetPayload, but reads only the primary block and the blocks `include` accepts;
    // the others are returned with their names, residency and fragments and no bytes. This is
    // how a streamed asset is loaded with its low detail first.
    [[nodiscard]] RuntimeAssetPackStatus ReadAssetPayloadBlocks(
        AssetId asset,
        RuntimeArtifactEncoding encoding,
        std::string_view qualifier,
        const std::function<bool(const AssetPackBlockEntry&)>& include,
        RuntimeAssetPayload& out);
    [[nodiscard]] RuntimeAssetPackStatus ReadAuxiliaryFile(
        std::string_view virtualPath,
        std::vector<std::uint8_t>& out);

    // Thread-safe block access for asynchronous readers that do their own I/O: locate a block,
    // read `block->storedBytes` at `block->offset` from ContainerPath(container) (or from
    // ContainerResidentBytes when that is not empty), then decode the stored bytes here.
    [[nodiscard]] AssetPackReadStatus LocateArtifactBlock(
        const AssetBakeDigest& digest,
        std::string_view blockName,
        RuntimeAssetBlockLocation& out) const;
    [[nodiscard]] AssetPackReadStatus DecodeStoredArtifactBlock(
        const RuntimeAssetBlockLocation& location,
        std::vector<std::uint8_t>& bytes) const;

    // The base pack's header, artifacts and seal: the whole story for a single-pack mount.
    [[nodiscard]] const AssetPackHeader& Header() const noexcept;
    [[nodiscard]] std::span<const AssetPackArtifactEntry> Artifacts() const noexcept;
    // Why the container reader refused the last mount (Success when it did not).
    [[nodiscard]] AssetPackReadStatus ContainerStatus() const noexcept;
    // Which pack of the set the last refusal was about (0 for the base).
    [[nodiscard]] std::uint32_t RefusedContainer() const noexcept;
    [[nodiscard]] const AssetPackSeal* Seal() const noexcept;
    [[nodiscard]] const kb::security::Sha512Digest& SealDigest() const noexcept;

    // Every pack of the set, in mount order; container 0 is the base. ContainerPath is the path
    // the pack was mounted from, as the caller (or the pack set index) named it.
    [[nodiscard]] std::uint32_t ContainerCount() const noexcept;
    [[nodiscard]] const AssetPackHeader& ContainerHeader(std::uint32_t container) const noexcept;
    [[nodiscard]] std::span<const AssetPackArtifactEntry> ContainerArtifacts(std::uint32_t container) const noexcept;
    [[nodiscard]] const std::filesystem::path& ContainerPath(std::uint32_t container) const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> ContainerResidentBytes(std::uint32_t container) const noexcept;
    [[nodiscard]] const AssetPackSeal* ContainerSeal(std::uint32_t container) const noexcept;
    [[nodiscard]] const kb::security::Sha512Digest& ContainerSealDigest(std::uint32_t container) const noexcept;
    [[nodiscard]] const AssetBakeDigest& ContainerCatalogIdentity(std::uint32_t container) const noexcept;
    // Reads one block of one pack's artifact, whichever pack of the set would answer its digest.
    [[nodiscard]] AssetPackReadStatus ReadContainerBlock(
        std::uint32_t container,
        const AssetPackArtifactEntry& artifact,
        std::string_view blockName,
        std::vector<std::uint8_t>& out);

private:
    struct Container {
        // As the caller named it; empty for a pack mounted from memory.
        std::filesystem::path path;
        std::unique_ptr<AssetPackReader> reader;
        // Ranged AssetPackReader owns one seekable stream. Runtime sync loads and the bounded
        // async worker may overlap, so every seek/read sequence on it is serialized here.
        std::unique_ptr<std::mutex> readMutex;
        RuntimeAssetManifest manifest;
    };
    struct ArtifactOwner {
        std::uint32_t container = 0U;
        const AssetPackArtifactEntry* artifact = nullptr;
    };

    [[nodiscard]] RuntimeAssetPackStatus ValidateContainer(
        Container& container,
        const BakeTargetProfile& profile);
    [[nodiscard]] RuntimeAssetPackStatus FinishMount(
        std::span<const RuntimeAssetPackMount> expected,
        const BakeTargetProfile& profile);
    [[nodiscard]] const ArtifactOwner* FindArtifactOwner(const AssetBakeDigest& digest) const noexcept;
    [[nodiscard]] RuntimeAssetPackStatus ReadPayload(
        AssetId asset,
        RuntimeArtifactEncoding encoding,
        std::string_view qualifier,
        const std::function<bool(const AssetPackBlockEntry&)>* include,
        RuntimeAssetPayload& out);

    std::vector<Container> containers_;
    RuntimeAssetManifest manifest_{};
    // Parallel to manifest_.assets: the pack that supplies each merged asset.
    std::vector<std::uint32_t> assetContainers_;
    // Parallel to manifest_.auxiliaryFiles.
    std::vector<std::uint32_t> auxiliaryContainers_;
    std::unordered_map<std::uint64_t, std::size_t> assetsById_;
    std::unordered_map<std::string, std::size_t> assetsByPath_;
    // Content-addressed: the same digest names the same bytes in any pack, and a later pack of
    // the set answers it first.
    std::map<AssetBakeDigest, ArtifactOwner> artifacts_;
    AssetPackReadStatus containerStatus_ = AssetPackReadStatus::NotMounted;
    std::uint32_t refusedContainer_ = 0U;
    bool mounted_ = false;
};

} // namespace kb::assets::bake
