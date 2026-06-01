#pragma once

#include <cstdint>

namespace kb::render::ViewId {

constexpr std::uint16_t Scene3D = 0;
constexpr std::uint16_t SceneResolve = 1;
constexpr std::uint16_t Overlay = 2;
constexpr std::uint16_t EditorUi = 3;
constexpr std::uint16_t GpuCompute = 4;
constexpr std::uint16_t ReservedStart = 5;
constexpr std::uint16_t Max = 64;

[[nodiscard]] constexpr bool IsValid(std::uint16_t viewId) noexcept {
    return viewId < Max;
}

} // namespace kb::render::ViewId
