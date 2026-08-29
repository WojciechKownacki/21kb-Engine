#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"
#include "diagnostics/EditorLagTrace.hpp"

#include <algorithm>
#include <chrono>
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

// A renderer stage this slow is a stall worth a trace line. Below it the frame is healthy
// and nothing is written, so nothing may be formatted either.
constexpr double kSlowStageMs = 4.0;
constexpr double kSlowPaintStageMs = 8.0;

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
    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto batchStart = std::chrono::steady_clock::now();
    const std::vector<PendingPresentBatch> batches = PendingPresentBatchBuilder::Build(pendingPresents);
    const double batchMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - batchStart).count();
    // Built only for a stage that actually stalled. Formatting it on every frame cost four
    // string allocations to describe frames nobody would ever read about.
    const auto detail = [&pendingPresents, &batches] {
        std::ostringstream text;
        text << "presents=" << pendingPresents.size() << " batches=" << batches.size();
        return text.str();
    };
    if (batchMs >= kSlowPaintStageMs) {
        diagnostics::EditorLagTrace::Slow("viewport-batch-build", eventId, batchMs, detail(), kSlowPaintStageMs);
    }
    const auto prepareStart = std::chrono::steady_clock::now();
    if (!BuildPendingSubmissions(std::span<const PendingPresentBatch>{batches.data(), batches.size()})) {
        return false;
    }
    const double prepareMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - prepareStart).count();
    if (prepareMs >= kSlowPaintStageMs) {
        diagnostics::EditorLagTrace::Slow("viewport-prepare", eventId, prepareMs, detail(), kSlowPaintStageMs);
    }
    const auto submitStart = std::chrono::steady_clock::now();
    if (!SubmitPreparedSubmissions()) {
        return false;
    }
    const double submitMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - submitStart).count();
    if (submitMs >= kSlowPaintStageMs) {
        diagnostics::EditorLagTrace::Slow("viewport-render-frame", eventId, submitMs, detail(), kSlowPaintStageMs);
    }

    const auto showStart = std::chrono::steady_clock::now();
    viewport_.hostSurfaceStore_.ShowPresentedWindows();
    const double showMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - showStart).count();
    if (showMs >= kSlowPaintStageMs) {
        diagnostics::EditorLagTrace::Slow("viewport-show-windows", eventId, showMs, detail(), kSlowPaintStageMs);
    }
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
        viewport_.SetFailureDetail("Could not allocate or resolve the host surface entry for a queued viewport present.");
        return false;
    }
    viewport_.hostSurfaceStore_.MarkLayoutActive(*surface);
    if (surface->presentedInCurrentPaint) {
        surface = nullptr;
        return true;
    }

    if (!viewport_.EnsureHostSurfaceWindow(*surface, batch.surfaceRect)) {
        viewport_.SetFailureDetail("Native child window creation or update failed for a queued viewport present.");
        return false;
    }

    if (!viewport_.EnsurePresentTarget(*surface, RectWidth(surface->rect), RectHeight(surface->rect))) {
        return false;
    }
    const PresentSettings* overlaySettings = nullptr;
    for (const PendingPresent* present : batch.presents) {
        if (present != nullptr) overlaySettings = &present->settings;
    }
    if (overlaySettings == nullptr) {
        viewport_.SetFailureDetail("Viewport text overlay settings were missing from its present batch.");
        return false;
    }
    if (overlaySettings->viewportTextLabels.empty()) {
        surface->textOverlay.Hide();
    } else if (!surface->textOverlay.Ensure(
                   viewport_.instance_, surface->clipWindow,
                   RectWidth(surface->rect), RectHeight(surface->rect)) ||
        !surface->textOverlay.Update(overlaySettings->viewportTextLabels)) {
        viewport_.SetFailureDetail("Viewport text overlay could not rasterize or present its labels.");
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
            viewport_.SetFailureDetail("Viewport submission assembly failed before renderer submission.");
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
    const auto frameStart = std::chrono::steady_clock::now();
    if (viewport_.backendSettings_ != nullptr) {
        render::ScenePostProcessSettings settings = viewport_.renderer_.DefaultPostProcessSettings();
        settings.fxaaEnabled = viewport_.backendSettings_->FxaaEnabled();
        settings.temporalAntiAliasingEnabled = viewport_.backendSettings_->TemporalAntiAliasingEnabled();
        settings.temporalJitterEnabled = viewport_.backendSettings_->TemporalAntiAliasingEnabled();
        settings.bloomEnabled = viewport_.backendSettings_->BloomEnabled();
        viewport_.renderer_.SetDefaultPostProcessSettings(settings);
    }
    const std::uint64_t eventId = diagnostics::EditorLagTrace::NextEventId();
    const auto frameDetail = [this] {
        const std::size_t fullSyncCount = std::ranges::count_if(
            viewport_.pendingSubmissions_, [](const render::Renderer::SceneFrameSubmission& submission) {
                return submission.desc.synchronizeScene;
            });
        const std::size_t runtimeTransformSyncCount = std::ranges::count_if(
            viewport_.pendingSubmissions_, [](const render::Renderer::SceneFrameSubmission& submission) {
                return submission.desc.transformAffineSync;
            });
        std::size_t dirtyEntityCount = 0U;
        for (const render::Renderer::SceneFrameSubmission& submission : viewport_.pendingSubmissions_) {
            dirtyEntityCount += submission.desc.dirtySceneEntityIds.size();
        }
        std::ostringstream detail;
        detail << "submissions=" << viewport_.pendingSubmissions_.size()
               << " presents=" << viewport_.pendingPresents_.size()
               << " fullSync=" << fullSyncCount
               << " runtimeTransformSync=" << runtimeTransformSyncCount
               << " dirtyEntities=" << dirtyEntityCount
               << " backend=" << viewport_.ActiveBackendLabel();
        return detail.str();
    };
    const auto beginStart = std::chrono::steady_clock::now();
    if (!viewport_.renderer_.BeginFrame()) {
        viewport_.SetFailureDetail("Renderer BeginFrame failed while presenting queued editor viewports.");
        return false;
    }
    const double beginMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - beginStart).count();
    if (beginMs >= kSlowStageMs) {
        diagnostics::EditorLagTrace::Slow("renderer-begin-frame", eventId, beginMs, frameDetail(), kSlowStageMs);
    }

    const auto submitStart = std::chrono::steady_clock::now();
    const bool submitted = viewport_.renderer_.SubmitScenes(viewport_.pendingSubmissions_);
    const double submitMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - submitStart).count();
    if (submitMs >= kSlowStageMs) {
        diagnostics::EditorLagTrace::Slow("renderer-submit-scenes", eventId, submitMs, frameDetail(), kSlowStageMs);
    }
    const auto endStart = std::chrono::steady_clock::now();
    viewport_.renderer_.EndFrame();
    const double endMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - endStart).count();
    if (endMs >= kSlowStageMs) {
        std::ostringstream detail;
        detail << frameDetail();
        if (const bgfx::Stats* stats = bgfx::getStats(); stats != nullptr) {
            const double timerToMs = stats->cpuTimerFreq > 0
                ? 1000.0 / static_cast<double>(stats->cpuTimerFreq)
                : 0.0;
            detail << " waitSubmit=" << static_cast<double>(stats->waitSubmit) * timerToMs << "ms"
                   << " waitRender=" << static_cast<double>(stats->waitRender) * timerToMs << "ms"
                   << " cpuFrame=" << static_cast<double>(stats->cpuTimeFrame) * timerToMs << "ms"
                   << " draws=" << stats->numDraw
                   << " compute=" << stats->numCompute
                   << " blit=" << stats->numBlit
                   << " views=" << stats->numViews
                   << " framebuffers=" << stats->numFrameBuffers;
        }
        diagnostics::EditorLagTrace::Slow("renderer-end-frame", eventId, endMs, detail.str(), kSlowStageMs);
    }
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
    SceneViewportToolbarRenderer::RecordFrameMilliseconds(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
    return true;
}

} // namespace kb::editor

#endif
