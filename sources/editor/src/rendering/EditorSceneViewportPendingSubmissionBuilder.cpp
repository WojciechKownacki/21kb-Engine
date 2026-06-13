#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include <algorithm>
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
    if (!EnsureSessionTargets(session, present.renderWidth, present.renderHeight, present.settings.postProcessEnabled)) {
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
    bool postProcessEnabled) {
    const render::RenderExtent renderExtent{renderWidth, renderHeight};
    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = renderExtent,
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
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
    return render::RenderSceneSubmitDesc{
        .target = render::RenderSceneTargetBinding{
            .frameBuffer = session.sceneTarget.FrameBuffer(),
            .colorTexture = session.sceneTarget.ColorTexture(),
            .depthTexture = session.sceneTarget.DepthTexture(),
            .viewport = render::RenderViewportDesc{
                .id = render::RenderViewportId{session.viewportIndex + 1U},
                .extent = renderExtent,
                .viewportIndex = session.viewportIndex,
            },
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
        .meshPassMode = present.settings.meshPassMode,
        .selectedEntityIds = SelectedEntitySpan(session),
        .dirtySceneEntityIds = dirtySceneEntityIds,
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
