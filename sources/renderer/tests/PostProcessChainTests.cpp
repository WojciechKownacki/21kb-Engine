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

void PostProcessChainRejectsDuplicatePassKinds() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::Tonemap, .enabled = false}), "PostProcessChain rejected first tonemap pass");
    Require(!chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::Tonemap, .enabled = false}), "PostProcessChain accepted duplicate tonemap pass");
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

void EnabledPlaceholderPassesAreValidHdrPassthrough() {
    PostProcessChain chain;
    Require(chain.AddPass(PostProcessChain::kDefaultIdentityPass), "PostProcessChain rejected default identity pass");
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::Tonemap, .enabled = true}), "PostProcessChain rejected enabled tonemap placeholder");
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::Bloom, .enabled = true}), "PostProcessChain rejected enabled bloom placeholder");
    Require(chain.AddPass(PostProcessPass{.kind = PostProcessPassKind::SelectionOutline, .enabled = true}), "PostProcessChain rejected enabled selection outline placeholder");

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
    Require(output.enabledPassCount == 4U, "PostProcessChain did not count enabled placeholder passes");
    Require(output.producer == PostProcessPassKind::SelectionOutline, "PostProcessChain did not track the last enabled producer");
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
    Require(PostProcessPassKindName(PostProcessPassKind::Tonemap)[0] != '\0', "Tonemap pass name is empty");
    Require(PostProcessPassKindName(PostProcessPassKind::Bloom)[0] != '\0', "Bloom pass name is empty");
    Require(PostProcessPassKindName(PostProcessPassKind::SelectionOutline)[0] != '\0', "SelectionOutline pass name is empty");
}

} // namespace

void RunPostProcessChainTests() {
    EmptyPostProcessChainIsValidPassthrough();
    PostProcessChainRejectsDuplicatePassKinds();
    DisabledPassesDoNotAffectOutput();
    EnabledPlaceholderPassesAreValidHdrPassthrough();
    SelectionOutlineRequiresSelectionMaskInput();
    PostProcessChainRequiresOutputTarget();
    PostProcessPassKindNamesAreStable();
}

} // namespace kb::render::tests
