#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <span>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectWidth(rect);
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectHeight(rect);
}

} // namespace

EditorSceneBgfxViewport::PendingPaintSubmitter::PendingPaintSubmitter(EditorSceneBgfxViewport& viewport) noexcept
    : viewport_(viewport) {}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::Submit(std::span<const PendingPresent> pendingPresents) {
    const std::vector<PendingPresentBatch> batches = PendingPresentBatchBuilder::Build(pendingPresents);
    if (!BuildPendingSubmissions(std::span<const PendingPresentBatch>{batches.data(), batches.size()})) {
        return false;
    }
    if (!SubmitPreparedSubmissions()) {
        return false;
    }

    viewport_.hostSurfaceStore_.ShowPresentedWindows();
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::BuildPendingSubmissions(std::span<const PendingPresentBatch> batches) {
    for (const PendingPresentBatch& batch : batches) {
        HostSurface* surface = nullptr;
        if (!PrepareHostSurfaceBatch(batch, surface)) {
            return false;
        }
        if (surface != nullptr && !AppendHostSubmissions(batch, *surface)) {
            return false;
        }
    }
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::PrepareHostSurfaceBatch(const PendingPresentBatch& batch, HostSurface*& surface) {
    surface = viewport_.EnsureHostSurface(batch.host, batch.viewportKey);
    if (surface == nullptr) {
        return false;
    }
    if (surface->presentedInCurrentPaint) {
        surface = nullptr;
        return true;
    }

    if (!viewport_.EnsureHostSurfaceWindow(*surface, batch.surfaceRect)) {
        return false;
    }

    if (!viewport_.EnsurePresentTarget(*surface, RectWidth(surface->rect), RectHeight(surface->rect))) {
        return false;
    }
    surface->presentedInCurrentPaint = true;
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::AppendHostSubmissions(const PendingPresentBatch& batch, const HostSurface& surface) {
    bool clearTarget = true;
    for (const PendingPresent* present : batch.presents) {
        if (present == nullptr) {
            continue;
        }
        render::Renderer::SceneFrameSubmission submission{};
        if (!PendingSubmissionBuilder::Build(*present, surface, clearTarget, submission)) {
            return false;
        }
        viewport_.pendingSubmissions_.push_back(submission);
        clearTarget = false;
    }
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::SubmitPreparedSubmissions() {
    if (viewport_.pendingSubmissions_.empty()) {
        return true;
    }
    if (!viewport_.renderer_.BeginFrame()) {
        return false;
    }

    const bool submitted = viewport_.renderer_.SubmitScenes(viewport_.pendingSubmissions_);
    viewport_.renderer_.EndFrame();
    if (!submitted) {
        return false;
    }
    for (const PendingPresent& present : viewport_.pendingPresents_) {
        if (present.session != nullptr) {
            present.session->submittedSceneRevision = present.settings.sceneRevision;
        }
    }
    const render::SceneRenderSubmitStats stats = viewport_.renderer_.LastSceneSubmitStats();
    const render::ScenePostProcessSettings postProcessSettings = viewport_.renderer_.DefaultPostProcessSettings();
    bool postProcessActive = false;
    bool finalCompositeActive = false;
    for (const render::Renderer::SceneFrameSubmission& submission : viewport_.pendingSubmissions_) {
        postProcessActive = postProcessActive || (submission.desc.postProcessEnabled && submission.desc.postProcess.enabled);
        finalCompositeActive = finalCompositeActive || submission.desc.finalComposite.enabled;
    }
    SceneViewportToolbarRenderer::RecordRenderStats(SceneViewportToolbarRenderStats{
        .submittedDrawCalls = stats.submittedDrawCallCount,
        .submittedMeshes = stats.submittedMeshCount,
        .gpuDispatches = stats.gpuCullingDispatchCount,
        .msaaSamples = viewport_.rendererMsaaSamples_,
        .gpuDrivenActive = stats.gpuDrivenFeatureState != render::SceneGpuDrivenFeatureState::Disabled &&
            stats.gpuDrivenFeatureState != render::SceneGpuDrivenFeatureState::CpuValidationOnly,
        .postProcessActive = postProcessActive,
        .temporalAntiAliasingActive = postProcessActive && postProcessSettings.temporalAntiAliasingEnabled,
        .bloomActive = postProcessActive && postProcessSettings.bloomEnabled && postProcessSettings.bloomStrength > 0.0F,
        .finalCompositeActive = finalCompositeActive,
    });
    SceneViewportToolbarRenderer::RecordPresentedFrame();
    return true;
}

} // namespace kb::editor

#endif
