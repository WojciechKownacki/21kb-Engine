#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "app/EditorCrashBreadcrumbs.hpp"
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <span>

namespace kb::editor {
namespace {

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectWidth(rect);
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectHeight(rect);
}

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "1" : "0";
}

[[nodiscard]] const char* AntiAliasingModeName(EditorAntiAliasingMode mode) noexcept {
    switch (mode) {
    case EditorAntiAliasingMode::None:
        return "None";
    case EditorAntiAliasingMode::Fxaa:
        return "FXAA";
    case EditorAntiAliasingMode::Taa:
        return "TAA";
    case EditorAntiAliasingMode::Msaa:
        return "MSAA";
    }
    return "Unknown";
}

} // namespace

EditorSceneBgfxViewport::PendingPaintSubmitter::PendingPaintSubmitter(EditorSceneBgfxViewport& viewport) noexcept
    : viewport_(viewport) {}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::Submit(std::span<const PendingPresent> pendingPresents) {
    EditorCrashBreadcrumbs::WriteValue("viewport_submit", "Submit begin pending", pendingPresents.size());
    const std::vector<PendingPresentBatch> batches = PendingPresentBatchBuilder::Build(pendingPresents);
    EditorCrashBreadcrumbs::WriteValue("viewport_submit", "batch count", batches.size());
    if (!BuildPendingSubmissions(std::span<const PendingPresentBatch>{batches.data(), batches.size()})) {
        EditorCrashBreadcrumbs::Write("viewport_submit", "BuildPendingSubmissions failed");
        return false;
    }
    if (!SubmitPreparedSubmissions()) {
        EditorCrashBreadcrumbs::Write("viewport_submit", "SubmitPreparedSubmissions failed");
        return false;
    }

    viewport_.hostSurfaceStore_.ShowPresentedWindows();
    EditorCrashBreadcrumbs::Write("viewport_submit", "Submit end ok");
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
    EditorCrashBreadcrumbs::Write(
        "viewport_submit",
        "PrepareHostSurfaceBatch begin key=" + std::to_string(batch.viewportKey) +
            " presents=" + std::to_string(batch.presents.size()));
    surface = viewport_.EnsureHostSurface(batch.host, batch.viewportKey);
    if (surface == nullptr) {
        viewport_.SetFailureDetail("Could not allocate or resolve the host surface entry for a queued viewport present.");
        return false;
    }
    viewport_.hostSurfaceStore_.MarkLayoutActive(*surface);
    if (surface->presentedInCurrentPaint) {
        surface = nullptr;
        EditorCrashBreadcrumbs::Write("viewport_submit", "PrepareHostSurfaceBatch already presented");
        return true;
    }

    EditorCrashBreadcrumbs::Write("viewport_submit", "EnsureHostSurfaceWindow begin");
    if (!viewport_.EnsureHostSurfaceWindow(*surface, batch.surfaceRect)) {
        viewport_.SetFailureDetail("Native child window creation or update failed for a queued viewport present.");
        return false;
    }

    EditorCrashBreadcrumbs::Write("viewport_submit", "EnsurePresentTarget begin");
    if (!viewport_.EnsurePresentTarget(*surface, RectWidth(surface->rect), RectHeight(surface->rect))) {
        return false;
    }
    surface->presentedInCurrentPaint = true;
    EditorCrashBreadcrumbs::Write("viewport_submit", "PrepareHostSurfaceBatch end");
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::AppendHostSubmissions(const PendingPresentBatch& batch, const HostSurface& surface) {
    EditorCrashBreadcrumbs::Write("viewport_submit", "AppendHostSubmissions begin");
    bool clearTarget = true;
    for (const PendingPresent* present : batch.presents) {
        if (present == nullptr) {
            continue;
        }
        render::Renderer::SceneFrameSubmission submission{};
        EditorCrashBreadcrumbs::Write(
            "viewport_submit",
            "PendingSubmissionBuilder build begin key=" + std::to_string(present->settings.viewportKey) +
                " render=" + std::to_string(present->renderWidth) + "x" + std::to_string(present->renderHeight) +
                " post=" + (present->settings.postProcessEnabled ? std::string{"1"} : std::string{"0"}));
        if (!PendingSubmissionBuilder::Build(*present, surface, clearTarget, submission)) {
            viewport_.SetFailureDetail("Viewport submission assembly failed before renderer submission.");
            return false;
        }
        EditorCrashBreadcrumbs::Write("viewport_submit", "PendingSubmissionBuilder build end");
        viewport_.pendingSubmissions_.push_back(submission);
        clearTarget = false;
    }
    EditorCrashBreadcrumbs::WriteValue("viewport_submit", "AppendHostSubmissions end submissions", viewport_.pendingSubmissions_.size());
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::SubmitPreparedSubmissions() {
    EditorCrashBreadcrumbs::WriteValue("viewport_submit", "SubmitPreparedSubmissions begin submissions", viewport_.pendingSubmissions_.size());
    if (viewport_.pendingSubmissions_.empty()) {
        EditorCrashBreadcrumbs::Write("viewport_submit", "SubmitPreparedSubmissions no submissions");
        return true;
    }
    if (viewport_.backendSettings_ != nullptr) {
        render::ScenePostProcessSettings settings = viewport_.renderer_.DefaultPostProcessSettings();
        {
            std::ostringstream message;
            message << "PendingPaint before renderer default sync"
                    << " uiFxaa=" << BoolText(viewport_.backendSettings_->FxaaEnabled())
                    << " uiTaa=" << BoolText(viewport_.backendSettings_->TemporalAntiAliasingEnabled())
                    << " uiMsaaSamples=" << static_cast<unsigned>(viewport_.backendSettings_->MsaaSamples())
                    << " rendererDefaultFxaa=" << BoolText(settings.fxaaEnabled)
                    << " rendererDefaultTaa=" << BoolText(settings.temporalAntiAliasingEnabled)
                    << " rendererDefaultJitter=" << BoolText(settings.temporalJitterEnabled);
            EditorCrashBreadcrumbs::Write("aa_trace", message.str());
        }
        settings.fxaaEnabled = viewport_.backendSettings_->FxaaEnabled();
        settings.temporalAntiAliasingEnabled = viewport_.backendSettings_->TemporalAntiAliasingEnabled();
        settings.temporalJitterEnabled = viewport_.backendSettings_->TemporalAntiAliasingEnabled();
        settings.bloomEnabled = viewport_.backendSettings_->BloomEnabled();
        viewport_.renderer_.SetDefaultPostProcessSettings(settings);
        {
            const render::ScenePostProcessSettings confirmed = viewport_.renderer_.DefaultPostProcessSettings();
            std::ostringstream message;
            message << "PendingPaint after renderer default sync"
                    << " confirmedFxaa=" << BoolText(confirmed.fxaaEnabled)
                    << " confirmedTaa=" << BoolText(confirmed.temporalAntiAliasingEnabled)
                    << " confirmedJitter=" << BoolText(confirmed.temporalJitterEnabled)
                    << " confirmedBloom=" << BoolText(confirmed.bloomEnabled);
            EditorCrashBreadcrumbs::Write("aa_trace", message.str());
        }
    }
    EditorCrashBreadcrumbs::Write("viewport_submit", "Renderer BeginFrame begin");
    if (!viewport_.renderer_.BeginFrame()) {
        viewport_.SetFailureDetail("Renderer BeginFrame failed while presenting queued editor viewports.");
        return false;
    }

    EditorCrashBreadcrumbs::Write("viewport_submit", "Renderer SubmitScenes begin");
    const bool submitted = viewport_.renderer_.SubmitScenes(viewport_.pendingSubmissions_);
    EditorCrashBreadcrumbs::Write("viewport_submit", submitted ? "Renderer SubmitScenes end ok" : "Renderer SubmitScenes end failed");
    EditorCrashBreadcrumbs::Write("viewport_submit", "Renderer EndFrame begin");
    viewport_.renderer_.EndFrame();
    EditorCrashBreadcrumbs::Write("viewport_submit", "Renderer EndFrame end");
    if (!submitted) {
        viewport_.SetFailureDetail("Renderer SubmitScenes failed while presenting queued editor viewports.");
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
    bool depthTextureValid = false;
    bool gridVisible = false;
    bool postTargetsEnabled = false;
    std::uint8_t actualSceneMsaaSamples = 0U;
    for (const render::Renderer::SceneFrameSubmission& submission : viewport_.pendingSubmissions_) {
        postProcessActive = postProcessActive || (submission.desc.postProcessEnabled && submission.desc.postProcess.enabled);
        postTargetsEnabled = postTargetsEnabled || submission.desc.postProcess.enabled;
        finalCompositeActive = finalCompositeActive || submission.desc.finalComposite.enabled;
        depthTextureValid = depthTextureValid || bgfx::isValid(submission.desc.target.depthTexture);
        gridVisible = gridVisible || submission.desc.editorGrid.visible;
        actualSceneMsaaSamples = std::max(actualSceneMsaaSamples, submission.desc.target.msaaSamples);
    }
    const bool effectiveTemporalJitter = postProcessActive &&
        postProcessSettings.temporalAntiAliasingEnabled &&
        postProcessSettings.temporalJitterEnabled;
    {
        std::ostringstream message;
        message << "Toolbar/render display stats"
                << " postProcessActive=" << BoolText(postProcessActive)
                << " finalCompositeActive=" << BoolText(finalCompositeActive)
                << " displayedTaaActive=" << BoolText(postProcessActive && postProcessSettings.temporalAntiAliasingEnabled)
                << " displayedMsaaSamples=" << static_cast<unsigned>(viewport_.rendererMsaaSamples_)
                << " actualSceneMsaaSamples=" << static_cast<unsigned>(actualSceneMsaaSamples)
                << " displayedBloomActive=" << BoolText(postProcessActive && postProcessSettings.bloomEnabled && postProcessSettings.bloomStrength > 0.0F);
        EditorCrashBreadcrumbs::Write("aa_trace", message.str());
    }
    {
        std::ostringstream message;
        message << "AA state"
                << " postProcess=" << BoolText(postProcessActive)
                << " taa=" << BoolText(postProcessActive && postProcessSettings.temporalAntiAliasingEnabled)
                << " fxaa=" << BoolText(postProcessActive && postProcessSettings.fxaaEnabled)
                << " effectiveJitter=" << BoolText(effectiveTemporalJitter)
                << " msaa=" << static_cast<unsigned>(actualSceneMsaaSamples)
                << " depth=" << BoolText(depthTextureValid)
                << " finalComposite=" << BoolText(finalCompositeActive)
                << " grid=" << BoolText(gridVisible);
        viewport_.ReportAaTrace(message.str());
    }
    {
        const EditorAntiAliasingMode uiMode = viewport_.backendSettings_ == nullptr
            ? EditorAntiAliasingMode::None
            : viewport_.backendSettings_->AntiAliasingMode();
        std::ostringstream message;
        message << "AA route"
                << " ui=" << AntiAliasingModeName(uiMode)
                << " rendererFxaa=" << BoolText(postProcessSettings.fxaaEnabled)
                << " rendererTaa=" << BoolText(postProcessSettings.temporalAntiAliasingEnabled)
                << " rendererJitter=" << BoolText(postProcessSettings.temporalJitterEnabled)
                << " postProcess=" << BoolText(postProcessActive)
                << " postTargets=" << BoolText(postTargetsEnabled)
                << " finalComposite=" << BoolText(finalCompositeActive)
                << " depth=" << BoolText(depthTextureValid)
                << " targetMsaa=" << static_cast<unsigned>(actualSceneMsaaSamples)
                << " rendererMsaa=" << static_cast<unsigned>(viewport_.rendererMsaaSamples_)
                << " historyBlend=" << postProcessSettings.temporalHistoryBlend
                << " bloom=" << BoolText(postProcessSettings.bloomEnabled)
                << " tonemap=" << static_cast<int>(postProcessSettings.outputTransform.tonemap)
                << " exposure=" << postProcessSettings.outputTransform.exposureStops
                << " gamma=" << postProcessSettings.outputTransform.gamma
                << " autoExposure=" << BoolText(postProcessSettings.outputTransform.autoExposure.enabled);
        viewport_.ReportAaRouteTrace(message.str());
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
