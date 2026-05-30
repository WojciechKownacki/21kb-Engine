#pragma once

#include "engine/render/postfx/PostProcessPipeline.hpp"
#include "render/postfx/PostProcessCompiledPass.hpp"
#include "render/postfx/PostProcessDependencyGraph.hpp"
#include "render/postfx/PostProcessGraphBuilder.hpp"
#include "render/postfx/PostProcessPassCompiler.hpp"
#include "render/postfx/PostProcessPassValidator.hpp"
#include "render/postfx/PostProcessResourceRegistry.hpp"

#include <vector>

namespace kb::render::postfx {

struct PostProcessPipelineState {
    PostProcessResourceRegistry resources;
    PostProcessPassValidator validator;
    PostProcessPassCompiler passCompiler;
    PostProcessGraphBuilder graphBuilder;
    std::vector<PostProcessPipeline::PassDesc> passes;
    std::vector<PostProcessCompiledPass> compiledPasses;
    PostProcessDependencyGraph graph;
    bool compiled = false;
};

} // namespace kb::render::postfx
