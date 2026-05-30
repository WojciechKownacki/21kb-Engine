#include "render/postfx/PostProcessResourceDependencyIndexer.hpp"

namespace kb::render::postfx {

PostProcessResourceDependencyIndex PostProcessResourceDependencyIndexer::Build(const std::vector<PostProcessCompiledPass>& passes) const {
    PostProcessResourceDependencyIndex index{};

    for (std::uint32_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
        for (const auto read : passes[passIndex].reads) {
            index.readers[read.id].push_back(passIndex);
        }
        for (const auto write : passes[passIndex].writes) {
            index.writers[write.id].push_back(passIndex);
        }
    }

    return index;
}

} // namespace kb::render::postfx
