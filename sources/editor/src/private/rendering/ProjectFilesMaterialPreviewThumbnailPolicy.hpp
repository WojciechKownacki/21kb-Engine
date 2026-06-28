#pragma once

#include "engine/assets/AssetMetadata.hpp"
#include "rendering/ProjectFilesAssetIconResolver.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"

#include <cstdint>
#include <string_view>

namespace kb::editor {

struct ProjectFilesMaterialPreviewThumbnailPolicy {
    bool usesPreviewScenePrimitive = false;
    std::string_view primitiveName;
    std::uint32_t vertexCount = 0U;
    std::uint32_t triangleCount = 0U;
    float boundsRadius = 1.0F;

    [[nodiscard]] static ProjectFilesMaterialPreviewThumbnailPolicy Resolve(const kb::assets::AssetMetadata& metadata) {
        if (!ProjectFilesAssetIconResolver::IsMaterial(metadata)) {
            return {};
        }
        const kb::render::RenderMeshAssetData sphere = EditorMaterialPreviewMeshFactory::BuildSphere();
        return ProjectFilesMaterialPreviewThumbnailPolicy{
            .usesPreviewScenePrimitive = true,
            .primitiveName = "sphere",
            .vertexCount = static_cast<std::uint32_t>(sphere.desc.vertexCount),
            .triangleCount = static_cast<std::uint32_t>(sphere.indices32.size() / 3U),
            .boundsRadius = sphere.bounds.radius,
        };
    }
};

} // namespace kb::editor
