#pragma once

#include <cstdint>

namespace kb::render {

struct RenderProxyId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(RenderProxyId lhs, RenderProxyId rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

} // namespace kb::render
