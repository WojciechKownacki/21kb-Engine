#pragma once

#include "kb/render/frame/RenderSceneSubmitDesc.hpp"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <span>

namespace kb::render {

class RendererViewConfigurator {
public:
    static void ApplyViewOrder(std::span<const std::uint16_t> viewOrder);
    static void ConfigureSceneClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc);
    static void ConfigureSceneNoClear(bgfx::ViewId viewId, const RenderSceneSubmitDesc& desc, const char* name);
    static void ConfigureShadowDepth(bgfx::ViewId viewId, bgfx::FrameBufferHandle frameBuffer, std::uint32_t size);
};

} // namespace kb::render
