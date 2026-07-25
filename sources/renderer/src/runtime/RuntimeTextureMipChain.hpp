#pragma once

#include "kb/render/resources/RenderResources.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace kb::render {

struct RuntimeTextureMipChain {
    std::vector<std::uint8_t> rgba8;
    std::uint8_t mipCount = 1U;
};

[[nodiscard]] std::optional<RuntimeTextureMipChain> BuildRuntimeTexture2DMipChain(
    std::span<const std::uint8_t> baseLevelRgba8,
    std::uint16_t width,
    std::uint16_t height,
    RenderTextureColorSpace colorSpace);

} // namespace kb::render
