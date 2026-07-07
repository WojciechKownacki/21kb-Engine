#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "kb/render/ViewIdPolicy.hpp"
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "rendering/SceneViewportToolbarRenderer.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <span>

namespace kb::editor {
namespace {

constexpr bgfx::ViewId kMaterialGraphImguiViewId = render::ViewId::Max - 1U;

[[nodiscard]] std::uint32_t RectWidth(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectWidth(rect);
}

[[nodiscard]] std::uint32_t RectHeight(const RECT& rect) noexcept {
    return EditorSceneViewportGeometry::RectHeight(rect);
}

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "1" : "0";
}

[[nodiscard]] EditorImguiInputState ImguiInputForSurface(HWND host, const RECT& surfaceRect) noexcept {
    EditorImguiInputState input{};
    POINT cursor{};
    if (host != nullptr && GetCursorPos(&cursor) != 0 && ScreenToClient(host, &cursor) != 0) {
        input.mouseX = static_cast<float>(cursor.x - surfaceRect.left);
        input.mouseY = static_cast<float>(cursor.y - surfaceRect.top);
        input.mouseVisible = true;
    }
    input.leftMouseDown = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
    input.rightMouseDown = (GetKeyState(VK_RBUTTON) & 0x8000) != 0;
    input.middleMouseDown = (GetKeyState(VK_MBUTTON) & 0x8000) != 0;
    return input;
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
    const std::vector<PendingPresentBatch> batches = PendingPresentBatchBuilder::Build(pendingPresents);
    if (!BuildPendingSubmissions(std::span<const PendingPresentBatch>{batches.data(), batches.size()})) {
        return false;
    }
    for (const PendingImguiGraphPresent& present : viewport_.pendingImguiGraphPresents_) {
        HostSurface* surface = nullptr;
        if (!PrepareImguiGraphSurface(present, surface)) {
            return false;
        }
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

bool EditorSceneBgfxViewport::PendingPaintSubmitter::PrepareImguiGraphSurface(
    const PendingImguiGraphPresent& present,
    HostSurface*& surface) {
    surface = viewport_.EnsureHostSurface(present.host, present.viewportKey);
    if (surface == nullptr) {
        viewport_.SetFailureDetail("Could not allocate or resolve the host surface entry for the material graph ImGui present.");
        return false;
    }
    viewport_.hostSurfaceStore_.MarkLayoutActive(*surface);
    if (surface->presentedInCurrentPaint) {
        return true;
    }

    if (!viewport_.EnsureHostSurfaceWindow(*surface, present.surfaceRect, true)) {
        viewport_.SetFailureDetail("Native child window creation or update failed for the material graph ImGui present.");
        return false;
    }
    if (!viewport_.EnsurePresentTarget(*surface, RectWidth(surface->rect), RectHeight(surface->rect))) {
        return false;
    }
    surface->presentedInCurrentPaint = true;
    return true;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::SubmitImguiGraphPresents() {
    if (viewport_.pendingImguiGraphPresents_.empty()) {
        return true;
    }
    if (!viewport_.imguiRenderer_.Initialize()) {
        viewport_.SetFailureDetail("Material graph ImGui bgfx renderer initialization failed.");
        return false;
    }

    bool submittedAny = false;
    for (const PendingImguiGraphPresent& present : viewport_.pendingImguiGraphPresents_) {
        HostSurface* surface = viewport_.FindHostSurface(present.host, present.viewportKey);
        if (surface == nullptr || !surface->presentTarget.IsValid()) {
            viewport_.SetFailureDetail("Material graph ImGui present target was missing at submit time.");
            viewport_.imguiRenderer_.ClearCurrentContext();
            return false;
        }

        const std::uint32_t width = RectWidth(surface->rect);
        const std::uint32_t height = RectHeight(surface->rect);
        if (!viewport_.imguiRenderer_.BeginFrame(width, height, 1.0F / 60.0F, ImguiInputForSurface(present.host, surface->rect))) {
            viewport_.SetFailureDetail("Material graph ImGui frame setup failed.");
            viewport_.imguiRenderer_.ClearCurrentContext();
            return false;
        }

        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        const bool open = ImGui::Begin(
            "MaterialGraphImguiHost",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);
        MaterialImguiNodeEditorFrameResult frameResult{};
        if (open) {
            frameResult = viewport_.materialGraphImguiRenderer_.Render(
                present.model,
                ImVec2(static_cast<float>(width), static_cast<float>(height)),
                [this](const MaterialImguiTexturePreview& preview) {
                    return viewport_.imguiRenderer_.EnsureTexture(preview.cacheKey, preview.width, preview.height, preview.bgra);
                });
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        if (present.frameHandler) {
            present.frameHandler(present.model, frameResult);
        }

        if (!viewport_.imguiRenderer_.SubmitFrame(kMaterialGraphImguiViewId, surface->presentTarget.FrameBuffer())) {
            viewport_.SetFailureDetail("Material graph ImGui draw data submission failed.");
            viewport_.imguiRenderer_.ClearCurrentContext();
            return false;
        }
        submittedAny = true;
    }
    return submittedAny;
}

bool EditorSceneBgfxViewport::PendingPaintSubmitter::SubmitPreparedSubmissions() {
    const bool hasSceneSubmissions = !viewport_.pendingSubmissions_.empty();
    const bool hasImguiSubmissions = !viewport_.pendingImguiGraphPresents_.empty();
    if (!hasSceneSubmissions && !hasImguiSubmissions) {
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
        }
    }
    if (!viewport_.renderer_.BeginFrame()) {
        viewport_.SetFailureDetail("Renderer BeginFrame failed while presenting queued editor viewports.");
        return false;
    }

    const bool submitted = !hasSceneSubmissions || viewport_.renderer_.SubmitScenes(viewport_.pendingSubmissions_);
    const bool imguiSubmitted = submitted && SubmitImguiGraphPresents();
    viewport_.renderer_.EndFrame();
    if (!submitted || !imguiSubmitted) {
        if (!submitted) {
            viewport_.SetFailureDetail("Renderer SubmitScenes failed while presenting queued editor viewports.");
        }
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
