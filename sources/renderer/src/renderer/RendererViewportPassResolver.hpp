#pragma once

#include "kb/render/frame/RenderFramePipeline.hpp"
#include "kb/render/frame/RenderPassKind.hpp"
#include "kb/render/scene/MeshPipeline.hpp"

namespace kb::render {

class RendererViewportPassResolver {
public:
    [[nodiscard]] static MeshPassType MeshPassFor(const RenderViewportPlan& viewportPlan, RenderPassKind kind, MeshPassType fallback) noexcept;
};

} // namespace kb::render
