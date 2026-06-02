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

} // namespace

void RunFinalCompositePassTests() {
    FinalCompositeRequiresPostProcessColorAndExtent();
    FinalCompositeDefaultsToDisplayTonemapTransform();
}

} // namespace kb::render::tests
