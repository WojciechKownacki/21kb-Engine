#include "renderer/RendererViewConfigurator.hpp"

#include "kb/render/SceneDepthPolicy.hpp"
#include "renderer/RendererMatrixMath.hpp"

namespace kb::render {
namespace {

[[nodiscard]] std::uint16_t ClampToViewExtent(std::uint32_t value) noexcept {
    return static_cast<std::uint16_t>(value > UINT16_MAX ? UINT16_MAX : value);
}

} // namespace

void RendererViewConfigurator::ApplyViewOrder(std::span<const std::uint16_t> viewOrder) {
    if (!viewOrder.empty()) {
        bgfx::setViewOrder(0U, static_cast<std::uint16_t>(viewOrder.size()), viewOrder.data());
    }
}

void RendererViewConfigurator::ConfigureSceneClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc) {
    const std::array<float, 16> identity = RendererMatrixMath::Identity();
    const std::uint16_t width = ClampToViewExtent(desc.target.viewport.extent.width);
    const std::uint16_t height = ClampToViewExtent(desc.target.viewport.extent.height);

    bgfx::setViewName(viewId, "KB Scene Target");
    bgfx::setViewFrameBuffer(viewId, desc.target.frameBuffer);
    bgfx::setViewTransform(viewId, identity.data(), identity.data());
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL, desc.clearRgba, desc.clearDepth, desc.clearStencil);
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

void RendererViewConfigurator::ConfigureShadowDepth(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, std::uint32_t size) {
    const std::uint16_t extent = ClampToViewExtent(size);
    bgfx::setViewName(viewId, "KB Shadow Depth");
    bgfx::setViewFrameBuffer(viewId, frameBuffer);
    bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0U, SceneDepthPolicy::ClearDepth(), 0U);
    bgfx::setViewRect(viewId, 0, 0, extent, extent);
    bgfx::touch(viewId);
}

} // namespace kb::render
