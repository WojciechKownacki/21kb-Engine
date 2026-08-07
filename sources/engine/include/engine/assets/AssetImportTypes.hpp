#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kb::assets {

enum class AssetImportCategory : std::uint16_t {
    Unknown = 0,
    Model,
    Texture,
    Audio,
    Video,
    Animation,
    Material,
    Shader,
    Font,
    Script,
    Scene,
    Data,
    InputAction,
    InputMappingContext,
};

enum class AssetImportItemStatus : std::uint8_t {
    None = 0,
    Created,
    Reused,
    Missing,
    Unsupported,
    Failed,
};

struct AssetMeshImportOptions {
    bool importMaterialSlots = true;
    bool importTextures = false;
    bool importMaterials = false;
    bool combineMeshes = false;
    // The editor routes this mode through the canonical skeletal publication
    // pipeline rather than the generic static-mesh container importer.
    bool importSkeletalMesh = false;
};

struct AssetImportOptions {
    AssetMeshImportOptions mesh{};
};

inline constexpr std::uint16_t kAssetImportOptionMeshDisableMaterialSlots = 1U << 0U;
inline constexpr std::uint16_t kAssetImportOptionMeshImportTextures = 1U << 1U;
inline constexpr std::uint16_t kAssetImportOptionMeshImportMaterials = 1U << 2U;
inline constexpr std::uint16_t kAssetImportOptionMeshCombineMeshes = 1U << 3U;

[[nodiscard]] constexpr std::uint16_t AssetImportOptionFlags(const AssetImportOptions& options) noexcept {
    return (options.mesh.importMaterialSlots ? 0U : kAssetImportOptionMeshDisableMaterialSlots) |
        (options.mesh.importTextures ? kAssetImportOptionMeshImportTextures : 0U) |
        (options.mesh.importMaterials ? kAssetImportOptionMeshImportMaterials : 0U) |
        (options.mesh.combineMeshes ? kAssetImportOptionMeshCombineMeshes : 0U);
}

struct AssetImportItemResult {
    std::filesystem::path sourcePath;
    std::filesystem::path assetPhysicalPath;
    std::filesystem::path metaPhysicalPath;
    std::filesystem::path virtualPath;
    AssetId id{};
    AssetImportCategory category = AssetImportCategory::Unknown;
    AssetImportItemStatus status = AssetImportItemStatus::None;
    std::uint64_t sourceHash = 0;
    std::uint64_t assetHash = 0;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return error.empty() &&
            (status == AssetImportItemStatus::Created || status == AssetImportItemStatus::Reused) &&
            id.IsValid() &&
            !assetPhysicalPath.empty();
    }
};

struct AssetImportResult {
    std::vector<AssetImportItemResult> items;

    [[nodiscard]] std::size_t ImportedCount() const noexcept;
    [[nodiscard]] std::size_t CreatedCount() const noexcept;
    [[nodiscard]] std::size_t ReusedCount() const noexcept;
    [[nodiscard]] std::size_t MissingCount() const noexcept;
    [[nodiscard]] std::size_t UnsupportedCount() const noexcept;
    [[nodiscard]] std::size_t FailedCount() const noexcept;
    [[nodiscard]] bool Succeeded() const noexcept;
};

[[nodiscard]] std::string_view ToString(AssetImportCategory category) noexcept;
[[nodiscard]] std::string_view ToString(AssetImportItemStatus status) noexcept;
[[nodiscard]] std::string_view RuntimeAssetType(AssetImportCategory category) noexcept;

} // namespace kb::assets
