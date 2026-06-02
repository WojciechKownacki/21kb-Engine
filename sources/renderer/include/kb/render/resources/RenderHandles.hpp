#pragma once

#include <cstdint>

namespace kb::render {

namespace detail {

[[nodiscard]] constexpr std::uint32_t RenderHandleIndex(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value & 0xFFFF'FFFFULL);
}

[[nodiscard]] constexpr std::uint32_t RenderHandleGeneration(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value >> 32U);
}

[[nodiscard]] constexpr std::uint64_t MakeRenderHandleValue(std::uint32_t index, std::uint32_t generation) noexcept {
    return (static_cast<std::uint64_t>(generation) << 32U) | static_cast<std::uint64_t>(index);
}

} // namespace detail

struct RenderMeshHandle {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Index() != 0U && Generation() != 0U;
    }

    [[nodiscard]] constexpr std::uint32_t Index() const noexcept {
        return detail::RenderHandleIndex(value);
    }

    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept {
        return detail::RenderHandleGeneration(value);
    }

    [[nodiscard]] friend constexpr bool operator==(RenderMeshHandle lhs, RenderMeshHandle rhs) noexcept = default;
};

struct RenderMaterialHandle {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Index() != 0U && Generation() != 0U;
    }

    [[nodiscard]] constexpr std::uint32_t Index() const noexcept {
        return detail::RenderHandleIndex(value);
    }

    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept {
        return detail::RenderHandleGeneration(value);
    }

    [[nodiscard]] friend constexpr bool operator==(RenderMaterialHandle lhs, RenderMaterialHandle rhs) noexcept = default;
};

struct RenderTextureHandle {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return Index() != 0U && Generation() != 0U;
    }

    [[nodiscard]] constexpr std::uint32_t Index() const noexcept {
        return detail::RenderHandleIndex(value);
    }

    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept {
        return detail::RenderHandleGeneration(value);
    }

    [[nodiscard]] friend constexpr bool operator==(RenderTextureHandle lhs, RenderTextureHandle rhs) noexcept = default;
};

} // namespace kb::render
