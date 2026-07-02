#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "app/EditorCrashBreadcrumbs.hpp"
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
    EditorCrashBreadcrumbs::Write(
        "viewport_submission",
        "Build begin viewportIndex=" + std::to_string(session.viewportIndex) +
            " key=" + std::to_string(present.settings.viewportKey) +
            " post=" + (present.settings.postProcessEnabled ? std::string{"1"} : std::string{"0"}));
    if (!EnsureSessionTargets(session, present.renderWidth, present.renderHeight, present.settings.postProcessEnabled)) {
        EditorCrashBreadcrumbs::Write("viewport_submission", "EnsureSessionTargets failed");
        return false;
    }

    session.selectedEntityIds = present.settings.selectedEntityIds;
    submission = render::Renderer::SceneFrameSubmission{
        .scene = present.scene,
        .desc = BuildSubmitDesc(present, surface, session, clearTarget),
    };
    EditorCrashBreadcrumbs::Write("viewport_submission", "Build end");
    return true;
}

bool EditorSceneBgfxViewport::PendingSubmissionBuilder::EnsureSessionTargets(
    ViewportSession& session,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight,
    bool postProcessEnabled) {
    EditorCrashBreadcrumbs::Write(
        "viewport_targets",
        "EnsureSessionTargets begin " + std::to_string(renderWidth) + "x" + std::to_string(renderHeight) +
            " post=" + (postProcessEnabled ? std::string{"1"} : std::string{"0"}));
    const render::RenderExtent renderExtent{renderWidth, renderHeight};
    EditorCrashBreadcrumbs::Write("viewport_targets", "sceneTarget.Ensure begin");
    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = renderExtent,
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        EditorCrashBreadcrumbs::Write("viewport_targets", "sceneTarget.Ensure failed");
        return false;
    }
    EditorCrashBreadcrumbs::Write("viewport_targets", "sceneTarget.Ensure end");
    if (!postProcessEnabled) {
        EditorCrashBreadcrumbs::Write("viewport_targets", "postProcess disabled shutdown");
        session.postProcessTargets.Shutdown();
        return true;
    }
    EditorCrashBreadcrumbs::Write("viewport_targets", "postProcessTargets.Ensure begin");
    const bool postTargetsReady = session.postProcessTargets.Ensure(render::ScenePostProcessTargetsDesc{
        .extent = renderExtent,
        .colorPolicy = render::SceneColorFormatPolicy::Auto,
    });
    EditorCrashBreadcrumbs::Write("viewport_targets", postTargetsReady ? "postProcessTargets.Ensure end ok" : "postProcessTargets.Ensure end failed");
    return postTargetsReady;
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
        .postProcessSettings = present.settings.postProcessSettings,
        .lightingConfig = present.settings.lightingConfig,
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
