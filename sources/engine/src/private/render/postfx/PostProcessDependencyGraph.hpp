#pragma once

#include <cstdint>
#include <vector>

namespace kb::render::postfx {

struct PostProcessDependencyGraph {
    std::vector<std::vector<std::uint32_t>> edges;
    std::vector<std::uint32_t> executionOrder;
};

} // namespace kb::render::postfx
