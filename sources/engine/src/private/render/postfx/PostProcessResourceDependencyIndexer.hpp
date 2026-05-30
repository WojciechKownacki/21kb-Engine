#pragma once

#include "render/postfx/PostProcessCompiledPass.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kb::render::postfx {

struct PostProcessResourceDependencyIndex {
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> readers;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> writers;
};

class PostProcessResourceDependencyIndexer {
public:
    [[nodiscard]] PostProcessResourceDependencyIndex Build(const std::vector<PostProcessCompiledPass>& passes) const;
};

} // namespace kb::render::postfx
