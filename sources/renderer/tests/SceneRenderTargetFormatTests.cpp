#include "RendererTestSupport.hpp"

#include "kb/render/SceneGBuffer.hpp"
#include "kb/render/SceneGBufferContract.hpp"
#include "kb/render/SceneRenderTarget.hpp"
#include "kb/render/SceneRenderTargetFormat.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/post/ScenePostProcessTargets.hpp"
#include "kb/render/resources/NativeWindowFramebuffer.hpp"

#include <string_view>

namespace kb::render::tests {
namespace {

void SceneColorPolicyMapsToExplicitFormats() {
    Require(SceneColorFormatForPolicy(SceneColorFormatPolicy::Rgba16F) == bgfx::TextureFormat::RGBA16F, "RGBA16F policy mapped to the wrong format");
    Require(SceneColorFormatForPolicy(SceneColorFormatPolicy::Rgba16) == bgfx::TextureFormat::RGBA16, "RGBA16 policy mapped to the wrong format");
    Require(SceneColorFormatForPolicy(SceneColorFormatPolicy::Rgba8) == bgfx::TextureFormat::RGBA8, "RGBA8 policy mapped to the wrong format");
    Require(SceneColorFormatForPolicy(SceneColorFormatPolicy::Auto) == bgfx::TextureFormat::Count, "Auto policy should not map to a fixed format");
}

void SceneColorHdrClassificationIsStrict() {
    Require(SceneColorFormatIsHdr(bgfx::TextureFormat::RGBA16F), "RGBA16F should be classified as HDR");
    Require(!SceneColorFormatIsHdr(bgfx::TextureFormat::RGBA16), "RGBA16 should be an explicit non-HDR fallback");
    Require(!SceneColorFormatIsHdr(bgfx::TextureFormat::RGBA8), "RGBA8 should be an explicit non-HDR fallback");
    Require(!SceneColorFormatIsHdr(bgfx::TextureFormat::BGRA8), "BGRA8 should not be accepted as scene HDR");
}

void SceneDepthPreferredClassificationIsStrict() {
    SceneDepthFormatSelection d32f{
        .format = bgfx::TextureFormat::D32F,
        .status = SceneTargetFormatSelectionStatus::Selected,
    };
    SceneDepthFormatSelection d32{
        .format = bgfx::TextureFormat::D32,
        .status = SceneTargetFormatSelectionStatus::Selected,
    };
    SceneDepthFormatSelection d24s8{
        .format = bgfx::TextureFormat::D24S8,
        .status = SceneTargetFormatSelectionStatus::CapabilityFallback,
    };

    Require(d32f.IsPreferred(), "D32F should be a preferred scene depth format");
    Require(d32.IsPreferred(), "D32 should be a preferred scene depth format");
    Require(!d24s8.IsPreferred(), "D24S8 should be an explicit fallback depth format");
}

void SceneTargetFormatNamesAreExplicit() {
    Require(SceneTextureFormatName(bgfx::TextureFormat::RGBA16F) == std::string_view{"RGBA16F"}, "RGBA16F format name is unstable");
    Require(SceneTextureFormatName(bgfx::TextureFormat::RGBA16) == std::string_view{"RGBA16"}, "RGBA16 format name is unstable");
    Require(SceneTextureFormatName(bgfx::TextureFormat::RGBA8) == std::string_view{"RGBA8"}, "RGBA8 format name is unstable");
    Require(SceneTextureFormatName(bgfx::TextureFormat::BGRA8) == std::string_view{"BGRA8"}, "BGRA8 format name is unstable");
    Require(SceneTextureFormatName(bgfx::TextureFormat::RG16F) == std::string_view{"RG16F"}, "RG16F format name is unstable");
    Require(SceneTextureFormatName(bgfx::TextureFormat::D32F) == std::string_view{"D32F"}, "D32F format name is unstable");
    Require(SceneTextureFormatName(bgfx::TextureFormat::D32) == std::string_view{"D32"}, "D32 format name is unstable");
    Require(SceneTextureFormatName(bgfx::TextureFormat::D24S8) == std::string_view{"D24S8"}, "D24S8 format name is unstable");
}

void SceneRenderTargetDescRequiresValidExtent() {
    Require(!SceneRenderTargetDesc{}.IsValid(), "Default SceneRenderTargetDesc should be invalid");
    Require(SceneRenderTargetDesc{.extent = RenderExtent{1U, 1U}}.IsValid(), "SceneRenderTargetDesc rejected a valid extent");
    Require(SceneRenderTargetDesc{.extent = RenderExtent{1U, 1U}, .msaaSamples = 2U}.IsValid(), "SceneRenderTargetDesc rejected 2x MSAA");
    Require(SceneRenderTargetDesc{.extent = RenderExtent{1U, 1U}, .msaaSamples = 4U}.IsValid(), "SceneRenderTargetDesc rejected 4x MSAA");
    Require(!SceneRenderTargetDesc{.extent = RenderExtent{1U, 1U}, .msaaSamples = 3U}.IsValid(), "SceneRenderTargetDesc accepted unsupported MSAA sample count");
}

void SceneGBufferDescRequiresValidExtent() {
    Require(!SceneGBufferDesc{}.IsValid(), "Default SceneGBufferDesc should be invalid");
    Require(SceneGBufferDesc{.extent = RenderExtent{1U, 1U}}.IsValid(), "SceneGBufferDesc rejected a valid extent");
}

void SceneGBufferFormatSelectionRequiresEveryAttachment() {
    SceneGBufferFormatSelection selection{
        .albedoFormat = bgfx::TextureFormat::BGRA8,
        .normalFormat = bgfx::TextureFormat::RGBA16F,
        .materialFormat = bgfx::TextureFormat::RGBA8,
        .surfaceFormat = bgfx::TextureFormat::RGBA16F,
        .depth = SceneDepthFormatSelection{
            .format = bgfx::TextureFormat::D32F,
            .status = SceneTargetFormatSelectionStatus::Selected,
        },
        .status = SceneTargetFormatSelectionStatus::Selected,
    };
    Require(selection.IsSupported(), "Complete GBuffer format selection should be supported");
    selection.materialFormat = bgfx::TextureFormat::Count;
    Require(!selection.IsSupported(), "GBuffer format selection accepted a missing material attachment");
    selection.materialFormat = bgfx::TextureFormat::RGBA8;
    selection.surfaceFormat = bgfx::TextureFormat::Count;
    Require(!selection.IsSupported(), "P0.6: GBuffer format selection accepted a missing HDR surface attachment");

    Require(kSceneGBufferColorAttachmentCount == 4U,
        "P0.6: GBuffer contract must expose four color attachments");
    Require(EncodeSceneGBufferShadingModel(SceneGBufferShadingModelId::Unlit) == 0.0F &&
            EncodeSceneGBufferShadingModel(SceneGBufferShadingModelId::DefaultLit) > 0.0F &&
            kSceneGBufferClearColors[3].alpha == 0.5F,
        "P0.6: stable shading-model ids and neutral surface clear do not match the deferred shader contract");
}

void RenderTargetDescSupportsSceneFallbackFormats() {
    Require(RenderTargetDesc{.format = RenderTargetFormat::Bgra8, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected BGRA8 format");
    Require(RenderTargetDesc{.format = RenderTargetFormat::Rgba16, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected RGBA16 fallback format");
    Require(RenderTargetDesc{.format = RenderTargetFormat::Rgba16F, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected RGBA16F HDR format");
    Require(RenderTargetDesc{.format = RenderTargetFormat::Rg16F, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected RG16F GBuffer format");
    Require(RenderTargetDesc{.format = RenderTargetFormat::Rgba8, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected RGBA8 fallback format");
    Require(RenderTargetDesc{.format = RenderTargetFormat::D32, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected D32 fallback depth format");
    Require(RenderTargetDesc{.format = RenderTargetFormat::D32F, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected D32F depth format");
    Require(RenderTargetDesc{.format = RenderTargetFormat::D24S8, .extent = RenderExtent{1U, 1U}}.IsValid(), "RenderTargetDesc rejected D24S8 fallback depth format");
}

void SceneSubmitDescRequiresValidFinalCompositeExtentWhenEnabled() {
    RenderSceneSubmitDesc desc{};
    desc.target.viewport = RenderViewportDesc{
        .id = RenderViewportId{1U},
        .extent = RenderExtent{320U, 200U},
        .viewportIndex = 0U,
    };
    Require(desc.IsValid(), "RenderSceneSubmitDesc rejected a valid scene target without final composite");

    desc.finalComposite.enabled = true;
    Require(!desc.IsValid(), "RenderSceneSubmitDesc accepted enabled final composite without extent");

    desc.finalComposite.extent = RenderExtent{320U, 200U};
    Require(!desc.IsValid(), "RenderSceneSubmitDesc accepted enabled final composite without post-process target");

    desc.postProcessEnabled = false;
    Require(desc.IsValid(), "RenderSceneSubmitDesc rejected direct final composite when post-process is disabled");
    desc.postProcessEnabled = true;

    desc.postProcess = RenderPostProcessTargetBinding{
        .selectionMaskFrameBuffer = bgfx::FrameBufferHandle{1U},
        .selectionMaskTexture = bgfx::TextureHandle{2U},
        .bloomFrameBuffer = bgfx::FrameBufferHandle{3U},
        .bloomTexture = bgfx::TextureHandle{4U},
        .pingFrameBuffer = bgfx::FrameBufferHandle{5U},
        .pingTexture = bgfx::TextureHandle{6U},
        .motionVectorFrameBuffer = bgfx::FrameBufferHandle{11U},
        .motionVectorTexture = bgfx::TextureHandle{12U},
        .temporalHistoryFrameBuffer = bgfx::FrameBufferHandle{13U},
        .temporalHistoryTexture = bgfx::TextureHandle{14U},
        .previousTemporalHistoryTexture = bgfx::TextureHandle{16U},
        .temporalHistoryFrameBuffers = {{
            bgfx::FrameBufferHandle{13U},
            bgfx::FrameBufferHandle{15U},
        }},
        .temporalHistoryTextures = {{
            bgfx::TextureHandle{14U},
            bgfx::TextureHandle{16U},
        }},
        .combineFrameBuffer = bgfx::FrameBufferHandle{7U},
        .combineTexture = bgfx::TextureHandle{8U},
        .finalFrameBuffer = bgfx::FrameBufferHandle{9U},
        .finalTexture = bgfx::TextureHandle{10U},
        .bloomMipFrameBuffers = {{
            bgfx::FrameBufferHandle{3U},
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
        }},
        .pingMipFrameBuffers = {{
            bgfx::FrameBufferHandle{5U},
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE,
        }},
        .bloomMipExtents = {{
            RenderExtent{320U, 200U},
            RenderExtent{},
            RenderExtent{},
            RenderExtent{},
            RenderExtent{},
            RenderExtent{},
        }},
        .bloomMipCount = 1U,
        .extent = RenderExtent{320U, 200U},
        .enabled = true,
    };
    Require(desc.IsValid(), "RenderSceneSubmitDesc rejected enabled final composite with valid post-process target");
}

void SceneSubmitDescUsesExplicitOverlayDepthWhenPresent() {
    RenderSceneSubmitDesc desc{};
    desc.target.depthTexture = bgfx::TextureHandle{17U};

    Require(desc.SceneOverlayDepthTexture().idx == 17U, "Scene overlay depth should default to the scene target depth texture");

    desc.editorOverlayDepthTexture = bgfx::TextureHandle{23U};
    Require(desc.SceneOverlayDepthTexture().idx == 23U, "Scene overlay depth should prefer the explicit editor overlay depth texture");
}

void DefaultPostProcessTargetsBindingIsDisabledAndInvalid() {
    const ScenePostProcessTargets targets;
    const RenderPostProcessTargetBinding binding = targets.Binding();

    Require(!binding.enabled, "Default post-process binding should be disabled");
    Require(binding.IsValid(), "Disabled post-process binding should be valid");
    Require(!bgfx::isValid(binding.selectionMaskFrameBuffer), "Default selection mask framebuffer should be invalid");
    Require(!bgfx::isValid(binding.selectionMaskTexture), "Default selection mask texture should be invalid");
    Require(!bgfx::isValid(binding.finalFrameBuffer), "Default final framebuffer should be invalid");
    Require(!bgfx::isValid(binding.finalTexture), "Default final texture should be invalid");
}

void NativeWindowFramebufferDescDefaultsToColorOnlyPresentation() {
    Require(NativeWindowFramebufferDesc{
        .nativeWindow = reinterpret_cast<void*>(0x1),
        .width = 128U,
        .height = 64U,
    }.IsValid(), "NativeWindowFramebufferDesc rejected default color-only presentation");

    const NativeWindowFramebufferDesc desc{
        .nativeWindow = reinterpret_cast<void*>(0x1),
        .width = 128U,
        .height = 64U,
    };
    Require(desc.colorFormat == bgfx::TextureFormat::BGRA8, "Native presentation should default to BGRA8 color");
    Require(desc.depthFormat == bgfx::TextureFormat::Count, "Native presentation should not allocate depth by default");

    Require(!NativeWindowFramebufferDesc{.width = 128U, .height = 64U}.IsValid(), "Native presentation accepted a null window");
    Require(!NativeWindowFramebufferDesc{.nativeWindow = reinterpret_cast<void*>(0x1), .width = 0U, .height = 64U}.IsValid(), "Native presentation accepted a zero width");
    Require(!NativeWindowFramebufferDesc{
        .nativeWindow = reinterpret_cast<void*>(0x1),
        .width = 128U,
        .height = 64U,
        .colorFormat = bgfx::TextureFormat::Count,
    }.IsValid(), "Native presentation accepted an invalid color format");
}

} // namespace

void RunSceneRenderTargetFormatTests() {
    SceneColorPolicyMapsToExplicitFormats();
    SceneColorHdrClassificationIsStrict();
    SceneDepthPreferredClassificationIsStrict();
    SceneTargetFormatNamesAreExplicit();
    SceneRenderTargetDescRequiresValidExtent();
    SceneGBufferDescRequiresValidExtent();
    SceneGBufferFormatSelectionRequiresEveryAttachment();
    RenderTargetDescSupportsSceneFallbackFormats();
    SceneSubmitDescRequiresValidFinalCompositeExtentWhenEnabled();
    SceneSubmitDescUsesExplicitOverlayDepthWhenPresent();
    DefaultPostProcessTargetsBindingIsDisabledAndInvalid();
    NativeWindowFramebufferDescDefaultsToColorOnlyPresentation();
}

} // namespace kb::render::tests
