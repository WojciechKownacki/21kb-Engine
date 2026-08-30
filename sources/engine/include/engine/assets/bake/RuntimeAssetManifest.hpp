#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/bake/AssetBakeKey.hpp"
#include "engine/assets/bake/BakeTargetProfile.hpp"
#include "engine/project/ProjectDescriptor.hpp"
#include "engine/project/ProjectSettings.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kb::assets::bake {

inline constexpr std::string_view kRuntimeManifestAssetTypeId = "RuntimeManifest";
inline constexpr std::string_view kSourceAssetTypeId = "SourceAsset";
inline constexpr std::string_view kMaterialShaderAssetTypeId = "MaterialShader";
inline constexpr std::uint32_t kRuntimeAssetManifestVersion = 1U;

// How a manifest reference is meant to be consumed. The digest points at an artifact in the
// same AssetPack; the encoding prevents a raw source blob from being mistaken for cooked GPU
// data merely because both are byte ranges.
enum class RuntimeArtifactEncoding : std::uint8_t {
    SourceBytes,
    BakedMesh,
    BakedTexture,
    MaterialShader,
};

struct RuntimeArtifactReference {
    AssetBakeDigest digest{};
    RuntimeArtifactEncoding encoding = RuntimeArtifactEncoding::SourceBytes;
    // Canonical discriminator within one encoding: texture family, or shader pass/backend/stage.
    // Empty for source bytes and baked meshes.
    std::string qualifier;

    [[nodiscard]] bool operator==(const RuntimeArtifactReference&) const noexcept = default;
};

struct RuntimeAssetManifestEntry {
    AssetId id{};
    std::string type;
    std::string importCategory;
    std::string browseTag;
    std::string name;
    std::string virtualPath;
    std::string sourceExtension;
    std::uint64_t contentHash = 0U;
    bool runtimeLoadable = true;
    std::vector<AssetId> dependencies;
    std::vector<RuntimeArtifactReference> artifacts;

    [[nodiscard]] bool operator==(const RuntimeAssetManifestEntry&) const noexcept = default;
};

// Files that belong to the project but are not registry assets (for example scene metadata).
// They remain path-addressable through the packaged virtual filesystem and cannot silently be
// omitted by a loader-registration change.
struct RuntimeAuxiliaryFileEntry {
    std::string virtualPath;
    std::uint64_t contentHash = 0U;
    AssetBakeDigest artifactDigest{};

    [[nodiscard]] bool operator==(const RuntimeAuxiliaryFileEntry&) const noexcept = default;
};

struct RuntimeAssetManifest {
    std::string targetProfileId;
    std::uint64_t targetProfileHash = 0U;
    kb::project::ProjectDescriptor descriptor{};
    kb::project::ProjectSettings settings{};
    std::vector<RuntimeAssetManifestEntry> assets;
    std::vector<RuntimeAuxiliaryFileEntry> auxiliaryFiles;

};

enum class RuntimeAssetManifestStatus : std::uint8_t {
    Success,
    Malformed,
    UnsupportedVersion,
    InvalidProfile,
    InvalidProject,
    InvalidAsset,
    DuplicateEntry,
    TooLarge,
};

[[nodiscard]] std::string_view ToString(RuntimeAssetManifestStatus status) noexcept;

// Canonical encoding: entries, dependencies and artifact variants are sorted before writing,
// so discovery order and parallel cook completion order cannot affect package bytes.
[[nodiscard]] RuntimeAssetManifestStatus EncodeRuntimeAssetManifest(
    const RuntimeAssetManifest& manifest,
    std::vector<std::uint8_t>& out);

// Hostile-input decoder. `out` changes only on Success.
[[nodiscard]] RuntimeAssetManifestStatus DecodeRuntimeAssetManifest(
    std::span<const std::uint8_t> bytes,
    RuntimeAssetManifest& out);

// Source files use a tiny envelope so an empty source remains a legal non-empty AssetPack
// block and a runtime cannot accidentally treat a shader or cooked geometry block as a file.
[[nodiscard]] bool EncodeRuntimeSourceBlob(
    std::span<const std::uint8_t> sourceBytes,
    std::vector<std::uint8_t>& out);
[[nodiscard]] bool DecodeRuntimeSourceBlob(
    std::span<const std::uint8_t> blob,
    std::span<const std::uint8_t>& sourceBytes) noexcept;

} // namespace kb::assets::bake
