#pragma once

#include "kb/render/frame/RenderPassGraph.hpp"

namespace kb::render {

class RenderPassGraphValidator final {
public:
    [[nodiscard]] static RenderPassGraphValidationResult Validate(
        std::span<const RenderGraphResourceDesc> resources,
        std::span<const RenderPassDesc> passes) noexcept;
};

} // namespace kb::render
