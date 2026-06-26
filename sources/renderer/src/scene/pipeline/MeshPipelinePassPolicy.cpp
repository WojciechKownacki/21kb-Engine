#include "scene/pipeline/MeshPipelinePassPolicy.hpp"

#include "kb/render/SceneDepthPolicy.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::render {
namespace {

[[nodiscard]] bool IsSelectedEntity(std::span<const std::uint64_t> selectedEntityIds, std::uint64_t entityId) noexcept {
    return std::ranges::find(selectedEntityIds, entityId) != selectedEntityIds.end();
}

} // namespace

bool MeshPipelinePassPolicy::CanEverContain(
    MeshPassType pass,
    const SceneRenderMeshInstance& instance,
    std::span<const std::uint64_t> selectedEntityIds) noexcept {
    switch (pass) {
    case MeshPassType::ShadowDepth:
        return instance.castsShadow;
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return IsSelectedEntity(selectedEntityIds, instance.entityId);
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::BaseTransparent:
    case MeshPassType::Gizmo:
        return true;
    }

    return true;
}

std::uint32_t MeshPipelinePassPolicy::CountCandidateInstances(
    MeshPassType pass,
    const SceneMeshBatch& batch,
    std::span<const std::uint64_t> selectedEntityIds) noexcept {
    std::uint32_t count = 0U;
    for (const SceneRenderMeshInstance& instance : batch.instances) {
        count += CanEverContain(pass, instance, selectedEntityIds) ? 1U : 0U;
    }
    return count;
}

bool MeshPipelinePassPolicy::Accepts(
    MeshPassType pass,
    const SceneRenderMeshInstance& instance,
    const RenderMaterialResource* material,
    std::span<const std::uint64_t> selectedEntityIds) noexcept {
    switch (pass) {
    case MeshPassType::Depth:
        return !UsesDisabledAlphaBlend(material);
    case MeshPassType::BaseOpaque:
        return !UsesDisabledAlphaBlend(material);
    case MeshPassType::BaseTransparent:
        return false;
    case MeshPassType::ShadowDepth:
        return instance.castsShadow && !UsesDisabledAlphaBlend(material);
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return IsSelectedEntity(selectedEntityIds, instance.entityId);
    case MeshPassType::Gizmo:
        return true;
    }

    return false;
}

bool MeshPipelinePassPolicy::UsesDisabledAlphaBlend(const RenderMaterialResource* material) noexcept {
    return material != nullptr && material->alphaMode == RenderMaterialAlphaMode::Blend;
}

std::uint64_t MeshPipelinePassPolicy::State(
    MeshPassType pass,
    const RenderMeshResource* mesh,
    const RenderMaterialResource* material) noexcept {
    const bool doubleSided = (mesh != nullptr && mesh->doubleSided) || (material != nullptr && material->doubleSided);
    const std::uint64_t rasterStateExtra = mesh == nullptr ? 0U : mesh->rasterStateExtra;
    const std::uint64_t cullState = doubleSided ? 0U : BGFX_STATE_CULL_CCW;

    switch (pass) {
    case MeshPassType::Depth:
    case MeshPassType::ShadowDepth:
        return BGFX_STATE_WRITE_Z | SceneDepthPolicy::DepthTestState() | cullState | rasterStateExtra;
    case MeshPassType::BaseTransparent:
        return SceneDepthPolicy::SceneDepthReadState() | BGFX_STATE_BLEND_ALPHA | cullState | rasterStateExtra;
    case MeshPassType::SelectionId:
        return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | cullState | rasterStateExtra;
    case MeshPassType::EditorSelection:
    case MeshPassType::Gizmo:
        return SceneDepthPolicy::SceneOverlayState(true) | cullState | rasterStateExtra;
    case MeshPassType::BaseOpaque:
        return SceneDepthPolicy::SceneMeshState(doubleSided) | rasterStateExtra;
    }

    return SceneDepthPolicy::SceneMeshState(doubleSided) | rasterStateExtra;
}

} // namespace kb::render
