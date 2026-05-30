#pragma once

#include "render/postfx/PostProcessDependencyGraph.hpp"

#include <cstdint>
#include <vector>

namespace kb::render::postfx {

class PostProcessTopologicalSorter {
public:
    [[nodiscard]] std::vector<std::uint32_t> Sort(const PostProcessDependencyGraph& graph) const;

private:
    [[nodiscard]] static std::vector<std::uint32_t> CalculateIndegree(const PostProcessDependencyGraph& graph);
};

} // namespace kb::render::postfx
