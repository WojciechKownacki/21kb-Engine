#pragma once

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/post/PostProcessChain.hpp"
#include "kb/render/post/SceneExposureMeter.hpp"
#include "kb/render/scene/RenderScene.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstdint>

namespace kb::render {

class RendererExposureSubmitter {
public:
    [[nodiscard]] static SceneRenderExposureSubmitStats Submit(
        SceneExposureMeter& exposureMeter,
        PostProcessOutput& postProcessOutput,
        const RenderSceneSubmitDesc& desc,
        const RenderViewportPlan& viewportPlan,
        const RenderScene& renderScene,
        const SceneRenderLightingConfig& lightingConfig,
        std::uint32_t lastCompletedFrame);
};

} // namespace kb::render
