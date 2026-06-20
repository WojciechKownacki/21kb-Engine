#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"

#include <memory>
#include <utility>

namespace kb::editor {

kb::assets::AssetId EditorMaterialPreviewMeshLoader::PreviewMeshAssetId() noexcept {
    return kb::assets::MakeAssetId("EditorMaterialPreview:SphereMesh");
}

std::string_view EditorMaterialPreviewMeshLoader::Type() const noexcept {
    return "RenderMesh";
}

std::type_index EditorMaterialPreviewMeshLoader::PayloadType() const noexcept {
    return typeid(kb::render::RenderMeshAssetData);
}

std::vector<std::string> EditorMaterialPreviewMeshLoader::Extensions() const {
    return {};
}

kb::assets::AssetLoadResult EditorMaterialPreviewMeshLoader::Load(const kb::assets::AssetLoadRequest& request) {
    if (request.metadata.id.value != PreviewMeshAssetId().value) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = "Material preview mesh loader received a non-preview mesh asset."};
    }

    kb::render::RenderMeshAssetData mesh = EditorMaterialPreviewMeshFactory::BuildSphere();
    if (mesh.desc.vertexCount == 0U || mesh.desc.indexCount == 0U) {
        mesh = EditorMaterialPreviewMeshFactory::BuildCube();
    }
    if (mesh.desc.vertexCount == 0U || mesh.desc.indexCount == 0U) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = "Material preview mesh generation failed."};
    }

    return kb::assets::AssetLoadResult{
        .asset = std::make_shared<kb::render::RenderMeshAssetData>(std::move(mesh)),
        .error = {},
    };
}

} // namespace kb::editor
