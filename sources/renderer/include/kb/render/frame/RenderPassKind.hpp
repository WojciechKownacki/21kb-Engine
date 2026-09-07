#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::render {

enum class RenderPassKind : std::uint8_t {
    SceneTargetSetup,
    ShadowDepth,
    OpaqueScene,
    GBufferGeometry,
    DeferredLighting,
    TransparentScene,
    EditorSelectionMask,
    PostProcessExposureReadback,
    PostProcessMotionVectors,
    PostProcessTaaResolve,
    PostProcessBloomPrefilter,
    PostProcessBloomBlurH,
    PostProcessBloomBlurV,
    PostProcessHdrCombine,
    PostProcessHdrFinalize,
    EditorSceneOverlays,
    RuntimeUiBlurH,
    RuntimeUiBlurV,
    FinalComposite,
    UiComposite,
    EditorUiComposite = UiComposite,
    EditorGizmoOverlay,
};

constexpr std::size_t RenderPassKindCount = 21U;

[[nodiscard]] const char* RenderPassKindName(RenderPassKind kind) noexcept;
[[nodiscard]] std::span<const RenderPassKind> RequiredRenderPassKinds() noexcept;

} // namespace kb::render
