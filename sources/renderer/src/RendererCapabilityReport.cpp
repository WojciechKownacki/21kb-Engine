#include "kb/render/RendererCapabilityReport.hpp"

#include <algorithm>

namespace kb::render {
namespace {

constexpr std::uint64_t kSceneColorTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

constexpr std::uint64_t kSceneDepthTextureFlags =
    BGFX_TEXTURE_RT |
    BGFX_SAMPLER_MIN_POINT |
    BGFX_SAMPLER_MAG_POINT |
    BGFX_SAMPLER_MIP_POINT |
    BGFX_SAMPLER_U_CLAMP |
    BGFX_SAMPLER_V_CLAMP;

void AddFallbackReason(RendererCapabilityReport& report, const char* reason) noexcept {
    if (reason == nullptr || report.fallbackReasonCount >= report.fallbackReasons.size()) {
        return;
    }
    report.fallbackReasons[report.fallbackReasonCount] = reason;
    ++report.fallbackReasonCount;
}

[[nodiscard]] bool SupportedFlagsContain(std::uint64_t supported, std::uint64_t flag) noexcept {
    return (supported & flag) != 0ULL;
}

[[nodiscard]] bool ContainsBackend(const bgfx::RendererType::Enum* backends, std::uint8_t count, bgfx::RendererType::Enum backend) noexcept {
    if (backends == nullptr) {
        return false;
    }
    return std::find(backends, backends + count, backend) != backends + count;
}

} // namespace

const char* RendererBackendName(bgfx::RendererType::Enum renderer) noexcept {
    const char* name = bgfx::getRendererName(renderer);
    return name == nullptr ? "Unknown" : name;
}

bgfx::RendererType::Enum ResolvePreferredRendererBackend(const bgfx::RendererType::Enum* supportedBackends, std::uint8_t supportedBackendCount) noexcept {
    constexpr std::array<bgfx::RendererType::Enum, 4U> preferredBackends{
        bgfx::RendererType::Direct3D12,
        bgfx::RendererType::Vulkan,
        bgfx::RendererType::Direct3D11,
        bgfx::RendererType::Metal,
    };

    for (const bgfx::RendererType::Enum backend : preferredBackends) {
        if (ContainsBackend(supportedBackends, supportedBackendCount, backend)) {
            return backend;
        }
    }
    return bgfx::RendererType::Count;
}

RendererCapabilityReport BuildRendererCapabilityReport(bgfx::RendererType::Enum requestedBackend) noexcept {
    RendererCapabilityReport report{};
    report.requestedBackend = requestedBackend;
    report.backendPreference = requestedBackend == bgfx::RendererType::Count ? RendererBackendPreference::Auto : RendererBackendPreference::Explicit;

    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        AddFallbackReason(report, "bgfx capabilities are unavailable before renderer initialization");
        return report;
    }

    report.initialized = true;
    report.selectedBackend = caps->rendererType;
    report.selectedBackendName = RendererBackendName(caps->rendererType);
    report.backendSelectionReason = requestedBackend == bgfx::RendererType::Count
        ? "bgfx selected the highest-priority supported backend"
        : (requestedBackend == caps->rendererType ? "explicit requested backend selected" : "requested backend changed by bgfx during initialization");
    report.supportedFlags = caps->supported;
    report.vendorId = caps->vendorId;
    report.deviceId = caps->deviceId;
    report.homogeneousDepth = caps->homogeneousDepth;
    report.originBottomLeft = caps->originBottomLeft;
    report.computeSupported = SupportedFlagsContain(caps->supported, BGFX_CAPS_COMPUTE);
    report.indirectDrawSupported = SupportedFlagsContain(caps->supported, BGFX_CAPS_DRAW_INDIRECT);
    report.textureBlitSupported = SupportedFlagsContain(caps->supported, BGFX_CAPS_TEXTURE_BLIT);
    report.textureReadbackSupported = SupportedFlagsContain(caps->supported, BGFX_CAPS_TEXTURE_READ_BACK);
    report.hdrExposureMeteringSupported = report.textureBlitSupported && report.textureReadbackSupported;
    report.gpuDrivenComputeCullingSupported = report.computeSupported;
    report.gpuDrivenIndirectSubmitSupported = report.computeSupported && report.indirectDrawSupported;
    report.gpuDrivenMeshletSubmitSupported = false;
    report.multipleWindowsSupported = SupportedFlagsContain(caps->supported, BGFX_CAPS_SWAP_CHAIN);
    report.nativePresentColor = bgfx::TextureFormat::BGRA8;
    report.nativePresentDepth = bgfx::TextureFormat::Count;
    report.nativePresentColorOnly = report.nativePresentDepth == bgfx::TextureFormat::Count;
    report.sceneColor = SelectSceneColorFormat(SceneColorFormatPolicy::Auto, kSceneColorTextureFlags);
    report.sceneDepth = SelectSceneDepthFormat(kSceneDepthTextureFlags);
    report.hdrTextureSupported = report.sceneColor.format == bgfx::TextureFormat::RGBA16F;
    report.depthPreferredSupported = report.sceneDepth.IsPreferred();
    report.limits = RendererCapabilityLimits{
        .maxDrawCalls = caps->limits.maxDrawCalls,
        .maxBlits = caps->limits.maxBlits,
        .maxTextureSize = caps->limits.maxTextureSize,
        .maxViews = caps->limits.maxViews,
        .maxFrameBuffers = caps->limits.maxFrameBuffers,
        .maxTextures = caps->limits.maxTextures,
        .maxPrograms = caps->limits.maxPrograms,
        .maxShaders = caps->limits.maxShaders,
        .maxComputeBindings = caps->limits.maxComputeBindings,
        .maxEncoders = caps->limits.maxEncoders,
    };

    if (requestedBackend != bgfx::RendererType::Count && requestedBackend != caps->rendererType) {
        AddFallbackReason(report, "requested backend was not selected by bgfx");
    }
    if (report.sceneColor.status == SceneTargetFormatSelectionStatus::CapabilityFallback) {
        AddFallbackReason(report, "RGBA16F scene HDR target is unsupported; using lower-quality scene color format");
    } else if (report.sceneColor.status == SceneTargetFormatSelectionStatus::Unsupported) {
        AddFallbackReason(report, "no supported scene color render target format found");
    }
    if (report.sceneDepth.status == SceneTargetFormatSelectionStatus::CapabilityFallback) {
        AddFallbackReason(report, "D32F/D32 reverse-Z depth target is unsupported; using D24S8 fallback");
    } else if (report.sceneDepth.status == SceneTargetFormatSelectionStatus::Unsupported) {
        AddFallbackReason(report, "no supported scene depth render target format found");
    }
    if (!report.computeSupported) {
        AddFallbackReason(report, "compute shaders are unsupported on selected backend");
    }
    if (!report.indirectDrawSupported) {
        AddFallbackReason(report, "indirect draw submit is unsupported on selected backend");
    }
    if (!report.gpuDrivenMeshletSubmitSupported) {
        AddFallbackReason(report, "meshlet submit is not implemented for the selected renderer path");
    }
    if (!report.textureBlitSupported) {
        AddFallbackReason(report, "texture blit is unsupported on selected backend");
    }
    if (!report.textureReadbackSupported) {
        AddFallbackReason(report, "texture readback is unsupported; HDR auto-exposure will use scene-lighting fallback");
    }
    if (!report.multipleWindowsSupported) {
        AddFallbackReason(report, "bgfx swap-chain/multiple-window presentation is unsupported on selected backend");
    }

    return report;
}

std::string_view RendererCapabilitySummary(const RendererCapabilityReport& report) noexcept {
    if (!report.initialized) {
        return "Renderer capabilities unavailable";
    }
    if (report.fallbackReasonCount == 0U) {
        return "Renderer capabilities selected without quality fallback";
    }
    return "Renderer capabilities selected with explicit fallback diagnostics";
}

} // namespace kb::render
