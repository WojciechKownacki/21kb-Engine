#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include <algorithm>

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
    if (!EnsureSessionTargets(session, present.renderWidth, present.renderHeight)) {
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
    std::uint32_t renderHeight) {
    const render::RenderExtent renderExtent{renderWidth, renderHeight};
    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = renderExtent,
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
        })) {
        return false;
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
        .postProcess = session.postProcessTargets.Binding(),
        .finalComposite = render::RenderFinalCompositeTargetBinding{
            .frameBuffer = surface.presentTarget.FrameBuffer(),
            .extent = surfaceExtent,
            .outputRect = OutputRectFor(present, surface),
            .enabled = true,
            .clearTarget = clearTarget,
        },
        .cameraOverride = present.settings.cameraOverride,
        .selectedEntityIds = SelectedEntitySpan(session),
        .clearRgba = kSceneSubmitClearRgba,
        .editorSceneOverlaysEnabled = present.settings.editorSceneOverlaysEnabled,
        .editorGrid = present.settings.editorGrid,
        .editorGizmo = present.settings.editorGizmo,
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
    if (session.selectedEntityIds[0] == 0U) {
        return {};
    }
    return std::span<const std::uint64_t>{session.selectedEntityIds.data(), session.selectedEntityIds.size()};
}

} // namespace kb::editor

#endif
