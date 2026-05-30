#pragma once

#include "engine/render/postfx/PostProcessPipeline.hpp"

#include <cstdint>
#include <unordered_map>

namespace kb::render::postfx {

class PostProcessResourceRegistry {
public:
    [[nodiscard]] ResourceHandle Register(ResourceDesc desc);
    [[nodiscard]] const std::unordered_map<std::uint32_t, ResourceDesc>& Resources() const noexcept;
    [[nodiscard]] std::unordered_map<std::uint32_t, ResourceDesc>& Resources() noexcept;

private:
    std::uint32_t nextResourceId_ = 1;
    std::unordered_map<std::uint32_t, ResourceDesc> resources_;
};

} // namespace kb::render::postfx
