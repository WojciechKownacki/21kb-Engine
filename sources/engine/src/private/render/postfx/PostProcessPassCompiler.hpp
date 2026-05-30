#pragma once

#include "render/postfx/PostProcessCompiledPass.hpp"

#include <vector>

namespace kb::render::postfx {

class PostProcessPassCompiler {
public:
    [[nodiscard]] std::vector<PostProcessCompiledPass> Compile(const std::vector<PostProcessPipeline::PassDesc>& passes) const;
};

} // namespace kb::render::postfx
