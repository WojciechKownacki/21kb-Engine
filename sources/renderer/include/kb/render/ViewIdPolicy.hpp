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
constexpr std::uint16_t ScreenUIBlurH = 34;
constexpr std::uint16_t ScreenUIBlurV = 35;
constexpr std::uint16_t ScreenUIComposite = 36;
constexpr std::uint16_t ReservedStart = 18;
constexpr std::uint16_t PostProcessBloomDownsampleStart = 18;
constexpr std::uint16_t PostProcessBloomMipBlurHStart = 23;
constexpr std::uint16_t PostProcessBloomMipBlurVStart = 28;
constexpr std::uint16_t DetachedViewportStart = 37;
constexpr std::uint16_t DetachedViewportStride = 36;
constexpr std::uint16_t Max = 512;
// Global asynchronous capture runs after every remapped viewport draw.
constexpr std::uint16_t ScreenCapture = Max - 1U;
// Detached viewports occupy ids below this limit (six of them, as before the view budget grew).
constexpr std::uint16_t DetachedViewportLimit = 253;
// Shadow views of the primary viewport live above the detached range: one extra depth view per
// cascade beyond the first, then six cube faces for each shadow-casting point light.
constexpr std::uint16_t ShadowCascadeExtraViews = 3;
constexpr std::uint16_t ShadowCascadeExtraStart = 256;
constexpr std::uint16_t MaxPointShadowLights = 8;
constexpr std::uint16_t PointShadowFaceCount = 6;
constexpr std::uint16_t PointShadowViewCount = MaxPointShadowLights * PointShadowFaceCount;
constexpr std::uint16_t PointShadowStart = ShadowCascadeExtraStart + ShadowCascadeExtraViews;
static_assert(PointShadowStart + PointShadowViewCount <= ScreenCapture, "Shadow views overlap the capture view");
// The screen-space GI resolve of a viewport runs after every other view of the frame (it reads the
// lit colour the scene produced and feeds the next frame): one view per viewport index.
constexpr std::uint16_t GiResolveStart = 320;
constexpr std::uint16_t GiResolveMaxViewports = 8;
static_assert(GiResolveStart >= PointShadowStart + PointShadowViewCount, "GI resolve views overlap the shadow views");
static_assert(GiResolveStart + GiResolveMaxViewports <= ScreenCapture, "GI resolve views overlap the capture view");

[[nodiscard]] constexpr bool IsValid(std::uint16_t viewId) noexcept {
    return viewId < Max;
}

} // namespace kb::render::ViewId
