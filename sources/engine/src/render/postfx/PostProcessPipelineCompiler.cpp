#include "render/postfx/PostProcessPipelineCompiler.hpp"

#include <stdexcept>

namespace kb::render::postfx {

void PostProcessPipelineCompiler::Compile(PostProcessPipelineState& state) {
    if (state.passes.empty()) {
        throw std::runtime_error("PostProcess pipeline has no passes");
    }

    state.compiledPasses = state.passCompiler.Compile(state.passes);
    state.graph = state.graphBuilder.Build(state.compiledPasses);
    state.compiled = true;
}

} // namespace kb::render::postfx
