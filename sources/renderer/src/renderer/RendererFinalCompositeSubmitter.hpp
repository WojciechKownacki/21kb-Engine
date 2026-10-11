#pragma once

#include "kb/render/frame/FinalCompositePass.hpp"
#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderSceneSubmitDesc.hpp"
#include "kb/render/post/PostProcessChain.hpp"

#include <bgfx/bgfx.h>

namespace kb::render {

class RendererFinalCompositeSubmitter final {
public:
    // The transform that shows the post-processed scene: the profile's exposure, tone curve and
    // display encoding. Tonemapping off removes only the tone curve, as in Unity, Unreal and Godot.
    [[nodiscard]] static SceneDisplayOutputTransform DisplayTransform(const PostProcessOutput& postProcessOutput) noexcept;
    [[nodiscard]] static bool Submit(
        const FinalCompositePass& finalCompositePass,
        const RenderViewportPlan& viewportPlan,
        const RenderSceneSubmitDesc& desc,
        const PostProcessOutput& postProcessOutput,
        bgfx::TextureHandle scenePostProcessOutput);
};

} // namespace kb::render
