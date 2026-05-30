#pragma once

#include "engine/render/postfx/PostProcessPipeline.hpp"

#include <unordered_map>

namespace kb::render::postfx {

class PostProcessPassValidator {
public:
    void Validate(const PostProcessPipeline::PassDesc& desc, const std::unordered_map<std::uint32_t, ResourceDesc>& resources) const;
};

} // namespace kb::render::postfx
