#include "render/postfx/PostProcessPassCompiler.hpp"

namespace kb::render::postfx {

std::vector<PostProcessCompiledPass> PostProcessPassCompiler::Compile(const std::vector<PostProcessPipeline::PassDesc>& passes) const {
    std::vector<PostProcessCompiledPass> compiledPasses;
    compiledPasses.reserve(passes.size());

    for (const auto& pass : passes) {
        compiledPasses.push_back(PostProcessCompiledPass{
            .name = pass.name,
            .reads = pass.reads,
            .writes = pass.writes,
            .execute = pass.execute,
        });
    }

    return compiledPasses;
}

} // namespace kb::render::postfx
