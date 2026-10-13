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
    ScreenUIBlurH,
    ScreenUIBlurV,
    FinalComposite,
    ScreenUIComposite,
    EditorUiComposite,
    EditorGizmoOverlay,
    // Unreal's post-process materials (blendables), after the HDR finalize and before the tonemap.
    PostProcessMaterial0,
    PostProcessMaterial1,
    PostProcessMaterial2,
    PostProcessMaterial3,
};

constexpr std::size_t RenderPassKindCount = 26U;

[[nodiscard]] const char* RenderPassKindName(RenderPassKind kind) noexcept;
[[nodiscard]] std::span<const RenderPassKind> RequiredRenderPassKinds() noexcept;

} // namespace kb::render
