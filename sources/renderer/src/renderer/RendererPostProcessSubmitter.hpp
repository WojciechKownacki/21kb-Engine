#pragma once

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/post/PostProcessChain.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

class ScenePostProcessRenderer;

struct RendererPostProcessSubmitDesc {
    ScenePostProcessRenderer& postProcessRenderer;
    const RenderSceneSubmitDesc& sceneDesc;
    const RenderViewportPlan& viewportPlan;
    const PostProcessOutput& postProcessOutput;
    const SceneRenderCamera* sceneCamera = nullptr;
    const SceneRenderCamera* unjitteredSceneCamera = nullptr;
    std::array<float, 2> jitter{};
    std::uint64_t frameIndex = 0;
    RenderExtent& temporalExtent;
    std::array<float, 16>& previousViewProjection;
    std::array<float, 2>& previousJitter;
    bool& hasTemporalHistory;
};

class RendererPostProcessSubmitter final {
public:
    [[nodiscard]] static bgfx::TextureHandle Submit(const RendererPostProcessSubmitDesc& desc);
};

} // namespace kb::render
