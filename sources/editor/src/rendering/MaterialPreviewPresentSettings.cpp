#include "rendering/MaterialPreviewPresentSettings.hpp"

#if defined(_WIN32)

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "rendering/MaterialPreviewRenderPolicy.hpp"
#include "rendering/MaterialPreviewViewportKeys.hpp"
#include "scene/EditorSceneContext.hpp"

#include <algorithm>
#include <array>

#include <bx/math.h>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

[[nodiscard]] float Aspect(std::uint32_t width, std::uint32_t height) noexcept {
    return height == 0U ? 1.0F : static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
}

[[nodiscard]] kb::render::SceneRenderCamera BuildMaterialPreviewCamera(
    std::uint32_t renderWidth,
    std::uint32_t renderHeight,
    const EditorMaterialPreviewSceneSettings& settings) noexcept {
    kb::render::SceneRenderCamera camera{};
    const std::array<float, 3U> eye = settings.CameraEye();
    bx::mtxLookAt(camera.view.data(), bx::Vec3{eye[0], eye[1], eye[2]}, bx::Vec3{0.0F, 0.0F, 0.0F}, bx::Vec3{0.0F, 1.0F, 0.0F});
    kb::render::SceneDepthPolicy::MakePerspective(
        camera.projection.data(),
        settings.verticalFovDegrees,
        Aspect(renderWidth, renderHeight),
        0.05F,
        50.0F,
        kb::render::SceneDepthPolicy::HomogeneousDepth());
    return camera;
}

} // namespace

const kb::assets::AssetMetadata* MaterialPreviewMetadata(
    const EditorSceneContext& sceneContext, kb::assets::AssetId assetId) noexcept {
    if (!assetId.IsValid()) {
        return nullptr;
    }
    const kb::assets::AssetMetadata* metadata = sceneContext.Scene().Assets().Manager().Registry().Find(assetId);
    return metadata != nullptr && (metadata->type == "RenderMaterial" || metadata->type == "RenderMaterialInstance") ? metadata : nullptr;
}

EditorSceneBgfxViewport::PresentSettings BuildMaterialPreviewPresentSettings(
    EditorSceneContext& sceneContext, const RECT& previewRect, std::uint64_t viewportKey) {
    const std::uint32_t renderWidth = std::max<std::uint32_t>(1U, RectWidth(previewRect));
    const std::uint32_t renderHeight = std::max<std::uint32_t>(1U, RectHeight(previewRect));
    const EditorMaterialPreviewSceneSettings& previewSettings = sceneContext.MaterialPreviewSceneSettings();
    const EditorMaterialPreviewSurface previewSurface = viewportKey == kInspectorMaterialPreviewViewportKey
        ? EditorMaterialPreviewSurface::Inspector
        : EditorMaterialPreviewSurface::MaterialEditor;
    return EditorSceneBgfxViewport::PresentSettings{
        .renderWidth = renderWidth,
        .renderHeight = renderHeight,
        .fitMode = EditorViewportFitMode::Fit,
        .cameraOverride = BuildMaterialPreviewCamera(renderWidth, renderHeight, previewSettings),
        .viewportKey = viewportKey,
        .editorSceneOverlaysEnabled = false,
        .meshPassMode = kb::render::SceneRenderMeshPassMode::OpaqueAndTransparent,
        .lightingConfig = MaterialPreviewRenderPolicy::NeutralPbrLightingConfig(
            previewSettings, sceneContext.ProjectConfiguration().lightingPath),
        .materialGraphContext = kb::render::RenderMaterialGraphBuildContext{
            .qualityLevel = previewSettings.qualityLevel,
            .variantUsage = previewSurface == EditorMaterialPreviewSurface::MaterialEditor &&
                    sceneContext.MaterialPreviewNodePreviewEnabled()
                ? kb::render::RenderMaterialGraphVariantUsage::NodePreview
                : kb::render::RenderMaterialGraphVariantUsage::Preview,
        },
        .postProcessSettings = MaterialPreviewRenderPolicy::StableExposurePostProcessSettings(previewSettings),
        .shadowPassEnabled = false,
        .postProcessEnabled = previewSettings.postProcessEnabled && !previewSettings.normalDebugView,
        .selectionMaskEnabled = false,
        .selectionOutlineEnabled = false,
        .gpuDrivenRuntimeDispatchEnabled = false,
        .drawSafeArea = false,
        .sceneRevision = sceneContext.MaterialPreviewRevision(previewSurface),
        .sceneDirtyBaseRevision = sceneContext.MaterialPreviewRevision(previewSurface),
        .sceneFullSyncRequired = true,
    };
}

} // namespace kb::editor

#endif
