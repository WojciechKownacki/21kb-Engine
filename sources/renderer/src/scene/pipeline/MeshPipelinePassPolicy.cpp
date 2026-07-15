#include "scene/pipeline/MeshPipelinePassPolicy.hpp"

#include "kb/render/SceneDepthPolicy.hpp"

#include <algorithm>
#include <cstdint>

namespace kb::render {
namespace {

[[nodiscard]] bool IsSelectedEntity(std::span<const std::uint64_t> selectedEntityIds, std::uint64_t entityId) noexcept {
    return std::ranges::find(selectedEntityIds, entityId) != selectedEntityIds.end();
}

// LIB-136: applied unconditionally (not gated per-pass like selection) - the drawing
// camera's cullingMask defaults to all-bits-set (SceneRenderCamera's own default member
// initializer), so this is a no-op for every instance with the default layer=1 and for
// every camera authored before LIB-136, including the shadow-casting light's own
// SceneRenderCamera (DirectionalShadowSetup::camera{} - default-constructed, never given a
// restricted mask). A camera's cullingMask therefore only ever restricts what THAT camera
// itself draws (opaque/transparent/GBuffer/selection), never what casts shadows for it.
[[nodiscard]] bool PassesCullingMask(const SceneRenderMeshInstance& instance, std::uint32_t cullingMask) noexcept {
    return (instance.layer & cullingMask) != 0U;
}

} // namespace

bool MeshPipelinePassPolicy::CanEverContain(
    MeshPassType pass,
    const SceneRenderMeshInstance& instance,
    std::span<const std::uint64_t> selectedEntityIds,
    std::uint32_t cullingMask) noexcept {
    if (!PassesCullingMask(instance, cullingMask)) {
        return false;
    }
    switch (pass) {
    case MeshPassType::ShadowDepth:
        return instance.castsShadow;
    case MeshPassType::SelectionId:
    case MeshPassType::EditorSelection:
        return IsSelectedEntity(selectedEntityIds, instance.entityId);
    case MeshPassType::Depth:
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
    case MeshPassType::BaseTransparent:
    case MeshPassType::Gizmo:
        return true;
    }

    return true;
}

std::uint32_t MeshPipelinePassPolicy::CountCandidateInstances(
    MeshPassType pass,
    const SceneMeshBatch& batch,
    std::span<const std::uint64_t> selectedEntityIds,
    std::uint32_t cullingMask) noexcept {
    std::uint32_t count = 0U;
    for (const SceneRenderMeshInstance& instance : batch.instances) {
        count += CanEverContain(pass, instance, selectedEntityIds, cullingMask) ? 1U : 0U;
    }
    return count;
}

bool MeshPipelinePassPolicy::Accepts(
    MeshPassType pass,
    const SceneRenderMeshInstance& instance,
    const RenderMaterialResource* material,
    std::span<const std::uint64_t> selectedEntityIds,
    std::uint32_t cullingMask) noexcept {
    if (!PassesCullingMask(instance, cullingMask)) {
        return false;
    }
    switch (pass) {
    case MeshPassType::Depth:
        return !UsesDisabledAlphaBlend(material);
    case MeshPassType::BaseOpaque:
    case MeshPassType::GBuffer:
        return !UsesDisabledAlphaBlend(material);
    case MeshPassType::BaseTransparent:
        // Translucent (alphaMode == Blend) materials render exclusively in the transparent pass (MAT-80).
        return UsesDisabledAlphaBlend(material);
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

namespace {

// Maps a material's translucent blend mode to bgfx blend bits (MAT-79). Used only for the transparent
// pass; opaque/masked materials never reach it.
[[nodiscard]] std::uint64_t TranslucencyBlendState(const RenderMaterialResource* material) noexcept {
    const RenderMaterialTranslucencyBlend mode =
        material != nullptr ? material->translucencyBlend : RenderMaterialTranslucencyBlend::Alpha;
    switch (mode) {
    case RenderMaterialTranslucencyBlend::Alpha:
        return BGFX_STATE_BLEND_ALPHA;
    case RenderMaterialTranslucencyBlend::Additive:
        return BGFX_STATE_BLEND_ADD;
    case RenderMaterialTranslucencyBlend::Modulate:
        return BGFX_STATE_BLEND_MULTIPLY;
    case RenderMaterialTranslucencyBlend::PreMultipliedAlpha:
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    case RenderMaterialTranslucencyBlend::AlphaHoldout:
        // The source removes destination coverage by its alpha (dst *= 1 - srcAlpha) without adding color.
        return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    }
    return BGFX_STATE_BLEND_ALPHA;
}

} // namespace

std::uint64_t MeshPipelinePassPolicy::State(
    MeshPassType pass,
    const RenderMeshResource* mesh,
    const RenderMaterialResource* material) noexcept {
    const bool doubleSided = (mesh != nullptr && mesh->doubleSided) || (material != nullptr && material->doubleSided);
    const std::uint64_t rasterStateExtra = mesh == nullptr ? 0U : mesh->rasterStateExtra;
    const std::uint64_t cullState = doubleSided ? 0U : BGFX_STATE_CULL_CCW;
    // Opaque writes depth by default; a material may opt out (e.g. decals/overlays). Translucent always
    // renders depth read-only (convention). Two-sided already flows from the material above (MAT-79).
    const bool writesDepth = material == nullptr || material->writesDepth;

    switch (pass) {
    case MeshPassType::Depth:
    case MeshPassType::ShadowDepth:
        return BGFX_STATE_WRITE_Z | SceneDepthPolicy::DepthTestState() | cullState | rasterStateExtra;
    case MeshPassType::GBuffer:
        return SceneDepthPolicy::SceneWriteState() | cullState | rasterStateExtra;
    case MeshPassType::BaseTransparent:
        return SceneDepthPolicy::SceneDepthReadState() | TranslucencyBlendState(material) | cullState | rasterStateExtra;
    case MeshPassType::SelectionId:
        return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | cullState | rasterStateExtra;
    case MeshPassType::EditorSelection:
    case MeshPassType::Gizmo:
        return SceneDepthPolicy::SceneOverlayState(true) | cullState | rasterStateExtra;
    case MeshPassType::BaseOpaque: {
        const std::uint64_t opaqueState = SceneDepthPolicy::SceneMeshState(doubleSided) | rasterStateExtra;
        return writesDepth ? opaqueState : (opaqueState & ~BGFX_STATE_WRITE_Z);
    }
    }

    return SceneDepthPolicy::SceneMeshState(doubleSided) | rasterStateExtra;
}

} // namespace kb::render
