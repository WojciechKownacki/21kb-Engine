#pragma once

#include "kb/render/resources/RenderResources.hpp"

namespace kb::render {

class RenderTextureResourceBuilder final {
public:
    [[nodiscard]] static bool IsValidDesc(const RenderTextureDesc& desc) noexcept;
    [[nodiscard]] static RenderTextureResource Build(const RenderTextureDesc& desc, bgfx::TextureHandle texture) noexcept;
};

} // namespace kb::render
