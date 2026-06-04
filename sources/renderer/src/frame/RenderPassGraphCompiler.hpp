#pragma once

#include "kb/render/frame/RenderPassGraph.hpp"

namespace kb::render {

class RenderPassGraphCompiler final {
public:
    [[nodiscard]] static RenderPassGraphCompileResult Compile(
        std::span<const RenderGraphResourceDesc> resources,
        std::span<const RenderPassDesc> passes,
        RenderPassGraphValidationResult validation);
};

} // namespace kb::render
