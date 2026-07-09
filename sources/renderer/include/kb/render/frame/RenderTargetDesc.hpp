#pragma once

#include <cstdint>

namespace kb::render {

enum class RenderTargetFormat : std::uint8_t {
    Unknown,
    Backbuffer,
    Bgra8,
    Rgba8,
    Rg16F,
    Rgba16,
    Rgba16F,
    R8,
    D24S8,
    D32,
    D32F,
};

enum class RenderTargetRole : std::uint8_t {
    SceneColorHdr,
    SceneDepth,
    GBufferAlbedo,
    GBufferNormal,
    GBufferMaterial,
    GBufferSurface,
    GBufferDepth,
    SelectionMask,
    PostProcessColor,
    CompositeOutput,
};

struct RenderExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return width > 0U && height > 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(RenderExtent lhs, RenderExtent rhs) noexcept = default;
};

struct RenderViewportRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    RenderExtent extent{};

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return extent.IsValid();
    }

    [[nodiscard]] friend constexpr bool operator==(RenderViewportRect lhs, RenderViewportRect rhs) noexcept = default;
};

struct RenderTargetDesc {
    RenderTargetRole role = RenderTargetRole::SceneColorHdr;
    RenderTargetFormat format = RenderTargetFormat::Unknown;
    RenderExtent extent{};
    bool renderable = true;
    bool sampled = false;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return format != RenderTargetFormat::Unknown && extent.IsValid();
    }
};

} // namespace kb::render
