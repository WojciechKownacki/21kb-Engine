#include "scene/material_preview/EditorMaterialPreviewMeshLoader.hpp"

#include "kb/render/resources/RenderMeshAssetBuilder.hpp"
#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"

#include <memory>
#include <utility>

namespace kb::editor {

kb::assets::AssetId EditorMaterialPreviewMeshLoader::PreviewMeshAssetId() noexcept {
    return EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Sphere);
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
    const kb::assets::AssetId sphereId = EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Sphere);
    const kb::assets::AssetId cubeId = EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Cube);
    const kb::assets::AssetId planeId = EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Plane);
    const kb::assets::AssetId fallbackId = EditorMaterialPreviewPrimitivePolicy::GeneratedMeshAssetId(EditorMaterialPreviewPrimitiveKind::Fallback);
    if (request.metadata.id.value != sphereId.value &&
        request.metadata.id.value != cubeId.value &&
        request.metadata.id.value != planeId.value &&
        request.metadata.id.value != fallbackId.value) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = "Material preview mesh loader received a non-preview mesh asset."};
    }

    kb::render::RenderMeshAssetData mesh = request.metadata.id.value == cubeId.value || request.metadata.id.value == fallbackId.value
        ? EditorMaterialPreviewMeshFactory::BuildCube()
        : request.metadata.id.value == planeId.value
            ? EditorMaterialPreviewMeshFactory::BuildPlane()
            : EditorMaterialPreviewMeshFactory::BuildSphere();
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
