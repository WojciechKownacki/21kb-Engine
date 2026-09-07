#pragma once

#include <cstdint>

namespace kb::render::ViewId {

constexpr std::uint16_t Invalid = 0xFFFFU;
constexpr std::uint16_t Scene3D = 0;
constexpr std::uint16_t GBufferGeometry = 1;
constexpr std::uint16_t DeferredLighting = 2;
constexpr std::uint16_t TransparentScene = 3;
constexpr std::uint16_t SceneResolve = TransparentScene;
constexpr std::uint16_t Overlay = 4;
constexpr std::uint16_t EditorUi = 5;
constexpr std::uint16_t GpuCompute = 6;
constexpr std::uint16_t EditorSelectionMask = 7;
constexpr std::uint16_t PostProcessBloomPrefilter = 8;
constexpr std::uint16_t FinalComposite = 9;
constexpr std::uint16_t PostProcessBloomBlurH = 10;
constexpr std::uint16_t PostProcessBloomBlurV = 11;
constexpr std::uint16_t PostProcessHdrCombine = 12;
constexpr std::uint16_t PostProcessHdrFinalize = 13;
constexpr std::uint16_t ShadowDepth = 14;
constexpr std::uint16_t PostProcessExposureReadback = 15;
constexpr std::uint16_t PostProcessMotionVectors = 16;
constexpr std::uint16_t PostProcessTaaResolve = 17;
constexpr std::uint16_t EditorGizmoOverlay = 33;
constexpr std::uint16_t ReservedStart = 18;
constexpr std::uint16_t PostProcessBloomDownsampleStart = 18;
constexpr std::uint16_t PostProcessBloomMipBlurHStart = 23;
constexpr std::uint16_t PostProcessBloomMipBlurVStart = 28;
constexpr std::uint16_t DetachedViewportStart = 34;
constexpr std::uint16_t DetachedViewportStride = 33;
constexpr std::uint16_t Max = 256;

[[nodiscard]] constexpr bool IsValid(std::uint16_t viewId) noexcept {
    return viewId < Max;
}

} // namespace kb::render::ViewId
