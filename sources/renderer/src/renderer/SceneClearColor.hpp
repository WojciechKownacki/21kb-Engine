#pragma once

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace kb::render {

// Scene colour targets are HDR (linear float): a packed RGBA8 clear would round a dark background
// to 1/255 steps of linear light (the camera's #0A0C16 came out as #141418). The clear goes through
// bgfx's float colour palette instead, one entry per viewport; entries 11..15 belong to the G-buffer
// clear (RendererViewConfigurator::ConfigureGBufferClear).
inline constexpr std::uint32_t kSceneClearPaletteEntries = 11U;

[[nodiscard]] inline std::uint8_t SceneClearPaletteIndex(std::uint32_t viewportIndex) noexcept {
    return static_cast<std::uint8_t>(viewportIndex % kSceneClearPaletteEntries);
}

[[nodiscard]] inline std::array<float, 3> UnpackLinearRgb(std::uint32_t rgba) noexcept {
    return { static_cast<float>((rgba >> 24U) & 0xFFU) / 255.0F,
             static_cast<float>((rgba >> 16U) & 0xFFU) / 255.0F,
             static_cast<float>((rgba >> 8U) & 0xFFU) / 255.0F };
}

// Sets the view's clear with the linear `color` (opaque); depth and stencil as given.
inline void SetSceneViewClear(bgfx::ViewId viewId,
                              std::uint16_t clearFlags,
                              const std::array<float, 3>& color,
                              float depth,
                              std::uint8_t stencil,
                              std::uint32_t viewportIndex) {
    if ((clearFlags & BGFX_CLEAR_COLOR) == 0U) {
        bgfx::setViewClear(viewId, clearFlags, 0U, depth, stencil);
        return;
    }
    const std::uint8_t paletteIndex = SceneClearPaletteIndex(viewportIndex);
    const std::array<float, 4> rgba{ color[0], color[1], color[2], 1.0F };
    bgfx::setPaletteColor(paletteIndex, rgba.data());
    bgfx::setViewClear(viewId, clearFlags, depth, stencil, paletteIndex);
}

} // namespace kb::render
