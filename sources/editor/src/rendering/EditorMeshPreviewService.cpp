#include "rendering/EditorMeshPreviewService.hpp"

#include "engine/assets/IAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "rendering/EditorMeshPreviewRasterizer.hpp"
#include "rendering/EditorMeshThumbnailDiskCache.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace kb::editor {
namespace {

constexpr std::size_t kMaxCachedPreviewVariants = 16U;

[[nodiscard]] bool HasKnownMeshExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".21kb"
        || extension == ".obj"
        || extension == ".gltf"
        || extension == ".glb"
        || extension == ".fbx";
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
        entry.thumbnail = EditorMeshPreviewRasterizer::Render(entry.geometry, kEditorMeshThumbnailSize, EditorMeshPreviewSettings{});
        ++revision_;
    }
    return HasImage(entry.thumbnail) ? &entry.thumbnail : nullptr;
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::PreviewFor(const kb::assets::AssetMetadata& metadata) {
    return PreviewFor(metadata, EditorMeshPreviewSettings{});
}

const EditorMeshThumbnailImage* EditorMeshPreviewService::PreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    Entry& entry = EnsureEntry(metadata);
    if (entry.state != EntryState::Ready) {
        return nullptr;
    }
    if (settings == EditorMeshPreviewSettings{}) {
        if (!HasImage(entry.preview)) {
            if (!EnsureGeometry(metadata, entry)) {
                return nullptr;
            }
            entry.preview = EditorMeshPreviewRasterizer::Render(entry.geometry, kEditorMeshPreviewSize, settings);
            ++revision_;
        }
        return HasImage(entry.preview) ? &entry.preview : nullptr;
    }
    for (const Entry::PreviewVariant& variant : entry.previewVariants) {
        if (variant.settings == settings && HasImage(variant.image)) {
            return &variant.image;
        }
    }
    if (!EnsureGeometry(metadata, entry)) {
        return &entry.preview;
    }

    Entry::PreviewVariant variant{
        .settings = settings,
        .image = EditorMeshPreviewRasterizer::Render(entry.geometry, kEditorMeshPreviewSize, settings),
    };
    if (entry.previewVariants.size() >= kMaxCachedPreviewVariants) {
        entry.previewVariants.erase(entry.previewVariants.begin());
    }
    entry.previewVariants.push_back(std::move(variant));
    ++revision_;
    return &entry.previewVariants.back().image;
}

const EditorMeshThumbnailStats* EditorMeshPreviewService::StatsFor(const kb::assets::AssetMetadata& metadata) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    const Entry& entry = EnsureEntry(metadata);
    return entry.state == EntryState::Ready ? &entry.stats : nullptr;
}

const EditorMeshValidationResult* EditorMeshPreviewService::ValidationFor(const kb::assets::AssetMetadata& metadata) {
    if (!IsMeshAsset(metadata)) {
        return nullptr;
    }

    Entry& entry = EnsureEntry(metadata);
    if (entry.state != EntryState::Ready) {
        return nullptr;
    }
    if (entry.validation.issues.empty() && EnsureGeometry(metadata, entry)) {
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

std::uint64_t EditorMeshPreviewService::Revision() const noexcept {
    return revision_;
}

void EditorMeshPreviewService::Clear() noexcept {
    entries_.clear();
    ++revision_;
}

bool EditorMeshPreviewService::IsMeshAsset(const kb::assets::AssetMetadata& metadata) {
    return metadata.type == "RenderMesh"
        || metadata.importCategory == "Mesh"
        || HasKnownMeshExtension(metadata.physicalPath)
        || HasKnownMeshExtension(metadata.virtualPath);
}

std::optional<EditorMeshPreviewService::Entry> EditorMeshPreviewService::BuildEntry(const kb::assets::AssetMetadata& metadata) {
    Entry cached;
    cached.contentHash = metadata.contentHash;
    cached.state = EntryState::Ready;
    if (EditorMeshThumbnailDiskCache::Load(metadata, cached.thumbnail, cached.preview, cached.stats)) {
        return cached;
    }

    std::optional<kb::render::RenderMeshAssetData> mesh = LoadMesh(metadata);
    if (!mesh.has_value()) {
        return std::nullopt;
    }

    EditorMeshPreviewGeometry geometry = EditorMeshPreviewRasterizer::ExtractGeometry(*mesh);
    if (geometry.positions.size() < 3U) {
        return std::nullopt;
    }

    Entry entry;
    entry.contentHash = metadata.contentHash;
    entry.state = EntryState::Ready;
    entry.geometry = std::move(geometry);
    entry.geometryLoaded = true;
    entry.stats = entry.geometry.stats;
    entry.validation = ValidateGeometry(entry.geometry);
    return entry;
}

EditorMeshPreviewService::Entry& EditorMeshPreviewService::EnsureEntry(const kb::assets::AssetMetadata& metadata) {
    const auto existing = entries_.find(metadata.id.value);
    if (existing != entries_.end() && existing->second.contentHash == metadata.contentHash) {
        return existing->second;
    }

    Entry entry;
    if (std::optional<Entry> built = BuildEntry(metadata)) {
        entry = std::move(*built);
    } else {
        entry.contentHash = metadata.contentHash;
        entry.state = EntryState::Failed;
    }

    auto [inserted, _] = entries_.insert_or_assign(metadata.id.value, std::move(entry));
    static_cast<void>(_);
    ++revision_;
    return inserted->second;
}

bool EditorMeshPreviewService::EnsureGeometry(const kb::assets::AssetMetadata& metadata, Entry& entry) {
    if (entry.geometryLoaded) {
        return !entry.geometry.positions.empty();
    }

    std::optional<kb::render::RenderMeshAssetData> mesh = LoadMesh(metadata);
    if (!mesh.has_value()) {
        return false;
    }
    entry.geometry = EditorMeshPreviewRasterizer::ExtractGeometry(*mesh);
    entry.geometryLoaded = true;
    entry.validation = ValidateGeometry(entry.geometry);
    if (entry.stats.vertexCount == 0U) {
        entry.stats = entry.geometry.stats;
    }
    return !entry.geometry.positions.empty();
}

EditorMeshPreviewService& EditorMeshPreviewCache() {
    static EditorMeshPreviewService service;
    return service;
}

} // namespace kb::editor
