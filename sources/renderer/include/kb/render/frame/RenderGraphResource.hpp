#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"

#include <cstdint>

namespace kb::render {

struct RenderGraphResourceId {
    std::uint16_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(RenderGraphResourceId lhs, RenderGraphResourceId rhs) noexcept = default;
};

namespace RenderGraphResource {

constexpr RenderGraphResourceId SceneColor{ 1U };
constexpr RenderGraphResourceId SceneDepth{ 2U };
constexpr RenderGraphResourceId ShadowMap{ 3U };
constexpr RenderGraphResourceId SelectionMask{ 4U };
constexpr RenderGraphResourceId ExposureReadback{ 5U };
constexpr RenderGraphResourceId MotionVectors{ 6U };
constexpr RenderGraphResourceId TemporalHistory{ 7U };
constexpr RenderGraphResourceId BloomPrefilter{ 8U };
constexpr RenderGraphResourceId BloomPing{ 9U };
constexpr RenderGraphResourceId BloomCombine{ 10U };
constexpr RenderGraphResourceId PostProcessFinal{ 11U };
constexpr RenderGraphResourceId FinalOutput{ 12U };
constexpr RenderGraphResourceId GBufferAlbedo{ 13U };
constexpr RenderGraphResourceId GBufferNormal{ 14U };
constexpr RenderGraphResourceId GBufferMaterial{ 15U };
constexpr RenderGraphResourceId GBufferDepth{ 16U };
constexpr RenderGraphResourceId GBufferSurface{ 17U };
constexpr std::uint16_t Max = 64U;

} // namespace RenderGraphResource

enum class RenderGraphResourceLifetime : std::uint8_t {
    Transient,
    External,
};

struct RenderGraphResourceDesc {
    RenderGraphResourceId id{};
    RenderTargetDesc target{};
    RenderGraphResourceLifetime lifetime = RenderGraphResourceLifetime::Transient;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return id.IsValid() && target.IsValid();
    }
};

} // namespace kb::render
