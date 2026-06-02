#pragma once

#include <cstdint>

namespace kb::render::ViewId {

constexpr std::uint16_t Invalid = 0xFFFFU;
constexpr std::uint16_t Scene3D = 0;
constexpr std::uint16_t TransparentScene = 1;
constexpr std::uint16_t SceneResolve = TransparentScene;
constexpr std::uint16_t Overlay = 2;
constexpr std::uint16_t EditorUi = 3;
constexpr std::uint16_t GpuCompute = 4;
constexpr std::uint16_t EditorSelectionMask = 5;
constexpr std::uint16_t PostProcessBloomPrefilter = 6;
constexpr std::uint16_t FinalComposite = 7;
constexpr std::uint16_t PostProcessBloomBlurH = 8;
constexpr std::uint16_t PostProcessBloomBlurV = 9;
constexpr std::uint16_t PostProcessHdrCombine = 10;
constexpr std::uint16_t PostProcessHdrFinalize = 11;
constexpr std::uint16_t ShadowDepth = 12;
constexpr std::uint16_t ReservedStart = 13;
constexpr std::uint16_t DetachedViewportStart = 16;
constexpr std::uint16_t DetachedViewportStride = 12;
constexpr std::uint16_t Max = 256;

[[nodiscard]] constexpr bool IsValid(std::uint16_t viewId) noexcept {
    return viewId < Max;
}

} // namespace kb::render::ViewId
