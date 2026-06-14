#include "RendererTestSupport.hpp"

#include "kb/render/post/PostProcessChain.hpp"

namespace kb::render::tests {
namespace {

[[nodiscard]] bgfx::TextureHandle TextureHandleForTest(std::uint16_t index) noexcept {
    bgfx::TextureHandle handle{};
    handle.idx = index;
    return handle;
}

void EmptyPostProcessChainIsValidPassthrough() {
    PostProcessChain chain;
    const bgfx::TextureHandle sceneColor = TextureHandleForTest(42U);
    const bgfx::TextureHandle postColor = TextureHandleForTest(43U);
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = sceneColor,
        .outputFrameBuffer = bgfx::FrameBufferHandle{44U},
        .outputColor = postColor,
        .extent = RenderExtent{1280U, 720U},
        .viewId = 6U,
    });

    Require(output.IsValid(), "Empty PostProcessChain did not produce a valid passthrough output");
    Require(output.color.idx == postColor.idx, "Empty PostProcessChain did not route output through the post-process target");
    Require(output.passthrough, "Empty PostProcessChain did not mark passthrough output");
    Require(!output.gpuSubmitted, "PostProcessChain Evaluate should not submit GPU work");
    Require(output.sceneHdrPreserved, "Empty PostProcessChain did not preserve scene HDR");
    Require(output.colorSpace == PostProcessColorSpace::SceneHdr, "Empty PostProcessChain changed color space");
    Require(output.enabledPassCount == 0U, "Empty PostProcessChain reported enabled passes");
}

void DefaultPostProcessChainManifestIsComplete() {
    PostProcessChain chain;
    Require(chain.Configure(PostProcessChain::DefaultSceneChainDesc()), "PostProcessChain rejected the default scene manifest");
    Require(chain.Passes().size() == 5U, "Default post-process manifest has the wrong pass count");
    Require(chain.Passes()[0].kind == PostProcessPassKind::IdentityCopy, "Default post-process manifest should start with identity copy");
    Require(chain.Passes()[1].kind == PostProcessPassKind::AntiAliasing, "Default post-process manifest should resolve AA before bloom");
    Require(chain.Passes()[2].kind == PostProcessPassKind::Bloom, "Default post-process manifest should run bloom before overlays and tonemap");
    Require(chain.Passes()[3].kind == PostProcessPassKind::SelectionOutline, "Default post-process manifest should keep selection outline before tonemap");
    Require(chain.Passes()[4].kind == PostProcessPassKind::Tonemap, "Default post-process manifest should expose tonemap as the final controllable pass");
}

void PostProcessChainRejectsDuplicatePassKinds() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::Tonemap, .enabled = false}), "PostProcessChain rejected first tonemap pass");
    Require(!chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::Tonemap, .enabled = false}), "PostProcessChain accepted duplicate tonemap pass");
}

void PostProcessChainConfigureRejectsDuplicateKindsAtomically() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessChain::kDefaultIdentityPass), "PostProcessChain rejected initial identity pass");
    Require(!chain.Configure(PostProcessChainDesc{
        .passes = {
            PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = true},
            PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = false},
        },
    }), "PostProcessChain accepted a manifest with duplicate pass kinds");
    Require(chain.Passes().size() == 1U, "PostProcessChain mutated existing passes after rejected manifest");
    Require(chain.Passes()[0].kind == PostProcessPassKind::IdentityCopy, "PostProcessChain replaced existing passes after rejected manifest");
}

void PostProcessChainCanInsertAndTogglePasses() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessChain::kDefaultIdentityPass), "PostProcessChain rejected identity pass");
    Require(chain.InsertPass(1U, PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = true}), "PostProcessChain rejected bloom insertion");
    Require(chain.InsertPass(1U, PostProcessPass{.kind = PostProcessPassKind::Tonemap, .enabled = false}), "PostProcessChain rejected tonemap insertion");
    Require(chain.Passes()[1].kind == PostProcessPassKind::Tonemap, "PostProcessChain inserted tonemap at the wrong index");
    Require(chain.SetPassEnabled(PostProcessPassKind::Tonemap, true), "PostProcessChain did not toggle an existing pass");
    Require(chain.FindPass(PostProcessPassKind::Tonemap).has_value(), "PostProcessChain did not find an existing pass");
    Require(chain.FindPass(PostProcessPassKind::Tonemap)->enabled, "PostProcessChain did not persist a toggled pass");
    Require(!chain.InsertPass(4U, PostProcessPass{.kind = PostProcessPassKind::SelectionOutline, .enabled = true}), "PostProcessChain accepted insertion past end");
    Require(!chain.InsertPass(0U, PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = true}), "PostProcessChain accepted duplicate inserted pass");
}

void DisabledPassesDoNotAffectOutput() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = false}), "PostProcessChain rejected disabled bloom placeholder");
    const bgfx::TextureHandle sceneColor = TextureHandleForTest(7U);
    const bgfx::TextureHandle postColor = TextureHandleForTest(8U);
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = sceneColor,
        .outputFrameBuffer = bgfx::FrameBufferHandle{12U},
        .outputColor = postColor,
        .extent = RenderExtent{320U, 200U},
        .viewId = 6U,
    });
    Require(output.IsValid(), "PostProcessChain rejected a valid input with only disabled passes");
    Require(output.color.idx == postColor.idx, "Disabled PostProcessPass did not route through the post-process target");
    Require(output.enabledPassCount == 0U, "Disabled PostProcessPass was counted as enabled");
}

void BloomPassPublishesSettings() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessPass{
        .kind = PostProcessPassKind::Bloom,
        .enabled = true,
        .postProcessSettings = ScenePostProcessSettings{
            .bloomEnabled = true,
            .bloomStrength = 0.25F,
            .bloomThreshold = 1.5F,
            .bloomSoftKnee = 0.75F,
            .bloomRadiusPixels = 4.0F,
        },
    }), "PostProcessChain rejected configured bloom pass");
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = TextureHandleForTest(17U),
        .outputFrameBuffer = bgfx::FrameBufferHandle{18U},
        .outputColor = TextureHandleForTest(19U),
        .extent = RenderExtent{640U, 480U},
        .viewId = 6U,
    });

    Require(output.IsValid(), "Configured bloom pass did not produce a valid output");
    Require(output.bloomEnabled, "PostProcessChain did not publish enabled bloom state");
    Require(NearlyEqual(output.postProcessSettings.bloomStrength, 0.25F), "PostProcessChain did not publish bloom strength");
    Require(NearlyEqual(output.postProcessSettings.bloomThreshold, 1.5F), "PostProcessChain did not publish bloom threshold");
    Require(NearlyEqual(output.postProcessSettings.bloomSoftKnee, 0.75F), "PostProcessChain did not publish bloom soft knee");
    Require(NearlyEqual(output.postProcessSettings.bloomRadiusPixels, 4.0F), "PostProcessChain did not publish bloom radius");
}

void EnabledPlaceholderPassesAreValidHdrPassthrough() {
    PostProcessChain chain;
    Require(chain.Configure(PostProcessChain::DefaultSceneChainDesc()), "PostProcessChain rejected default enabled placeholder manifest");

    const bgfx::TextureHandle sceneColor = TextureHandleForTest(9U);
    const bgfx::TextureHandle postColor = TextureHandleForTest(12U);
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = sceneColor,
        .selectionMask = TextureHandleForTest(10U),
        .outputFrameBuffer = bgfx::FrameBufferHandle{11U},
        .outputColor = postColor,
        .extent = RenderExtent{1920U, 1080U},
        .viewId = 6U,
    });

    Require(output.IsValid(), "Enabled post-process placeholders did not produce a valid output");
    Require(output.color.idx == postColor.idx, "Enabled post-process placeholders did not route through the post-process target");
    Require(output.passthrough, "Enabled post-process placeholders did not report passthrough output");
    Require(output.sceneHdrPreserved, "Enabled post-process placeholders did not preserve scene HDR");
    Require(output.colorSpace == PostProcessColorSpace::SceneHdr, "Enabled post-process placeholders reported the wrong color space");
    Require(output.enabledPassCount == 5U, "PostProcessChain did not count enabled placeholder passes");
    Require(output.producer == PostProcessPassKind::Tonemap, "PostProcessChain did not track the last enabled producer");
    Require(output.tonemapEnabled, "PostProcessChain did not publish enabled tonemap state");
    Require(output.outputTransform.autoExposure.enabled, "Default tonemap pass did not enable auto exposure");
    Require(output.postProcessSettings.autoExposureMetering == ScenePostProcessSettings::AutoExposureMeteringMode::HdrColor, "Default tonemap pass did not publish HDR color auto exposure metering");
    Require(output.selectionOutlineEnabled, "PostProcessChain did not publish enabled selection outline state");
}

void AntiAliasingPassPublishesSettings() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessPass{
        .kind = PostProcessPassKind::AntiAliasing,
        .enabled = true,
        .postProcessSettings = ScenePostProcessSettings{
            .temporalAntiAliasingEnabled = false,
            .temporalJitterEnabled = false,
            .fxaaEnabled = true,
        },
    }), "PostProcessChain rejected configured anti-aliasing pass");
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = TextureHandleForTest(31U),
        .outputFrameBuffer = bgfx::FrameBufferHandle{32U},
        .outputColor = TextureHandleForTest(33U),
        .extent = RenderExtent{640U, 480U},
        .viewId = 6U,
    });

    Require(output.IsValid(), "Configured anti-aliasing pass did not produce a valid output");
    Require(output.fxaaEnabled, "PostProcessChain did not publish enabled FXAA state");
    Require(!output.temporalAntiAliasingEnabled, "PostProcessChain published TAA while FXAA is selected");
}

void SelectionOutlineRequiresSelectionMaskInput() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::SelectionOutline, .enabled = true}), "PostProcessChain rejected selection outline pass");
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = TextureHandleForTest(11U),
        .outputFrameBuffer = bgfx::FrameBufferHandle{12U},
        .outputColor = TextureHandleForTest(13U),
        .extent = RenderExtent{640U, 480U},
        .viewId = 6U,
    });

    Require(!output.IsValid(), "Selection outline post-process accepted missing selection mask input");
}

void DisabledSelectionOutlineDoesNotRequireSelectionMaskInput() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::SelectionOutline, .enabled = false}), "PostProcessChain rejected disabled selection outline pass");
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = TextureHandleForTest(21U),
        .outputFrameBuffer = bgfx::FrameBufferHandle{22U},
        .outputColor = TextureHandleForTest(23U),
        .extent = RenderExtent{640U, 480U},
        .viewId = 6U,
    });

    Require(output.IsValid(), "Disabled selection outline pass required a selection mask input");
    Require(!output.selectionOutlineEnabled, "Disabled selection outline pass published enabled state");
}

void PostProcessChainCanRemovePasses() {
    PostProcessChain chain;
    Require(chain.Configure(PostProcessChain::DefaultSceneChainDesc()), "PostProcessChain rejected default manifest before remove");
    Require(chain.RemovePass(PostProcessPassKind::Bloom), "PostProcessChain did not remove an existing pass");
    Require(!chain.FindPass(PostProcessPassKind::Bloom).has_value(), "PostProcessChain found a removed bloom pass");
    Require(!chain.RemovePass(PostProcessPassKind::Bloom), "PostProcessChain removed the same pass twice");
}

void PostProcessChainRequiresOutputTarget() {
    PostProcessChain chain;
    const PostProcessOutput output = chain.Evaluate(PostProcessInput{
        .sceneColor = TextureHandleForTest(15U),
        .extent = RenderExtent{640U, 480U},
        .viewId = 6U,
    });

    Require(!output.IsValid(), "PostProcessChain accepted missing output target");
}

void PostProcessPassKindNamesAreStable() {
    Require(PostProcessPassKindName(PostProcessPassKind::IdentityCopy)[0] != '\0', "IdentityCopy pass name is empty");
    Require(PostProcessPassKindName(PostProcessPassKind::AntiAliasing)[0] != '\0', "AntiAliasing pass name is empty");
    Require(PostProcessPassKindName(PostProcessPassKind::Tonemap)[0] != '\0', "Tonemap pass name is empty");
    Require(PostProcessPassKindName(PostProcessPassKind::Bloom)[0] != '\0', "Bloom pass name is empty");
    Require(PostProcessPassKindName(PostProcessPassKind::SelectionOutline)[0] != '\0', "SelectionOutline pass name is empty");
}

} // namespace

void RunPostProcessChainTests() {
    EmptyPostProcessChainIsValidPassthrough();
    DefaultPostProcessChainManifestIsComplete();
    PostProcessChainRejectsDuplicatePassKinds();
    PostProcessChainConfigureRejectsDuplicateKindsAtomically();
    PostProcessChainCanInsertAndTogglePasses();
    DisabledPassesDoNotAffectOutput();
    BloomPassPublishesSettings();
    EnabledPlaceholderPassesAreValidHdrPassthrough();
    AntiAliasingPassPublishesSettings();
    SelectionOutlineRequiresSelectionMaskInput();
    DisabledSelectionOutlineDoesNotRequireSelectionMaskInput();
    PostProcessChainCanRemovePasses();
    PostProcessChainRequiresOutputTarget();
    PostProcessPassKindNamesAreStable();
}

} // namespace kb::render::tests
