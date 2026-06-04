#pragma once

#include "kb/render/frame/FinalCompositePass.hpp"
#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/post/PostProcessChain.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

class RendererFinalCompositeSubmitter final {
public:
    [[nodiscard]] static bool Submit(
        const FinalCompositePass& finalCompositePass,
        const RenderViewportPlan& viewportPlan,
        const RenderSceneSubmitDesc& desc,
        const PostProcessOutput& postProcessOutput,
        bgfx::TextureHandle scenePostProcessOutput);
};

} // namespace kb::render
