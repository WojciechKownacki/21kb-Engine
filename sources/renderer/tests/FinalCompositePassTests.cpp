#include "RendererTestSupport.hpp"

#include "kb/render/frame/FinalCompositePass.hpp"

namespace kb::render::tests {
namespace {

[[nodiscard]] bgfx::TextureHandle TextureHandleForTest(std::uint16_t index) noexcept {
    bgfx::TextureHandle handle{};
    handle.idx = index;
    return handle;
}

void FinalCompositeRequiresPostProcessColorAndExtent() {
    Require(!FinalCompositePassDesc{}.IsValid(), "Default FinalCompositePassDesc should be invalid");

    Require(FinalCompositePassDesc{
        .postProcessColor = TextureHandleForTest(9U),
        .extent = RenderExtent{640U, 480U},
    }.IsValid(), "FinalCompositePassDesc rejected a valid post-process color texture and extent");
}

void FinalCompositeDefaultsToDisplayTonemapTransform() {
    const FinalCompositePassDesc desc{
        .postProcessColor = TextureHandleForTest(10U),
        .extent = RenderExtent{1920U, 1080U},
    };
    Require(desc.IsValid(), "FinalCompositePassDesc default output transform made a valid desc invalid");
    Require(desc.outputTransform.tonemap == SceneDisplayTonemapOperator::Aces, "Final composite should default to ACES tonemap");
    Require(NearlyEqual(desc.outputTransform.gamma, 2.2F), "Final composite should default to display gamma 2.2");
}

void FullscreenTextureExposureResolverSupportsManualAndAutoExposure() {
    Require(NearlyEqual(ResolveFullscreenTextureExposureStops(FullscreenTextureOutputTransform{
                            .exposureStops = 1.5F,
                        }), 1.5F),
        "Manual fullscreen exposure should preserve configured stops");

    Require(NearlyEqual(ResolveFullscreenTextureExposureStops(FullscreenTextureOutputTransform{
                            .autoExposure = FullscreenTextureAutoExposureSettings{
                                .enabled = true,
                                .meteredAverageLuminance = 0.09F,
                                .middleGray = 0.18F,
                            },
                        }), 1.0F),
        "Auto exposure should brighten a scene one stop when metered luminance is half middle gray");

    Require(NearlyEqual(ResolveFullscreenTextureExposureStops(FullscreenTextureOutputTransform{
                            .autoExposure = FullscreenTextureAutoExposureSettings{
                                .enabled = true,
                                .meteredAverageLuminance = 18.0F,
                                .middleGray = 0.18F,
                                .minExposureStops = -2.0F,
                                .maxExposureStops = 2.0F,
                            },
                        }), -2.0F),
        "Auto exposure should clamp to minimum exposure stops");
}

} // namespace

void RunFinalCompositePassTests() {
    FinalCompositeRequiresPostProcessColorAndExtent();
    FinalCompositeDefaultsToDisplayTonemapTransform();
    FullscreenTextureExposureResolverSupportsManualAndAutoExposure();
}

} // namespace kb::render::tests
