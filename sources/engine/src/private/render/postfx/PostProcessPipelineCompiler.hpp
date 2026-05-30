#pragma once

#include "render/postfx/PostProcessPipelineState.hpp"

namespace kb::render::postfx {

class PostProcessPipelineCompiler {
public:
    PostProcessPipelineCompiler() = delete;

    static void Compile(PostProcessPipelineState& state);
};

} // namespace kb::render::postfx
