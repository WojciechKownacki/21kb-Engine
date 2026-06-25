#include "rendering/EditorMeshThumbnailService.hpp"

#include "rendering/EditorMeshPreviewService.hpp"

namespace kb::editor {

const EditorMeshThumbnailImage* EditorMeshThumbnailService::ThumbnailFor(const kb::assets::AssetMetadata& metadata) {
    return EditorMeshPreviewCache().ThumbnailFor(metadata);
}

const EditorMeshThumbnailImage* EditorMeshThumbnailService::PreviewFor(const kb::assets::AssetMetadata& metadata) {
    return EditorMeshPreviewCache().PreviewFor(metadata);
}

const EditorMeshThumbnailImage* EditorMeshThumbnailService::PreviewFor(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    return EditorMeshPreviewCache().PreviewFor(manager, metadata, EditorMeshPreviewSettings{});
}

const EditorMeshThumbnailImage* EditorMeshThumbnailService::PreviewFor(const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) {
    return EditorMeshPreviewCache().PreviewFor(metadata, settings);
}

const EditorMeshThumbnailImage* EditorMeshThumbnailService::PreviewFor(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata, const EditorMeshPreviewSettings& settings) {
    return EditorMeshPreviewCache().PreviewFor(manager, metadata, settings);
}

const EditorMeshThumbnailStats* EditorMeshThumbnailService::StatsFor(const kb::assets::AssetMetadata& metadata) {
    return EditorMeshPreviewCache().StatsFor(metadata);
}

const EditorMeshThumbnailStats* EditorMeshThumbnailService::StatsFor(const kb::assets::AssetManager& manager, const kb::assets::AssetMetadata& metadata) {
    return EditorMeshPreviewCache().StatsFor(manager, metadata);
}

std::uint64_t EditorMeshThumbnailService::Revision() const noexcept {
    return EditorMeshPreviewCache().Revision();
}

void EditorMeshThumbnailService::Clear() noexcept {
    EditorMeshPreviewCache().Clear();
}

EditorMeshThumbnailService& EditorMeshThumbnailCache() {
    static EditorMeshThumbnailService service;
    return service;
}

} // namespace kb::editor
