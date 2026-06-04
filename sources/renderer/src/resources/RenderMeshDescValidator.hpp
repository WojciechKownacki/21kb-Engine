#pragma once

#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

class RenderMeshDescValidator final {
public:
    [[nodiscard]] static bool IsValid(const RenderMeshDesc& desc) noexcept;
};

} // namespace kb::render
