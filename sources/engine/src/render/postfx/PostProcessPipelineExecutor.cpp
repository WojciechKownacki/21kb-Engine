#include "render/postfx/PostProcessPipelineExecutor.hpp"

#include "render/postfx/PostProcessPipelineCompiler.hpp"

namespace kb::render::postfx {

void PostProcessPipelineExecutor::Execute(PostProcessPipelineState& state) {
    if (!state.compiled) {
        PostProcessPipelineCompiler::Compile(state);
    }

    PassContext ctx{ .resources = &state.resources.Resources() };

    for (const auto passIndex : state.graph.executionOrder) {
        state.compiledPasses[passIndex].execute(ctx);
    }
}

} // namespace kb::render::postfx
