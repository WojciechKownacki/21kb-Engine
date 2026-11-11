#pragma once

#include "engine/assets/AssetId.hpp"

#include <array>
#include <cmath>
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
    // A reimport replaced an existing asset in place (same id, same path).
    Updated,
};

// What a reimport does when an asset it would overwrite was edited since the import wrote it (see AssetImportRecord).
enum class AssetImportConflictPolicy : std::uint8_t {
    Report,    // refuse, change nothing and list the edited assets
    KeepLocal, // leave the edited assets as they are and update the rest
    Overwrite, // replace everything; the old files are kept in a backup that can be restored
};

enum class AssetImportChangeKind : std::uint8_t {
    Added,         // the import writes an asset that is not there
    Changed,       // the file differs from what the import would write, and nobody edited it since the last import
    Unchanged,     // the file is what the import would write
    LocalOverride, // the file was edited since the last import, and differs from what the import would write
    Removed,       // the last import wrote it, this one does not
};

struct AssetImportChange {
    std::filesystem::path virtualPath;
    std::string assetType;
    AssetImportChangeKind kind = AssetImportChangeKind::Unchanged;
    std::uint64_t recordedHash = 0U; // what the last import wrote (0 when unknown)
    std::uint64_t currentHash = 0U;  // the file now (0 when it is not there)
    std::uint64_t newHash = 0U;      // what this import would write (0 for Removed)
};

// The size and orientation a model is brought in with: a uniform scale and a rotation,
// applied to the mesh data after
// the automatic right- to left-handed conversion. Rotation is engine Euler degrees (Z, then X,
// then Y, as kb::math::FromEulerDegrees and the Inspector).
struct AssetMeshImportTransform {
    float scale = 1.0F;
    std::array<float, 3> rotationDegrees{ 0.0F, 0.0F, 0.0F };

    [[nodiscard]] bool IsIdentity() const noexcept {
        return scale == 1.0F && rotationDegrees[0] == 0.0F && rotationDegrees[1] == 0.0F && rotationDegrees[2] == 0.0F;
    }
    // A scale above zero and finite angles; anything else is refused at import.
    [[nodiscard]] bool IsValid() const noexcept {
        return std::isfinite(scale) && scale > 0.0F && std::isfinite(rotationDegrees[0]) &&
            std::isfinite(rotationDegrees[1]) && std::isfinite(rotationDegrees[2]);
    }
    [[nodiscard]] bool operator==(const AssetMeshImportTransform&) const noexcept = default;
};

// How the levels of detail of a model are built, one record per mesh asset. The editor, the command line, the cooker and a game that
// draws the source file all read this same record, so a level looks the same wherever it was built.
inline constexpr std::uint32_t kMeshLodMaxLevels = 8U;

// One coarser level in Manual mode.
struct AssetMeshLodLevel {
    // The share of the finest level's triangles the level keeps.
    float trianglePercent = 0.0F;
    // The most deviation the level may add beyond the one above it, as a percent of the mesh's own extent.
    float errorPercent = 20.0F;

    [[nodiscard]] bool operator==(const AssetMeshLodLevel&) const noexcept = default;
};

struct AssetMeshLodSettings {
    // Auto derives every level from keepPercent and errorPercent; Manual takes each level from `levels`.
    bool manual = false;
    // Levels including the finest one; 1 means the mesh has no chain.
    std::uint8_t levelCount = static_cast<std::uint8_t>(kMeshLodMaxLevels);
    // Auto: each level keeps this share of the triangles of the level above it.
    float keepPercent = 50.0F;
    // Auto: the most deviation a level may add beyond the one above it, as a percent of the mesh's own extent.
    float errorPercent = 20.0F;
    // A mesh with fewer triangles gets no chain.
    std::uint32_t minTriangles = 1024U;
    // Manual: levels 1 .. levelCount - 1.
    std::array<AssetMeshLodLevel, kMeshLodMaxLevels - 1U> levels{};

    [[nodiscard]] bool operator==(const AssetMeshLodSettings&) const noexcept = default;
    [[nodiscard]] bool IsDefault() const noexcept { return *this == AssetMeshLodSettings{}; }
    // Whether a mesh of `triangles` is given a chain at all.
    [[nodiscard]] bool BuildsChain(std::uint64_t triangles) const noexcept { return levelCount > 1U && triangles >= minTriangles; }
    // The share of triangles and the deviation of level `level` (1 .. levelCount - 1).
    [[nodiscard]] AssetMeshLodLevel Level(std::uint32_t level) const noexcept {
        if (manual) return levels[level - 1U];
        float share = 100.0F;
        for (std::uint32_t step = 0U; step < level; ++step) share *= keepPercent / 100.0F;
        return AssetMeshLodLevel{ .trianglePercent = share, .errorPercent = errorPercent };
    }
    // Levels 1 .. 8; shares above 0 and below 100, falling level by level; deviations above 0 up to 100.
    [[nodiscard]] bool IsValid() const noexcept {
        if (levelCount < 1U || levelCount > kMeshLodMaxLevels) return false;
        if (!manual) {
            return std::isfinite(keepPercent) && keepPercent > 0.0F && keepPercent < 100.0F &&
                std::isfinite(errorPercent) && errorPercent > 0.0F && errorPercent <= 100.0F;
        }
        float previous = 100.0F;
        for (std::uint32_t level = 1U; level < levelCount; ++level) {
            const AssetMeshLodLevel& entry = levels[level - 1U];
            if (!std::isfinite(entry.trianglePercent) || !(entry.trianglePercent > 0.0F) || !(entry.trianglePercent < previous) ||
                !std::isfinite(entry.errorPercent) || !(entry.errorPercent > 0.0F) || entry.errorPercent > 100.0F) {
                return false;
            }
            previous = entry.trianglePercent;
        }
        return true;
    }
};

struct AssetMeshImportOptions {
    bool importMaterialSlots = true;
    bool importTextures = false;
    bool importMaterials = false;
    bool combineMeshes = false;
    // The editor routes this mode through the canonical skeletal publication
    // pipeline rather than the generic static-mesh container importer.
    bool importSkeletalMesh = false;
    AssetMeshImportTransform transform{};
};

struct AssetImportOptions {
    AssetMeshImportOptions mesh{};
    // Only a reimport or an import that finds its assets already there reads these two.
    AssetImportConflictPolicy conflictPolicy = AssetImportConflictPolicy::Report;
    // Plan only: fill AssetImportItemResult::changes and write nothing.
    bool dryRun = false;
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
    // Further assets the same source produced beside `id`: a skeletal model's skeleton and
    // animation clips, its textures and materials.
    std::vector<AssetId> producedAssets;
    // What a reimport changed, would change (dry run) or refused to change, one entry per asset it concerns.
    std::vector<AssetImportChange> changes;
    // The assets edited since the import that the reimport kept or refused to overwrite.
    std::vector<AssetImportChange> conflicts;
    // The backup of the files a reimport overwrote; AssetImportBackups::Restore undoes it. Empty when nothing was overwritten.
    std::string backupId;

    [[nodiscard]] bool Succeeded() const noexcept {
        return error.empty() &&
            (status == AssetImportItemStatus::Created || status == AssetImportItemStatus::Reused || status == AssetImportItemStatus::Updated) &&
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
[[nodiscard]] std::string AssetImportRecoveryAdvice(const AssetImportItemResult& item);
[[nodiscard]] std::string_view ToString(AssetImportItemStatus status) noexcept;
[[nodiscard]] std::string_view RuntimeAssetType(AssetImportCategory category) noexcept;

} // namespace kb::assets
