#include "rendering/EditorSceneBgfxViewport.hpp"

#if defined(_WIN32)
#include "app/EditorCrashBreadcrumbs.hpp"
#include <algorithm>
#include <cstdint>
#include <sstream>
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

[[nodiscard]] const char* BoolText(bool value) noexcept {
    return value ? "1" : "0";
}

void LogSubmitDescAntiAliasing(const render::RenderSceneSubmitDesc& desc, std::uint64_t viewportKey) {
    std::ostringstream message;
    message << "BuildSubmitDesc output key=" << viewportKey
            << " postProcessEnabled=" << BoolText(desc.postProcessEnabled)
            << " postTargetsEnabled=" << BoolText(desc.postProcess.enabled)
            << " targetMsaaSamples=" << static_cast<unsigned>(desc.target.msaaSamples)
            << " overridePresent=" << BoolText(desc.postProcessSettings.has_value());
    if (desc.postProcessSettings.has_value()) {
        const render::ScenePostProcessSettings& settings = *desc.postProcessSettings;
        message << " overrideFxaa=" << BoolText(settings.fxaaEnabled)
                << " overrideTaa=" << BoolText(settings.temporalAntiAliasingEnabled)
                << " overrideJitter=" << BoolText(settings.temporalJitterEnabled)
                << " overrideBloom=" << BoolText(settings.bloomEnabled);
    }
    EditorCrashBreadcrumbs::Write("aa_trace", message.str());
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
    const std::uint8_t sceneMsaaSamples =
        (!present.settings.postProcessEnabled && present.settings.lightingConfig.lightingPath != render::SceneRenderLightingPath::Deferred)
            ? present.settings.msaaSamples
            : 0U;
    if (!EnsureSessionTargets(session, present.renderWidth, present.renderHeight, present.settings.postProcessEnabled, sceneMsaaSamples)) {
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
    bool postProcessEnabled,
    std::uint8_t msaaSamples) {
    EditorCrashBreadcrumbs::Write(
        "viewport_targets",
        "EnsureSessionTargets begin " + std::to_string(renderWidth) + "x" + std::to_string(renderHeight) +
            " post=" + (postProcessEnabled ? std::string{"1"} : std::string{"0"}) +
            " sceneMsaaSamples=" + std::to_string(msaaSamples));
    const render::RenderExtent renderExtent{renderWidth, renderHeight};
    EditorCrashBreadcrumbs::Write("viewport_targets", "sceneTarget.Ensure begin");
    if (!session.sceneTarget.Ensure(render::SceneRenderTargetDesc{
            .extent = renderExtent,
            .colorPolicy = render::SceneColorFormatPolicy::Auto,
            .msaaSamples = msaaSamples,
        })) {
        EditorCrashBreadcrumbs::Write("viewport_targets", "sceneTarget.Ensure failed");
        return false;
    }
    {
        std::ostringstream message;
        message << "sceneTarget.Ensure end msaaSamples=" << static_cast<unsigned>(session.sceneTarget.MsaaSamples())
                << " depthSampled=" << BoolText(session.sceneTarget.DepthTextureSampled())
                << " colorTex=" << session.sceneTarget.ColorTexture().idx
                << " resolvedColorTex=" << session.sceneTarget.ResolvedColorTexture().idx
                << " depthTex=" << session.sceneTarget.DepthTexture().idx;
        EditorCrashBreadcrumbs::Write("viewport_targets", message.str());
        EditorCrashBreadcrumbs::Write("aa_trace", "Scene target confirmed " + message.str());
    }
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
    LogSubmitDescAntiAliasing(desc, present.settings.viewportKey);
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
