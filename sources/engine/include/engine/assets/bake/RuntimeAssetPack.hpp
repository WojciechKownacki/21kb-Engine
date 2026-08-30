#pragma once

#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/bake/RuntimeAssetManifest.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
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
};

[[nodiscard]] std::string_view ToString(RuntimeAssetPackStatus status) noexcept;

// Validated, multi-asset runtime view of a .kbpack. Mount is all-or-nothing: after any refusal
// neither a manifest nor a partially validated reader remains observable.
class RuntimeAssetPack final {
public:
    [[nodiscard]] RuntimeAssetPackStatus Mount(
        const std::filesystem::path& path,
        const BakeTargetProfile& profile,
        AssetPackAccess access = AssetPackAccess::Ranged);
    [[nodiscard]] RuntimeAssetPackStatus MountMemory(
        std::span<const std::uint8_t> bytes,
        const BakeTargetProfile& profile);
    void Unmount() noexcept;

    [[nodiscard]] bool IsMounted() const noexcept;
    [[nodiscard]] const RuntimeAssetManifest& Manifest() const noexcept;
    [[nodiscard]] const RuntimeAssetManifestEntry* FindAsset(AssetId id) const noexcept;
    [[nodiscard]] const RuntimeAssetManifestEntry* FindAsset(std::string_view virtualPath) const noexcept;

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
    [[nodiscard]] RuntimeAssetPackStatus ReadAuxiliaryFile(
        std::string_view virtualPath,
        std::vector<std::uint8_t>& out);

    [[nodiscard]] const AssetPackHeader& Header() const noexcept;
    [[nodiscard]] std::span<const AssetPackArtifactEntry> Artifacts() const noexcept;

private:
    [[nodiscard]] RuntimeAssetPackStatus FinishMount(const BakeTargetProfile& profile);

    AssetPackReader reader_;
    RuntimeAssetManifest manifest_{};
    // Ranged AssetPackReader owns one seekable stream. Runtime sync loads and the bounded async
    // worker may overlap, so every seek/read sequence is serialized here.
    mutable std::mutex readMutex_;
    bool mounted_ = false;
};

} // namespace kb::assets::bake
