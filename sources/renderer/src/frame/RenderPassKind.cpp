#include "kb/render/frame/RenderPassKind.hpp"

#include <array>

namespace kb::render {
namespace {

constexpr std::array<RenderPassKind, RenderPassKindCount> kRequiredRenderPassKinds{
    RenderPassKind::SceneTargetSetup,
    RenderPassKind::ShadowDepth,
    RenderPassKind::OpaqueScene,
    RenderPassKind::TransparentScene,
    RenderPassKind::EditorSelectionMask,
    RenderPassKind::PostProcessExposureReadback,
    RenderPassKind::PostProcessMotionVectors,
    RenderPassKind::PostProcessTaaResolve,
    RenderPassKind::PostProcessBloomPrefilter,
    RenderPassKind::PostProcessBloomBlurH,
    RenderPassKind::PostProcessBloomBlurV,
    RenderPassKind::PostProcessHdrCombine,
    RenderPassKind::PostProcessHdrFinalize,
    RenderPassKind::FinalComposite,
    RenderPassKind::EditorSceneOverlays,
    RenderPassKind::EditorUiComposite,
};

} // namespace

const char* RenderPassKindName(RenderPassKind kind) noexcept {
    switch (kind) {
    case RenderPassKind::SceneTargetSetup:
        return "SceneTargetSetup";
    case RenderPassKind::ShadowDepth:
        return "ShadowDepth";
    case RenderPassKind::OpaqueScene:
        return "OpaqueScene";
    case RenderPassKind::TransparentScene:
        return "TransparentScene";
    case RenderPassKind::EditorSelectionMask:
        return "EditorSelectionMask";
    case RenderPassKind::PostProcessExposureReadback:
        return "PostProcessExposureReadback";
    case RenderPassKind::PostProcessMotionVectors:
        return "PostProcessMotionVectors";
    case RenderPassKind::PostProcessTaaResolve:
        return "PostProcessTaaResolve";
    case RenderPassKind::PostProcessBloomPrefilter:
        return "PostProcessBloomPrefilter";
    case RenderPassKind::PostProcessBloomBlurH:
        return "PostProcessBloomBlurH";
    case RenderPassKind::PostProcessBloomBlurV:
        return "PostProcessBloomBlurV";
    case RenderPassKind::PostProcessHdrCombine:
        return "PostProcessHdrCombine";
    case RenderPassKind::PostProcessHdrFinalize:
        return "PostProcessHdrFinalize";
    case RenderPassKind::EditorSceneOverlays:
        return "EditorSceneOverlays";
    case RenderPassKind::FinalComposite:
        return "FinalComposite";
    case RenderPassKind::EditorUiComposite:
        return "EditorUiComposite";
    }
    return "Unknown";
}

std::span<const RenderPassKind> RequiredRenderPassKinds() noexcept {
    return kRequiredRenderPassKinds;
}

} // namespace kb::render
