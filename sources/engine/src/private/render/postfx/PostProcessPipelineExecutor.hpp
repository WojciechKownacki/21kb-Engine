#pragma once

#include "render/postfx/PostProcessPipelineState.hpp"

namespace kb::render::postfx {

class PostProcessPipelineExecutor {
public:
    PostProcessPipelineExecutor() = delete;

    static void Execute(PostProcessPipelineState& state);
};

} // namespace kb::render::postfx
