#include "RendererTestSupport.hpp"

#include "kb/render/post/SceneDisplayCompositeRenderer.hpp"

namespace kb::render::tests {
namespace {

[[nodiscard]] bgfx::TextureHandle TextureHandleForTest(std::uint16_t index) noexcept {
    bgfx::TextureHandle handle{};
    handle.idx = index;
    return handle;
}

void DisplayCompositeRequiresHdrTextureAndExtent() {
    Require(!SceneDisplayCompositeDesc{}.IsValid(), "Default SceneDisplayCompositeDesc should be invalid");

    Require(SceneDisplayCompositeDesc{
        .hdrColor = TextureHandleForTest(12U),
        .extent = RenderExtent{1280U, 720U},
    }.IsValid(), "SceneDisplayCompositeDesc rejected a valid HDR texture and extent");
}

void DisplayCompositeDefaultsUseAcesDisplayTransform() {
    const SceneDisplayOutputTransform transform{};
    Require(NearlyEqual(transform.exposureStops, 0.0F), "Default display exposure should be neutral");
    Require(NearlyEqual(transform.gamma, 2.2F), "Default display gamma should target standard display output");
    Require(transform.tonemap == SceneDisplayTonemapOperator::Aces, "Default display tonemap should be ACES");
    Require(NearlyEqual(transform.colorGradingLutStrength, 0.0F), "Default color grade LUT strength should be neutral");
}

} // namespace

void RunSceneDisplayCompositeTests() {
    DisplayCompositeRequiresHdrTextureAndExtent();
    DisplayCompositeDefaultsUseAcesDisplayTransform();
}

} // namespace kb::render::tests
