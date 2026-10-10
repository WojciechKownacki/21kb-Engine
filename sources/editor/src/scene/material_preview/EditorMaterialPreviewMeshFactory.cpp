#include "scene/material_preview/EditorMaterialPreviewMeshFactory.hpp"

#include "kb/render/resources/ShapeMeshes.hpp"

namespace kb::editor {

// Preview meshes come from the engine's one shape generator, at the preview's own sizes.

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildSphere() {
    return kb::render::ShapeMeshes::Sphere(1.0F, "Preview");
}

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildCylinder() {
    return kb::render::ShapeMeshes::Cylinder(1.0F, 2.0F, "Preview");
}

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildCube() {
    return kb::render::ShapeMeshes::Box(1.0F, "Preview");
}

kb::render::RenderMeshAssetData EditorMaterialPreviewMeshFactory::BuildPlane() {
    return kb::render::ShapeMeshes::Quad(2.0F, false, "Preview");
}

} // namespace kb::editor
