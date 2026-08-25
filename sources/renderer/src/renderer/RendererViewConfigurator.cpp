#include "renderer/RendererViewConfigurator.hpp"

#include "kb/render/SceneGBufferContract.hpp"
#include "kb/render/SceneDepthPolicy.hpp"
#include "renderer/RendererMatrixMath.hpp"

namespace kb::render {
namespace {

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

} // namespace

void RendererViewConfigurator::ApplyViewOrder(
    const std::array<std::uint16_t, ViewId::Max>& viewRemap) {
    bgfx::setViewOrder(
        0U,
        static_cast<std::uint16_t>(viewRemap.size()),
        viewRemap.data());
}

void RendererViewConfigurator::ConfigureSceneClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc, std::uint16_t clearFlags, std::uint32_t clearRgba) {
    const std::array<float, 16> identity = RendererMatrixMath::Identity();
    const std::uint16_t width = ClampToViewExtent(desc.target.viewport.extent.width);
    const std::uint16_t height = ClampToViewExtent(desc.target.viewport.extent.height);

    bgfx::setViewName(viewId, "KB Scene Target");
    bgfx::setViewFrameBuffer(viewId, desc.target.frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, clearFlags, clearRgba, desc.clearDepth, desc.clearStencil);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::touch(viewId);
}

void RendererViewConfigurator::ConfigureSceneNoClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc, const char* name) {
    const std::array<float, 16> identity = RendererMatrixMath::Identity();
    const std::uint16_t width = ClampToViewExtent(desc.target.viewport.extent.width);
    const std::uint16_t height = ClampToViewExtent(desc.target.viewport.extent.height);

    bgfx::setViewName(viewId, name);
    bgfx::setViewFrameBuffer(viewId, desc.target.frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewRect(viewId, 0, 0, width, height);
}

void RendererViewConfigurator::ConfigureFramebufferClear(
    bgfx::ViewId viewId,
    bgfx::FrameBufferHandle frameBuffer,
    RenderExtent extent,
    const char* name,
    std::uint16_t clearFlags,
    std::uint32_t rgba,
    float depth,
    std::uint8_t stencil) {
    const std::array<float, 16> identity = RendererMatrixMath::Identity();
    const std::uint16_t width = ClampToViewExtent(extent.width);
    const std::uint16_t height = ClampToViewExtent(extent.height);

    bgfx::setViewName(viewId, name == nullptr ? "KB Framebuffer Clear" : name);
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, clearFlags, rgba, depth, stencil);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::touch(viewId);
}

void RendererViewConfigurator::ConfigureGBufferClear(
    bgfx::ViewId viewId,
    bgfx::FrameBufferHandle frameBuffer,
    RenderExtent extent,
    float depth,
    std::uint8_t stencil) {
    constexpr std::array<std::uint8_t, kSceneGBufferColorAttachmentCount> paletteIndices{ 12U, 13U, 14U, 15U };
    const std::array<float, 16> identity = RendererMatrixMath::Identity();
    const std::uint16_t width = ClampToViewExtent(extent.width);
    const std::uint16_t height = ClampToViewExtent(extent.height);

    for (std::size_t index = 0U; index < kSceneGBufferClearColors.size(); ++index) {
        const std::array<float, 4U> clear = kSceneGBufferClearColors[index].ToArray();
        bgfx::setPaletteColor(paletteIndices[index], clear.data());
    }

    bgfx::setViewName(viewId, "KB GBuffer Geometry");
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(
        viewId,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL,
        depth,
        stencil,
        paletteIndices[0],
        paletteIndices[1],
        paletteIndices[2],
        paletteIndices[3]);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::touch(viewId);
}

void RendererViewConfigurator::ConfigureFramebufferNoClear(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, RenderExtent extent, const char* name) {
    const std::array<float, 16> identity = RendererMatrixMath::Identity();
    const std::uint16_t width = ClampToViewExtent(extent.width);
    const std::uint16_t height = ClampToViewExtent(extent.height);

    bgfx::setViewName(viewId, name == nullptr ? "KB Framebuffer" : name);
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewRect(viewId, 0, 0, width, height);
}

void RendererViewConfigurator::ConfigureShadowDepth(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, std::uint32_t size) {
    const std::uint16_t extent = ClampToViewExtent(size);
    bgfx::setViewName(viewId, "KB Shadow Depth");
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0U, SceneDepthPolicy::ClearDepth(), 0U);
    bgfx::setViewRect(viewId, 0, 0, extent, extent);
    bgfx::touch(viewId);
}

} // namespace kb::render
