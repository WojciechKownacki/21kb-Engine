#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include <algorithm>
#include <cstdint>
#include <span>

namespace kb::editor {
namespace {

constexpr std::uint32_t kSceneSubmitClearRgba = 0x000000FFU;

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
}

} // namespace

bool EditorSceneBgfxViewport::PendingSubmissionBuilder::Build(
    const PendingPresent& present,
    const HostSurface& surface,
    bool clearTarget,
    render::Renderer::SceneFrameSubmission& submission) {
    if (present.session == nullptr || present.scene == nullptr || present.renderWidth == 0U || present.renderHeight == 0U ||
        present.outputWidth == 0U || present.outputHeight == 0U || !surface.presentTarget.IsValid()) {
        return false;
    }

    ViewportSession& session = *present.session;
    const std::uint8_t sceneMsaaSamples =
        (!present.settings.postProcessEnabled && present.settings.lightingConfig.lightingPath != render::SceneRenderLightingPath::Deferred)
            ? present.settings.msaaSamples
            : 0U;
    if (!EnsureSessionTargets(session, present.renderWidth, present.renderHeight, present.settings.postProcessEnabled, sceneMsaaSamples)) {
        return false;
    }

    session.selectedEntityIds = present.settings.selectedEntityIds;
    submission = render::Renderer::SceneFrameSubmission{
        .scene = present.scene,
        .desc = BuildSubmitDesc(present, surface, session, clearTarget),
    };
    return true;
}

bool EditorSceneBgfxViewport::PendingSubmissionBuilder::EnsureSessionTargets(
    ViewportSession& session,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight,
    bool postProcessEnabled,
    std::uint8_t msaaSamples) {
    const render::RenderExtent renderExtent{renderWidth, renderHeight};
    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = renderExtent,
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
            .msaaSamples = msaaSamples,
        })) {
        return false;
    }
    if (!postProcessEnabled) {
        session.postProcessTargets.Shutdown();
        return true;
    }
    return session.postProcessTargets.Ensure(render::ScenePostProcessTargetsDesc{
        .extent = renderExtent,
        .colorPolicy = render::SceneColorFormatPolicy::Auto,
    });
}

render::RenderSceneSubmitDesc EditorSceneBgfxViewport::PendingSubmissionBuilder::BuildSubmitDesc(
    const PendingPresent& present,
    const HostSurface& surface,
    const ViewportSession& session,
    bool clearTarget) {
    const render::RenderExtent renderExtent{present.renderWidth, present.renderHeight};
    const render::RenderExtent surfaceExtent{RectWidth(surface.rect), RectHeight(surface.rect)};
    const bool sceneChanged = session.submittedSceneRevision != present.settings.sceneRevision;
    const bool canUseIncrementalSceneSync = sceneChanged &&
        !present.settings.sceneFullSyncRequired &&
        !present.settings.dirtySceneEntityIds.empty() &&
        session.submittedSceneRevision >= present.settings.sceneDirtyBaseRevision;
    const bool fullSceneSyncRequired = session.submittedSceneRevision == 0U || (sceneChanged && !canUseIncrementalSceneSync);
    const std::span<const std::uint64_t> dirtySceneEntityIds = canUseIncrementalSceneSync
        ? std::span<const std::uint64_t>{ present.settings.dirtySceneEntityIds.data(), present.settings.dirtySceneEntityIds.size() }
        : std::span<const std::uint64_t>{};
    bgfx::TextureHandle depthTextureForSampling = BGFX_INVALID_HANDLE;
    if (session.sceneTarget.DepthTextureSampled()) {
        depthTextureForSampling = session.sceneTarget.DepthTexture();
    }
    render::RenderSceneSubmitDesc desc{
        .target = render::RenderSceneTargetBinding{
            .frameBuffer = session.sceneTarget.FrameBuffer(),
            .colorTexture = session.sceneTarget.ColorTexture(),
            .resolvedColorTexture = session.sceneTarget.ResolvedColorTexture(),
            .depthTexture = depthTextureForSampling,
            .viewport = render::RenderViewportDesc{
                .id = render::RenderViewportId{session.viewportIndex + 1U},
                .extent = renderExtent,
                .viewportIndex = session.viewportIndex,
            },
            .msaaSamples = session.sceneTarget.MsaaSamples(),
        },
        .postProcess = present.settings.postProcessEnabled ? session.postProcessTargets.Binding() : render::RenderPostProcessTargetBinding{},
        .finalComposite = render::RenderFinalCompositeTargetBinding{
            .frameBuffer = surface.presentTarget.FrameBuffer(),
            .extent = surfaceExtent,
            .outputRect = OutputRectFor(present, surface),
            .enabled = true,
            .clearTarget = clearTarget,
        },
        .cameraOverride = present.settings.cameraOverride,
        .postProcessSettings = present.settings.postProcessSettings,
        .lightingConfig = present.settings.lightingConfig,
        .materialGraphContext = present.settings.materialGraphContext,
        .meshPassMode = present.settings.meshPassMode,
        .selectedEntityIds = SelectedEntitySpan(session),
        .dirtySceneEntityIds = dirtySceneEntityIds,
        .editorLightWireframes = std::span<const render::EditorLightWireframeDesc>{present.settings.editorLightWireframes.data(), present.settings.editorLightWireframes.size()},
        .clearRgba = kSceneSubmitClearRgba,
        .editorSceneOverlaysEnabled = present.settings.editorSceneOverlaysEnabled,
        .shadowPassEnabled = present.settings.shadowPassEnabled,
        .postProcessEnabled = present.settings.postProcessEnabled,
        .selectionMaskEnabled = present.settings.selectionMaskEnabled,
        .selectionOutlineEnabled = present.settings.selectionOutlineEnabled,
        .gpuDrivenRuntimeDispatchEnabled = present.settings.gpuDrivenRuntimeDispatchEnabled,
        .synchronizeScene = fullSceneSyncRequired,
        .editorGrid = present.settings.editorGrid,
        .editorGizmo = present.settings.editorGizmo,
        .editorSelectionBox = present.settings.editorSelectionBox,
    };
    return desc;
}

render::RenderViewportRect EditorSceneBgfxViewport::PendingSubmissionBuilder::OutputRectFor(const PendingPresent& present, const HostSurface& surface) noexcept {
    return render::RenderViewportRect{
        .x = static_cast<std::uint32_t>(std::max<LONG>(0, present.destination.left - surface.rect.left)),
        .y = static_cast<std::uint32_t>(std::max<LONG>(0, present.destination.top - surface.rect.top)),
        .extent = render::RenderExtent{present.outputWidth, present.outputHeight},
    };
}

std::span<const std::uint64_t> EditorSceneBgfxViewport::PendingSubmissionBuilder::SelectedEntitySpan(const ViewportSession& session) noexcept {
    return std::span<const std::uint64_t>{session.selectedEntityIds.data(), session.selectedEntityIds.size()};
}

} // namespace kb::editor

#endif
