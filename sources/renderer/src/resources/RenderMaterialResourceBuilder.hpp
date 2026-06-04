#pragma once

#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

class RenderMaterialResourceBuilder final {
public:
    [[nodiscard]] static RenderMaterialResource Build(const RenderMaterialDesc& desc) noexcept;
};

} // namespace kb::render
