#pragma once

#include "kb/render/frame/RenderFrameDesc.hpp"
#include "kb/render/frame/RenderPassDesc.hpp"
#include "kb/render/frame/RenderPassGraph.hpp"
#include "kb/render/frame/RenderViewportViewIds.hpp"

#include <cstdint>
#include <vector>

namespace kb::render {

enum class RenderFramePipelineStatus : std::uint8_t {
    Success,
    EmptyFrame,
    InvalidViewport,
    ViewIdsUnavailable,
    InvalidPassGraph,
};

[[nodiscard]] const char* RenderFramePipelineStatusName(RenderFramePipelineStatus status) noexcept;

struct RenderViewportPlan {
    RenderViewportDesc viewport{};
    RenderViewportViewIds viewIds{};
    std::vector<RenderPassDesc> passes;
    std::vector<std::uint16_t> viewOrder;
    RenderPassGraphValidationResult graphValidation{};
    RenderPassGraphCompileResult graphCompile{};
};

struct RenderFramePlan {
    RenderFramePipelineStatus status = RenderFramePipelineStatus::EmptyFrame;
    std::vector<RenderViewportPlan> viewports;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == RenderFramePipelineStatus::Success;
    }
};

class RenderFramePipeline {
public:
    [[nodiscard]] RenderFramePlan Build(const RenderFrameDesc& desc) const;
    [[nodiscard]] RenderPassGraph BuildViewportGraph(const RenderViewportViewIds& viewIds) const;
    [[nodiscard]] RenderPassGraph BuildViewportGraph(const RenderViewportViewIds& viewIds, RenderExtent extent) const;
};

} // namespace kb::render
