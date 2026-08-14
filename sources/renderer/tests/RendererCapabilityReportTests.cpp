#include "RendererTestSupport.hpp"

#include "kb/render/Renderer.hpp"
#include "kb/render/RenderSurface.hpp"
#include "kb/render/RendererCapabilityReport.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::render::tests {
namespace {

class HeadlessSurface final : public RenderSurface {
public:
    [[nodiscard]] std::uint32_t Width() const noexcept override {
        return 16U;
    }

    [[nodiscard]] std::uint32_t Height() const noexcept override {
        return 16U;
    }

    [[nodiscard]] void* NativeWindowHandle() const noexcept override {
        return nullptr;
    }

    [[nodiscard]] void* NativeDisplayHandle() const noexcept override {
        return nullptr;
    }
};

void PreferredBackendPolicyUsesProductionPriorityOrder() {
    constexpr std::array<bgfx::RendererType::Enum, 3U> d3d11Only{
        bgfx::RendererType::Noop,
        bgfx::RendererType::Direct3D11,
        bgfx::RendererType::OpenGL,
    };
    Require(
        ResolvePreferredRendererBackend(d3d11Only.data(), static_cast<std::uint8_t>(d3d11Only.size())) == bgfx::RendererType::Direct3D11,
        "Backend policy did not choose D3D11 when D3D12/Vulkan are unavailable");

    constexpr std::array<bgfx::RendererType::Enum, 3U> vulkanBeforeD3d11{
        bgfx::RendererType::Direct3D11,
        bgfx::RendererType::Vulkan,
        bgfx::RendererType::Noop,
    };
    Require(
        ResolvePreferredRendererBackend(vulkanBeforeD3d11.data(), static_cast<std::uint8_t>(vulkanBeforeD3d11.size())) == bgfx::RendererType::Vulkan,
        "Backend policy did not prefer Vulkan over D3D11");

    constexpr std::array<bgfx::RendererType::Enum, 3U> d3d12BeforeAll{
        bgfx::RendererType::Vulkan,
        bgfx::RendererType::Direct3D11,
        bgfx::RendererType::Direct3D12,
    };
    Require(
        ResolvePreferredRendererBackend(d3d12BeforeAll.data(), static_cast<std::uint8_t>(d3d12BeforeAll.size())) == bgfx::RendererType::Direct3D12,
        "Backend policy did not prefer D3D12 over Vulkan and D3D11");
}

void RendererExposesRuntimeCapabilityReport() {
    HeadlessSurface surface;
    DisplayConfig config{};
    config.allowHeadlessNoop = true;
    config.preferredBgfxRendererType = static_cast<std::int32_t>(bgfx::RendererType::Noop);

    Renderer renderer;
    Require(renderer.Initialize(surface, &config), "Renderer did not initialize for capability report test");

    const RendererCapabilityReport& report = renderer.CapabilityReport();
    Require(report.initialized, "Renderer capability report was not marked initialized");
    Require(report.requestedBackend == bgfx::RendererType::Noop, "Capability report did not preserve requested backend");
    Require(report.selectedBackend == bgfx::RendererType::Noop, "Capability report did not expose selected Noop backend");
    Require(std::string_view{report.selectedBackendName}.size() > 0U, "Capability report did not expose backend name");
    Require(report.backendPreference == RendererBackendPreference::Explicit, "Capability report did not mark explicit backend preference");
    Require(report.limits.maxViews > 0U, "Capability report did not expose bgfx max views");
    Require(report.limits.maxFrameBuffers > 0U, "Capability report did not expose bgfx max framebuffers");
    Require(report.limits.maxTextures > 0U, "Capability report did not expose bgfx max textures");
    Require(report.sceneColor.status != SceneTargetFormatSelectionStatus::Unsupported, "Capability report did not evaluate scene color format support");
    Require(report.sceneDepth.status != SceneTargetFormatSelectionStatus::Unsupported, "Capability report did not evaluate scene depth format support");
    Require(
        report.hdrExposureMeteringSupported == (report.textureBlitSupported && report.textureReadbackSupported),
        "Capability report did not classify HDR exposure metering from blit/readback capabilities");
    Require(
        report.gpuDrivenComputeCullingSupported == report.computeSupported,
        "Capability report did not gate GPU-driven compute culling on compute support");
    Require(
        report.gpuDrivenIndirectSubmitSupported == (report.computeSupported && report.indirectDrawSupported),
        "Capability report did not gate GPU-driven indirect submit on compute and indirect draw support");
    Require(!report.gpuDrivenMeshletSubmitSupported, "Capability report should keep meshlet submit disabled until a meshlet path exists");
    Require(report.particleGpuDrawingSupported == report.particleInstancingSupported &&
            report.particleSubtractiveBlendSupported == report.particleGpuDrawingSupported,
        "capability report did not expose the GPU particle instancing/blend gate");
    Require(report.nativePresentColor == bgfx::TextureFormat::BGRA8, "Capability report did not expose native presentation color format");
    Require(report.nativePresentDepth == bgfx::TextureFormat::Count, "Capability report should expose color-only native presentation");
    Require(report.nativePresentColorOnly, "Capability report did not classify native presentation as color-only");
    Require(RendererCapabilitySummary(report).size() > 0U, "Capability report summary is empty");

    bool foundIdentity = false;
    bool foundBloom = false;
    bool foundSelectionOutline = false;
    bool foundTonemap = false;
    for (const PostProcessPass& pass : renderer.PostProcessPasses()) {
        foundIdentity = foundIdentity || pass.kind == PostProcessPassKind::IdentityCopy;
        foundBloom = foundBloom || pass.kind == PostProcessPassKind::Bloom;
        foundSelectionOutline = foundSelectionOutline || pass.kind == PostProcessPassKind::SelectionOutline;
        foundTonemap = foundTonemap || pass.kind == PostProcessPassKind::Tonemap;
    }
    Require(foundIdentity, "Renderer default post-process chain is missing identity pass");
    Require(foundBloom, "Renderer default post-process chain is missing bloom pass");
    Require(foundSelectionOutline, "Renderer default post-process chain is missing selection outline pass");
    Require(foundTonemap, "Renderer default post-process chain is missing tonemap pass");

    renderer.Shutdown();
}

} // namespace

void RunRendererCapabilityReportTests() {
    PreferredBackendPolicyUsesProductionPriorityOrder();
    RendererExposesRuntimeCapabilityReport();
}

} // namespace kb::render::tests
