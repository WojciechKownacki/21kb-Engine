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
constexpr RenderGraphResourceId BloomPrefilter{ 5U };
constexpr RenderGraphResourceId BloomPing{ 6U };
constexpr RenderGraphResourceId BloomCombine{ 7U };
constexpr RenderGraphResourceId PostProcessFinal{ 8U };
constexpr RenderGraphResourceId FinalOutput{ 9U };
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
