#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/ViewIdPolicy.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

class RendererViewConfigurator {
public:
    static void ApplyViewOrder(
        const std::array<std::uint16_t, ViewId::Max>& viewRemap);
    static void ConfigureSceneClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc, std::uint16_t clearFlags, std::uint32_t clearRgba);
    static void ConfigureSceneNoClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc, const char* name);
    static void ConfigureFramebufferClear(
        bgfx::ViewId viewId,
        bgfx::FrameBufferHandle frameBuffer,
        RenderExtent extent,
        const char* name,
        std::uint16_t clearFlags,
        std::uint32_t rgba,
        float depth,
        std::uint8_t stencil);
    static void ConfigureGBufferClear(
        bgfx::ViewId viewId,
        bgfx::FrameBufferHandle frameBuffer,
        RenderExtent extent,
        float depth,
        std::uint8_t stencil);
    static void ConfigureFramebufferNoClear(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, RenderExtent extent, const char* name);
    static void ConfigureShadowDepth(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, std::uint32_t size);
};

} // namespace kb::render
