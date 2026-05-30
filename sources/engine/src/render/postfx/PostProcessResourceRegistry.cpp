#include "render/postfx/PostProcessResourceRegistry.hpp"

#include <stdexcept>
#include <utility>

namespace kb::render::postfx {

ResourceHandle PostProcessResourceRegistry::Register(ResourceDesc desc) {
    if (desc.name.empty()) {
        throw std::invalid_argument("PostProcess resource name cannot be empty");
    }

    if (desc.texture.width == 0 || desc.texture.height == 0) {
        throw std::invalid_argument("PostProcess resource dimensions must be greater than zero");
    }

    const auto id = nextResourceId_++;
    resources_.emplace(id, std::move(desc));
    return ResourceHandle{ id };
}

const std::unordered_map<std::uint32_t, ResourceDesc>& PostProcessResourceRegistry::Resources() const noexcept {
    return resources_;
}

std::unordered_map<std::uint32_t, ResourceDesc>& PostProcessResourceRegistry::Resources() noexcept {
    return resources_;
}

} // namespace kb::render::postfx
