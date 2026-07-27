#pragma once

#include "kb/render/frame/RenderTargetDesc.hpp"

#include <cstdint>

namespace kb::render {

struct RenderViewportId {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(RenderViewportId lhs, RenderViewportId rhs) noexcept = default;
};

struct RenderViewportDesc {
    RenderViewportId id{};
    RenderExtent extent{};
    std::uint32_t viewportIndex = 0;
    // The local player whose view this viewport presents. The renderer keeps
    // this as a plain stable id so it does not depend on the engine input
    // module. Hosts must set it when submitting local-player views; 0 is the
    // primary local user and preserves every existing single-player submit.
    std::uint32_t localUserId = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return id.IsValid() && extent.IsValid();
    }
};

} // namespace kb::render
