#pragma once

#include "engine/render/postfx/PostProcessPipeline.hpp"

#include <string>
#include <vector>

namespace kb::render::postfx {

struct PostProcessCompiledPass {
    std::string name;
    std::vector<ResourceHandle> reads;
    std::vector<ResourceHandle> writes;
    PostProcessPipeline::ExecuteFn execute;
};

} // namespace kb::render::postfx
