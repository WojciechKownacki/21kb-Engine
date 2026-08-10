#include "rendering/EditorMeshPreviewService.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/IAssetLoader.hpp"
#include "engine/scene/SkeletalMeshAssetIO.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"
#include "rendering/EditorMeshPreviewRasterizer.hpp"
#include "rendering/EditorMeshThumbnailDiskCache.hpp"
#include "diagnostics/EditorLagTrace.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::size_t kMaxCachedPreviewVariants = 16U;
constexpr std::size_t kMaxConcurrentSkeletalPreviewJobs = 1U;
constexpr std::size_t kMaxConcurrentSkeletalEntryJobs = 1U;

[[nodiscard]] bool HasKnownMeshExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".21kb"
        || extension == ".obj"
        || extension == ".gltf"
        || extension == ".glb"
        || extension == ".fbx"
        || extension == kb::scene::kSkeletalMeshAssetExtension;
}

[[nodiscard]] bool IsSkeletalMeshAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == kb::scene::kSkeletalMeshAssetType ||
        metadata.physicalPath.extension() == kb::scene::kSkeletalMeshAssetExtension ||
        metadata.virtualPath.extension() == kb::scene::kSkeletalMeshAssetExtension;
}

[[nodiscard]] std::optional<kb::render::RenderMeshAssetData> LoadMesh(const kb::assets::AssetMetadata& metadata) {
    if (metadata.physicalPath.empty()) {
        return std::nullopt;
    }

    kb::render::RenderMeshAssetLoader loader;
    kb::assets::AssetLoadRequest request{
        .metadata = metadata,
        .resolvedPath = metadata.physicalPath,
    };
    kb::assets::AssetLoadResult result = loader.Load(request);
    if (!result.Succeeded() || result.asset == nullptr) {
        return std::nullopt;
    }

    std::shared_ptr<kb::render::RenderMeshAssetData> mesh = std::static_pointer_cast<kb::render::RenderMeshAssetData>(result.asset);
    if (mesh == nullptr) {
        return std::nullopt;
    }
    return *mesh;
}

[[nodiscard]] bool HasImage(const EditorMeshThumbnailImage& image) noexcept {
    return image.width > 0 && image.height > 0 && !image.bgra.empty();
}

[[nodiscard]] std::optional<EditorMeshPreviewGeometry> ExtractSkeletalMeshGeometry(
    const kb::scene::SkeletalMeshAsset& mesh) {
    if (mesh.lods.empty() || mesh.lods.front().vertices.size() < 3U) {
        return std::nullopt;
    }

    const kb::scene::SkeletalMeshLod& lod = mesh.lods.front();
    const float orientationTolerance = std::max(0.001F, mesh.conservativeBounds.extents.y * 0.01F);
    const bool correctLegacyFbxUpAxis = mesh.conservativeBounds.center.y < -orientationTolerance &&
        mesh.conservativeBounds.center.y + mesh.conservativeBounds.extents.y <= orientationTolerance;
    EditorMeshPreviewGeometry geometry{};
    geometry.positions.reserve(lod.vertices.size());
    geometry.normals.reserve(lod.vertices.size());
    for (const kb::scene::SkeletalMeshVertex& vertex : lod.vertices) {
        geometry.positions.push_back({
            correctLegacyFbxUpAxis ? -vertex.position.x : vertex.position.x,
            correctLegacyFbxUpAxis ? -vertex.position.y : vertex.position.y,
            vertex.position.z,
        });
        geometry.normals.push_back({
            correctLegacyFbxUpAxis ? -vertex.normal.x : vertex.normal.x,
            correctLegacyFbxUpAxis ? -vertex.normal.y : vertex.normal.y,
            vertex.normal.z,
        });
    }
    geometry.indices = lod.indices;
    geometry.stats.vertexCount = static_cast<std::uint32_t>(geometry.positions.size());
    geometry.stats.indexCount = static_cast<std::uint32_t>(geometry.indices.size());
    geometry.stats.triangleCount = geometry.stats.indexCount / 3U;
    geometry.stats.materialSlotCount = static_cast<std::uint32_t>(lod.sections.size());
    geometry.stats.boundsCenter[0] = correctLegacyFbxUpAxis
        ? -mesh.conservativeBounds.center.x : mesh.conservativeBounds.center.x;
    geometry.stats.boundsCenter[1] = correctLegacyFbxUpAxis
        ? -mesh.conservativeBounds.center.y : mesh.conservativeBounds.center.y;
    geometry.stats.boundsCenter[2] = mesh.conservativeBounds.center.z;
    const kb::math::Vec3 extents = mesh.conservativeBounds.extents;
    geometry.stats.boundsRadius = std::sqrt(
        extents.x * extents.x + extents.y * extents.y + extents.z * extents.z);
    return geometry;
}

[[nodiscard]] std::optional<EditorMeshPreviewGeometry> LoadSkeletalMeshGeometry(
    const kb::assets::AssetMetadata& metadata) {
    if (metadata.physicalPath.empty()) return std::nullopt;
    const std::optional<kb::scene::SkeletalMeshAsset> mesh =
        kb::scene::SkeletalMeshAssetIO::Load(metadata.physicalPath);
    return mesh.has_value() ? ExtractSkeletalMeshGeometry(*mesh) : std::nullopt;
}

[[nodiscard]] std::uint64_t PreviewMaterialCacheKey(
    const kb::assets::AssetManager* manager,
    const kb::assets::AssetMetadata& metadata) noexcept {
    // Skeletal thumbnails currently use their neutral preview material, so an AssetManager revision
    // cannot change their pixels. Treating it as part of the key made Project Files (no manager) and
    // Inspector (manager supplied) continuously evict and rebuild the same 24 MB mesh.
    return manager == nullptr || IsSkeletalMeshAsset(metadata) ? 0U : manager->Revision();
}

void ApplyResolvedPreviewMaterial(
    kb::assets::AssetManager* manager,
    const kb::render::RenderMeshAssetData& mesh,
    EditorMeshPreviewGeometry& geometry) {
    if (manager == nullptr || mesh.materialSlots.empty()) {
        return;
    }

    const std::uint64_t materialAssetId = mesh.materialSlots.front().defaultMaterialAssetId;
    if (materialAssetId == 0U) {
        return;
    }

    const kb::render::ResolvedRuntimeMaterialAsset resolved = kb::render::RuntimeMaterialResolver{}.ResolveAsset(*manager, kb::assets::AssetId{ materialAssetId });
    if (!resolved.resolved) {
        return;
    }
    std::copy(std::begin(resolved.material.desc.baseColor), std::end(resolved.material.desc.baseColor), std::begin(geometry.materialBaseColor));
    std::copy(std::begin(resolved.material.desc.emissiveColor), std::end(resolved.material.desc.emissiveColor), std::begin(geometry.materialEmissiveColor));
    geometry.materialEmissiveStrength = resolved.material.desc.emissiveStrength;
}

void AddIssue(EditorMeshValidationResult& result, EditorMeshValidationSeverity severity, std::string message) {
    result.issues.push_back(EditorMeshValidationIssue{
        .severity = severity,
        .message = std::move(message),
    });
}

[[nodiscard]] float TriangleAreaSquared(
    const EditorMeshPreviewVector3& a,
    const EditorMeshPreviewVector3& b,
    const EditorMeshPreviewVector3& c) noexcept {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float abz = b.z - a.z;
    const float acx = c.x - a.x;
    const float acy = c.y - a.y;
    const float acz = c.z - a.z;
    const float crossX = aby * acz - abz * acy;
    const float crossY = abz * acx - abx * acz;
    const float crossZ = abx * acy - aby * acx;
    return crossX * crossX + crossY * crossY + crossZ * crossZ;
}

[[nodiscard]] std::uint32_t GeometryIndexAt(const EditorMeshPreviewGeometry& geometry, std::size_t offset) noexcept {
    return geometry.indices.empty() ? static_cast<std::uint32_t>(offset) : geometry.indices[offset];
}

[[nodiscard]] std::uint32_t CountDegenerateTriangles(const EditorMeshPreviewGeometry& geometry) noexcept {
    const std::size_t indexCount = geometry.indices.empty() ? geometry.positions.size() : geometry.indices.size();
    const std::size_t triangleCount = indexCount / 3U;
    std::uint32_t degenerate = 0;
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle) {
        const std::size_t base = triangle * 3U;
        const std::uint32_t ia = GeometryIndexAt(geometry, base);
        const std::uint32_t ib = GeometryIndexAt(geometry, base + 1U);
        const std::uint32_t ic = GeometryIndexAt(geometry, base + 2U);
        if (ia >= geometry.positions.size() || ib >= geometry.positions.size() || ic >= geometry.positions.size()) {
            continue;
        }
        if (ia == ib || ib == ic || ia == ic || TriangleAreaSquared(geometry.positions[ia], geometry.positions[ib], geometry.positions[ic]) <= 0.00000001F) {
            ++degenerate;
        }
    }
    return degenerate;
}

[[nodiscard]] EditorMeshValidationResult ValidateGeometry(const EditorMeshPreviewGeometry& geometry) {
    EditorMeshValidationResult result;
    const EditorMeshThumbnailStats& stats = geometry.stats;

    if (geometry.positions.empty()) {
        AddIssue(result, EditorMeshValidationSeverity::Error, "No vertex geometry");
    } else if (geometry.positions.size() < 3U) {
        AddIssue(result, EditorMeshValidationSeverity::Error, "Not enough vertices");
    }

    if (stats.triangleCount == 0U) {
        AddIssue(result, EditorMeshValidationSeverity::Error, "No triangles");
    }
    if (geometry.indices.empty()) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "No index buffer");
    }
    if (stats.indexCount % 3U != 0U) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "Index count is not divisible by 3");
    }

    if (!geometry.indices.empty()) {
        const auto invalid = std::ranges::find_if(geometry.indices, [&geometry](std::uint32_t index) {
            return index >= geometry.positions.size();
        });
        if (invalid != geometry.indices.end()) {
            AddIssue(result, EditorMeshValidationSeverity::Error, "Index buffer references missing vertices");
        }
    }

    const std::uint32_t degenerateTriangles = CountDegenerateTriangles(geometry);
    if (degenerateTriangles > 0U) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "Degenerate triangles: " + std::to_string(degenerateTriangles));
    }

    if (geometry.normals.empty()) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "No normals");
    } else if (geometry.normals.size() != geometry.positions.size()) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "Normal count does not match vertices");
    } else {
        const auto zeroNormal = std::ranges::find_if(geometry.normals, [](const EditorMeshPreviewVector3& normal) {
            const float lengthSquared = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
            return lengthSquared <= 0.0001F;
        });
        if (zeroNormal != geometry.normals.end()) {
            AddIssue(result, EditorMeshValidationSeverity::Warning, "Some normals are zero length");
        }
    }

    const bool validBoundsCenter = std::isfinite(stats.boundsCenter[0])
        && std::isfinite(stats.boundsCenter[1])
        && std::isfinite(stats.boundsCenter[2]);
    if (!validBoundsCenter || !std::isfinite(stats.boundsRadius) || stats.boundsRadius <= 0.0F) {
        AddIssue(result, EditorMeshValidationSeverity::Error, "Invalid bounds");
    } else if (stats.boundsRadius > 10000.0F) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "Bounds are unusually large");
    } else if (stats.boundsRadius > 1000.0F) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "Mesh is very large");
    }

    if (stats.materialSlotCount == 0U) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "No material slots");
    }
    if (stats.vertexCount > 500000U || stats.triangleCount > 500000U) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "Mesh is too large for editor preview");
    } else if (stats.triangleCount > 250000U) {
        AddIssue(result, EditorMeshValidationSeverity::Warning, "High triangle count");
    }

    if (result.issues.empty()) {
        AddIssue(result, EditorMeshValidationSeverity::Info, "Mesh OK");
    }
    return result;
}

} // namespace

const EditorMeshThumbnailImage* EditorMeshPreviewService::ThumbnailFor(const kb::assets::AssetMetadata& metadata) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    Entry& entry = EnsureEntry(metadata);
    if (entry.state != EntryState::Ready) {
        return nullptr;
    }
    if (!HasImage(entry.thumbnail)) {
        if (!EnsureGeometry(metadata, entry)) {
            return nullptr;
        }
        const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
        const auto rasterStart = std::chrono::steady_clock::now();
        entry.thumbnail = EditorMeshPreviewRasterizer::Render(entry.geometry, kEditorMeshThumbnailSize, EditorMeshPreviewSettings{});
        const double rasterMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - rasterStart).count();
        diagnostics::EditorLagTrace::Slow(
            "mesh-thumbnail-raster-ui",
            eventId,
            rasterMs,
            "assetId=" + std::to_string(metadata.id.value),
            4.0);
        ++revision_;
    }
    return HasImage(entry.thumbnail) ? &entry.thumbnail : nullptr;
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::ThumbnailFor(
    kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    Entry& entry = EnsureEntry(&manager, metadata);
    if (entry.state != EntryState::Ready) {
        return nullptr;
    }
    if (!HasImage(entry.thumbnail)) {
        if (!EnsureGeometry(&manager, metadata, entry)) {
            return nullptr;
        }
        entry.thumbnail = EditorMeshPreviewRasterizer::Render(
            entry.geometry, kEditorMeshThumbnailSize, EditorMeshPreviewSettings{});
        ++revision_;
    }
    return HasImage(entry.thumbnail) ? &entry.thumbnail : nullptr;
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::PreviewFor(const kb::assets::AssetMetadata& metadata) {
    return PreviewFor(metadata, EditorMeshPreviewSettings{});
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::PreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) {
    return PreviewFor(nullptr, metadata, settings);
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::PreviewFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) {
    return PreviewFor(&manager, metadata, settings);
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::PreviewFor(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    Entry& entry = EnsureEntry(manager, metadata);
    if (entry.state != EntryState::Ready) {
        return nullptr;
    }
    if (settings == EditorMeshPreviewSettings{}) {
        if (!HasImage(entry.preview)) {
            if (!EnsureGeometry(manager, metadata, entry)) {
                return nullptr;
            }
            if (IsSkeletalMeshAsset(metadata)) {
                static_cast<void>(QueueSkeletalPreview(metadata, entry, settings));
                return HasImage(entry.thumbnail) ? &entry.thumbnail : nullptr;
            }
            const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
            const auto rasterStart = std::chrono::steady_clock::now();
            entry.preview = EditorMeshPreviewRasterizer::Render(entry.geometry, kEditorMeshPreviewSize, settings);
            const double rasterMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - rasterStart).count();
            diagnostics::EditorLagTrace::Slow(
                "mesh-preview-raster-ui",
                eventId,
                rasterMs,
                "assetId=" + std::to_string(metadata.id.value),
                4.0);
            ++revision_;
        }
        return HasImage(entry.preview) ? &entry.preview : nullptr;
    }
    for (const Entry::PreviewVariant& variant : entry.previewVariants) {
        if (variant.settings == settings && HasImage(variant.image)) {
            return &variant.image;
        }
    }
    if (!EnsureGeometry(manager, metadata, entry)) {
        return &entry.preview;
    }
    if (IsSkeletalMeshAsset(metadata)) {
        static_cast<void>(QueueSkeletalPreview(metadata, entry, settings));
        if (HasImage(entry.preview)) {
            return &entry.preview;
        }
        return HasImage(entry.thumbnail) ? &entry.thumbnail : nullptr;
    }

    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto rasterStart = std::chrono::steady_clock::now();
    Entry::PreviewVariant variant{
        .settings = settings,
        .image = EditorMeshPreviewRasterizer::Render(entry.geometry, kEditorMeshPreviewSize, settings),
    };
    const double rasterMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - rasterStart).count();
    diagnostics::EditorLagTrace::Slow(
        "mesh-preview-variant-raster-ui",
        eventId,
        rasterMs,
        "assetId=" + std::to_string(metadata.id.value),
        4.0);
    if (entry.previewVariants.size() >= kMaxCachedPreviewVariants) {
        entry.previewVariants.erase(entry.previewVariants.begin());
    }
    entry.previewVariants.push_back(std::move(variant));
    ++revision_;
    return &entry.previewVariants.back().image;
}

const EditorMeshThumbnailStats* EditorMeshPreviewService::StatsFor(const kb::assets::AssetMetadata& metadata) {
    return StatsFor(nullptr, metadata);
}

const EditorMeshThumbnailStats* EditorMeshPreviewService::StatsFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    return StatsFor(&manager, metadata);
}

const EditorMeshThumbnailStats* EditorMeshPreviewService::StatsFor(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    const Entry& entry = EnsureEntry(manager, metadata);
    return entry.state == EntryState::Ready ? &entry.stats : nullptr;
}

const EditorMeshValidationResult* EditorMeshPreviewService::ValidationFor(const kb::assets::AssetMetadata& metadata) {
    return ValidationFor(nullptr, metadata);
}

const EditorMeshValidationResult* EditorMeshPreviewService::ValidationFor(kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    return ValidationFor(&manager, metadata);
}

const EditorMeshValidationResult* EditorMeshPreviewService::ValidationFor(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    Entry& entry = EnsureEntry(manager, metadata);
    if (entry.state != EntryState::Ready) {
        return nullptr;
    }
    if (entry.validation.issues.empty() && !entry.geometryLoaded && IsSkeletalMeshAsset(metadata)) {
        QueueSkeletalEntryLoad(manager, metadata);
        return nullptr;
    }
    if (entry.validation.issues.empty() && EnsureGeometry(manager, metadata, entry)) {
        entry.validation = ValidateGeometry(entry.geometry);
    }
    return &entry.validation;
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::CachedPreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) const noexcept {
    const auto existing = entries_.find(metadata.id.value);
    if (existing == entries_.end() || existing->second.contentHash != metadata.contentHash || existing->second.state != EntryState::Ready) {
        return nullptr;
    }

    const Entry& entry = existing->second;
    if (settings == EditorMeshPreviewSettings{}) {
        return HasImage(entry.preview) ? &entry.preview : nullptr;
    }
    for (const Entry::PreviewVariant& variant : entry.previewVariants) {
        if (variant.settings == settings && HasImage(variant.image)) {
            return &variant.image;
        }
    }
    return HasImage(entry.preview) ? &entry.preview : nullptr;
}

const EditorMeshThumbnailStats* EditorMeshPreviewService::CachedStatsFor(const kb::assets::AssetMetadata& metadata) const noexcept {
    const auto existing = entries_.find(metadata.id.value);
    if (existing == entries_.end() || existing->second.contentHash != metadata.contentHash || existing->second.state != EntryState::Ready) {
        return nullptr;
    }
    return &existing->second.stats;
}

const EditorMeshValidationResult* EditorMeshPreviewService::CachedValidationFor(
    const kb::assets::AssetMetadata& metadata) const noexcept {
    const auto existing = entries_.find(metadata.id.value);
    if (existing == entries_.end() || existing->second.contentHash != metadata.contentHash ||
        existing->second.state != EntryState::Ready || !existing->second.geometryLoaded) {
        return nullptr;
    }
    return &existing->second.validation;
}

std::uint64_t EditorMeshPreviewService::Revision() const noexcept {
    return revision_;
}

void EditorMeshPreviewService::Clear() noexcept {
    entries_.clear();
    ++revision_;
}

std::size_t EditorMeshPreviewService::PumpCompletedPreviews() {
    std::size_t completed = 0U;
    for (auto pending = pendingEntries_.begin(); pending != pendingEntries_.end();) {
        if (pending->future.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            ++pending;
            continue;
        }

        std::optional<Entry> loaded;
        try {
            loaded = pending->future.get();
        } catch (...) {
            loaded = std::nullopt;
        }
        const auto existing = entries_.find(pending->metadata.id.value);
        if (existing != entries_.end() &&
            existing->second.contentHash == pending->metadata.contentHash) {
            if (loaded.has_value()) {
                if (existing->second.state == EntryState::Loading) {
                    existing->second = std::move(*loaded);
                } else {
                    existing->second.geometry = std::move(loaded->geometry);
                    existing->second.validation = std::move(loaded->validation);
                    existing->second.stats = loaded->stats;
                    existing->second.geometryLoaded = loaded->geometryLoaded;
                }
                if (HasImage(existing->second.thumbnail)) {
                    // Project Files only needs the 128 px thumbnail. Persist it immediately instead of
                    // waiting for an Inspector to request the unrelated 512 px preview; otherwise every
                    // editor restart briefly falls back to a generic Skeletal Mesh glyph.
                    EditorMeshThumbnailDiskCache::Save(
                        pending->metadata,
                        existing->second.thumbnail,
                        existing->second.preview,
                        existing->second.stats);
                }
            } else if (existing->second.state == EntryState::Loading) {
                existing->second.state = EntryState::Failed;
            }
            ++revision_;
        }
        pending = pendingEntries_.erase(pending);
        ++completed;
    }
    for (auto pending = pendingPreviews_.begin(); pending != pendingPreviews_.end();) {
        if (pending->future.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            ++pending;
            continue;
        }

        EditorMeshThumbnailImage image{};
        try {
            image = pending->future.get();
        } catch (...) {
            image = {};
        }

        const auto existing = entries_.find(pending->metadata.id.value);
        if (existing != entries_.end() &&
            existing->second.contentHash == pending->metadata.contentHash &&
            existing->second.materialContentHash == pending->materialContentHash &&
            HasImage(image)) {
            StoreCompletedPreview(existing->second, pending->settings, std::move(image));
            if (HasImage(existing->second.thumbnail) && HasImage(existing->second.preview)) {
                EditorMeshThumbnailDiskCache::Save(
                    pending->metadata,
                    existing->second.thumbnail,
                    existing->second.preview,
                    existing->second.stats);
            }
            ++revision_;
        }
        pending = pendingPreviews_.erase(pending);
        ++completed;
    }
    return completed;
}

std::size_t EditorMeshPreviewService::PumpCompletedPreviews(
    kb::assets::AssetManager& manager) {
    static_cast<void>(manager);
    return PumpCompletedPreviews();
}

bool EditorMeshPreviewService::HasPendingPreviewWork() const noexcept {
    return !pendingEntries_.empty() || !pendingPreviews_.empty();
}

bool EditorMeshPreviewService::IsMeshAsset(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "RenderMesh"
        || metadata.importCategory == "Mesh"
        || HasKnownMeshExtension(metadata.physicalPath)
        || HasKnownMeshExtension(metadata.virtualPath);
}

std::optional<EditorMeshPreviewService::Entry> EditorMeshPreviewService::BuildEntry(const kb::assets::AssetMetadata& metadata) {
    return BuildEntry(nullptr, metadata);
}

std::optional<EditorMeshPreviewService::Entry> EditorMeshPreviewService::BuildEntry(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata) {
    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto buildStart = std::chrono::steady_clock::now();
    std::string detail = "assetId=" + std::to_string(metadata.id.value) + " type=" + metadata.type;
    Entry cached;
    cached.contentHash = metadata.contentHash;
    cached.materialContentHash = PreviewMaterialCacheKey(manager, metadata);
    cached.state = EntryState::Ready;
    if (manager == nullptr || IsSkeletalMeshAsset(metadata)) {
        const auto diskStart = std::chrono::steady_clock::now();
        const bool diskLoaded = EditorMeshThumbnailDiskCache::Load(
            metadata, cached.thumbnail, cached.preview, cached.stats);
        const double diskMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - diskStart).count();
        diagnostics::EditorLagTrace::Slow(
            "mesh-preview-disk-cache", eventId, diskMs,
            detail + " hit=" + (diskLoaded ? "1" : "0"), 4.0);
        if (diskLoaded) {
            const double buildMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - buildStart).count();
            diagnostics::EditorLagTrace::Slow("mesh-preview-entry", eventId, buildMs, detail + " source=disk", 4.0);
            return cached;
        }
    }

    const auto geometryStart = std::chrono::steady_clock::now();
    std::optional<EditorMeshPreviewGeometry> loadedGeometry;
    std::optional<kb::render::RenderMeshAssetData> mesh;
    if (IsSkeletalMeshAsset(metadata)) {
        loadedGeometry = LoadSkeletalMeshGeometry(metadata);
    } else {
        mesh = LoadMesh(metadata);
        if (mesh) loadedGeometry = EditorMeshPreviewRasterizer::ExtractGeometry(*mesh);
    }
    const double geometryMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - geometryStart).count();
    diagnostics::EditorLagTrace::Slow("mesh-preview-geometry-load", eventId, geometryMs, detail, 4.0);
    if (!loadedGeometry) return std::nullopt;
    EditorMeshPreviewGeometry geometry = std::move(*loadedGeometry);
    if (geometry.positions.size() < 3U) {
        return std::nullopt;
    }

    Entry entry;
    entry.contentHash = metadata.contentHash;
    entry.materialContentHash = PreviewMaterialCacheKey(manager, metadata);
    entry.state = EntryState::Ready;
    entry.geometry = std::move(geometry);
    if (mesh) ApplyResolvedPreviewMaterial(manager, *mesh, entry.geometry);
    entry.geometryLoaded = true;
    entry.stats = entry.geometry.stats;
    entry.validation = ValidateGeometry(entry.geometry);
    const double buildMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - buildStart).count();
    diagnostics::EditorLagTrace::Slow("mesh-preview-entry", eventId, buildMs, detail + " source=asset", 4.0);
    return entry;
}

EditorMeshPreviewService::Entry& EditorMeshPreviewService::EnsureEntry(const kb::assets::AssetMetadata& metadata) {
    return EnsureEntry(nullptr, metadata);
}

EditorMeshPreviewService::Entry& EditorMeshPreviewService::EnsureEntry(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata) {
    const std::uint64_t materialContentHash = PreviewMaterialCacheKey(manager, metadata);
    const auto existing = entries_.find(metadata.id.value);
    if (existing != entries_.end() && existing->second.contentHash == metadata.contentHash) {
        // A caller without an AssetManager cannot provide a newer material revision. Preserve an
        // already material-resolved entry instead of replacing it with the neutral variant.
        if (manager == nullptr || existing->second.materialContentHash == materialContentHash) {
            if (existing->second.state == EntryState::Loading) {
                QueueSkeletalEntryLoad(manager, metadata);
            }
            return existing->second;
        }
    }

    if (IsSkeletalMeshAsset(metadata)) {
        Entry cached;
        cached.contentHash = metadata.contentHash;
        cached.materialContentHash = materialContentHash;
        cached.state = EntryState::Ready;
        if (EditorMeshThumbnailDiskCache::Load(
                metadata, cached.thumbnail, cached.preview, cached.stats)) {
            auto [inserted, _] = entries_.insert_or_assign(metadata.id.value, std::move(cached));
            static_cast<void>(_);
            ++revision_;
            return inserted->second;
        }

        Entry loading;
        loading.contentHash = metadata.contentHash;
        loading.materialContentHash = materialContentHash;
        loading.state = EntryState::Loading;
        auto [inserted, _] = entries_.insert_or_assign(metadata.id.value, std::move(loading));
        static_cast<void>(_);
        QueueSkeletalEntryLoad(manager, metadata);
        ++revision_;
        return inserted->second;
    }

    Entry entry;
    if (std::optional<Entry> built = BuildEntry(manager, metadata)) {
        entry = std::move(*built);
    } else {
        entry.contentHash = metadata.contentHash;
        entry.materialContentHash = materialContentHash;
        entry.state = EntryState::Failed;
    }

    auto [inserted, _] = entries_.insert_or_assign(metadata.id.value, std::move(entry));
    static_cast<void>(_);
    ++revision_;
    return inserted->second;
}

bool EditorMeshPreviewService::EnsureGeometry(const kb::assets::AssetMetadata& metadata, Entry& entry) {
    return EnsureGeometry(nullptr, metadata, entry);
}

bool EditorMeshPreviewService::EnsureGeometry(kb::assets::AssetManager* manager, const kb::assets::AssetMetadata& metadata, Entry& entry) {
    if (entry.geometryLoaded) {
        return !entry.geometry.positions.empty();
    }

    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto start = std::chrono::steady_clock::now();
    std::optional<kb::render::RenderMeshAssetData> mesh;
    if (IsSkeletalMeshAsset(metadata)) {
        QueueSkeletalEntryLoad(manager, metadata);
        return false;
    } else {
        mesh = LoadMesh(metadata);
        if (!mesh) return false;
        entry.geometry = EditorMeshPreviewRasterizer::ExtractGeometry(*mesh);
        ApplyResolvedPreviewMaterial(manager, *mesh, entry.geometry);
    }
    entry.geometryLoaded = true;
    entry.validation = ValidateGeometry(entry.geometry);
    if (entry.stats.vertexCount == 0U) {
        entry.stats = entry.geometry.stats;
    }
    const double durationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    diagnostics::EditorLagTrace::Slow(
        "mesh-preview-ensure-geometry",
        eventId,
        durationMs,
        "assetId=" + std::to_string(metadata.id.value),
        4.0);
    return !entry.geometry.positions.empty();
}

void EditorMeshPreviewService::QueueSkeletalEntryLoad(
    kb::assets::AssetManager* manager,
    const kb::assets::AssetMetadata& metadata) {
    const bool alreadyPending =
        std::ranges::find_if(pendingEntries_, [&](const PendingEntry& pending) {
            return pending.metadata.id == metadata.id;
        }) != pendingEntries_.end();
    if (alreadyPending || pendingEntries_.size() >= kMaxConcurrentSkeletalEntryJobs) {
        return;
    }

    if (manager != nullptr) {
        const kb::assets::AssetHandle<kb::scene::SkeletalMeshAsset> loaded =
            manager->AcquireLoaded<kb::scene::SkeletalMeshAsset>(metadata.id);
        if (loaded.IsLoaded()) {
            QueueSkeletalEntryBuild(metadata, loaded.Shared());
            return;
        }
    }

    // Project Files thumbnails are editor-only data. Load them on the preview worker instead of
    // retaining an entire runtime Skeletal Mesh through AssetManager; runtime/document I/O must not
    // leave a visible tile stuck on its fallback glyph.
    try {
        pendingEntries_.push_back(PendingEntry{
            .metadata = metadata,
            .future = std::async(std::launch::async, [metadata]() -> std::optional<Entry> {
                std::optional<EditorMeshPreviewGeometry> geometry = LoadSkeletalMeshGeometry(metadata);
                if (!geometry.has_value() || geometry->positions.size() < 3U) return std::nullopt;
                Entry entry;
                entry.contentHash = metadata.contentHash;
                entry.materialContentHash = 0U;
                entry.state = EntryState::Ready;
                entry.geometry = std::move(*geometry);
                entry.geometryLoaded = true;
                entry.stats = entry.geometry.stats;
                entry.validation = ValidateGeometry(entry.geometry);
                entry.thumbnail = EditorMeshPreviewRasterizer::Render(
                    entry.geometry, kEditorMeshThumbnailSize, EditorMeshPreviewSettings{});
                return std::optional<Entry>{std::move(entry)};
            }),
        });
    } catch (...) {
        if (const auto existing = entries_.find(metadata.id.value); existing != entries_.end()) {
            existing->second.state = EntryState::Failed;
        }
    }
}

void EditorMeshPreviewService::QueueSkeletalEntryBuild(
    const kb::assets::AssetMetadata& metadata,
    std::shared_ptr<const kb::scene::SkeletalMeshAsset> mesh) {
    if (mesh == nullptr || pendingEntries_.size() >= kMaxConcurrentSkeletalEntryJobs ||
        std::ranges::find_if(pendingEntries_, [&](const PendingEntry& pending) {
            return pending.metadata.id == metadata.id;
        }) != pendingEntries_.end()) {
        return;
    }

    try {
        pendingEntries_.push_back(PendingEntry{
            .metadata = metadata,
            .future = std::async(std::launch::async, [metadata, mesh = std::move(mesh)]() -> std::optional<Entry> {
                std::optional<EditorMeshPreviewGeometry> geometry = ExtractSkeletalMeshGeometry(*mesh);
                if (!geometry.has_value() || geometry->positions.size() < 3U) return std::nullopt;
                Entry entry;
                entry.contentHash = metadata.contentHash;
                entry.materialContentHash = 0U;
                entry.state = EntryState::Ready;
                entry.geometry = std::move(*geometry);
                entry.geometryLoaded = true;
                entry.stats = entry.geometry.stats;
                entry.validation = ValidateGeometry(entry.geometry);
                entry.thumbnail = EditorMeshPreviewRasterizer::Render(
                    entry.geometry, kEditorMeshThumbnailSize, EditorMeshPreviewSettings{});
                return std::optional<Entry>{std::move(entry)};
            }),
        });
    } catch (...) {
        if (const auto existing = entries_.find(metadata.id.value); existing != entries_.end()) {
            existing->second.state = EntryState::Failed;
        }
    }
}

bool EditorMeshPreviewService::QueueSkeletalPreview(
    const kb::assets::AssetMetadata& metadata,
    const Entry& entry,
    const EditorMeshPreviewSettings& settings) {
    const auto alreadyPending = std::ranges::find_if(pendingPreviews_, [&](const PendingPreview& pending) {
        return pending.metadata.id == metadata.id;
    });
    if (alreadyPending != pendingPreviews_.end()) {
        return true;
    }
    if (pendingPreviews_.size() >= kMaxConcurrentSkeletalPreviewJobs) {
        return false;
    }

    try {
        const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
        const auto queueStart = std::chrono::steady_clock::now();
        EditorMeshPreviewGeometry geometry = entry.geometry;
        pendingPreviews_.push_back(PendingPreview{
            .metadata = metadata,
            .materialContentHash = entry.materialContentHash,
            .settings = settings,
            .future = std::async(std::launch::async, [geometry = std::move(geometry), settings, eventId, assetId = metadata.id.value]() {
                const auto renderStart = std::chrono::steady_clock::now();
                EditorMeshThumbnailImage image = EditorMeshPreviewRasterizer::Render(
                    geometry, kEditorMeshPreviewSize, settings);
                const double renderMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - renderStart).count();
                diagnostics::EditorLagTrace::Slow(
                    "mesh-preview-raster-worker",
                    eventId,
                    renderMs,
                    "assetId=" + std::to_string(assetId),
                    4.0);
                return image;
            }),
        });
        const double queueMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - queueStart).count();
        diagnostics::EditorLagTrace::Slow(
            "mesh-preview-queue",
            eventId,
            queueMs,
            "assetId=" + std::to_string(metadata.id.value),
            4.0);
        return true;
    } catch (...) {
        return false;
    }
}

void EditorMeshPreviewService::StoreCompletedPreview(
    Entry& entry,
    const EditorMeshPreviewSettings& settings,
    EditorMeshThumbnailImage image) {
    if (settings == EditorMeshPreviewSettings{}) {
        entry.preview = std::move(image);
        return;
    }
    for (Entry::PreviewVariant& variant : entry.previewVariants) {
        if (variant.settings == settings) {
            variant.image = std::move(image);
            return;
        }
    }
    if (entry.previewVariants.size() >= kMaxCachedPreviewVariants) {
        entry.previewVariants.erase(entry.previewVariants.begin());
    }
    entry.previewVariants.push_back(Entry::PreviewVariant{
        .settings = settings,
        .image = std::move(image),
    });
}

EditorMeshPreviewService& EditorMeshPreviewCache() {
    static EditorMeshPreviewService service;
    return service;
}

} // namespace kb::editor
