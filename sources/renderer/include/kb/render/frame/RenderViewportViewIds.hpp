#pragma once

#include "kb/render/ViewIdPolicy.hpp"
#include "kb/render/frame/RenderPassKind.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kb::render {

struct RenderViewportViewIds {
    static constexpr std::size_t kBloomPyramidExtraMipCount = 5U;

    std::uint16_t shadowDepth = ViewId::Invalid;
    std::uint16_t opaqueScene = ViewId::Invalid;
    std::uint16_t gbufferGeometry = ViewId::Invalid;
    std::uint16_t deferredLighting = ViewId::Invalid;
    std::uint16_t transparentScene = ViewId::Invalid;
    std::uint16_t selectionMask = ViewId::Invalid;
    std::uint16_t postProcessExposureReadback = ViewId::Invalid;
    std::uint16_t postProcessMotionVectors = ViewId::Invalid;
    std::uint16_t postProcessTaaResolve = ViewId::Invalid;
    std::uint16_t postProcessBloomPrefilter = ViewId::Invalid;
    std::uint16_t postProcessBloomBlurH = ViewId::Invalid;
    std::uint16_t postProcessBloomBlurV = ViewId::Invalid;
    std::array<std::uint16_t, kBloomPyramidExtraMipCount> postProcessBloomDownsampleViews{};
    std::array<std::uint16_t, kBloomPyramidExtraMipCount> postProcessBloomMipBlurHViews{};
    std::array<std::uint16_t, kBloomPyramidExtraMipCount> postProcessBloomMipBlurVViews{};
    std::uint16_t postProcessHdrCombine = ViewId::Invalid;
    std::uint16_t postProcessHdrFinalize = ViewId::Invalid;
    std::uint16_t sceneOverlays = ViewId::Invalid;
    std::uint16_t finalComposite = ViewId::Invalid;
    std::uint16_t editorUiComposite = ViewId::Invalid;
    std::uint16_t editorGizmoOverlay = ViewId::Invalid;

    [[nodiscard]] constexpr std::uint16_t ViewFor(RenderPassKind kind) const noexcept {
        switch (kind) {
        case RenderPassKind::SceneTargetSetup:
            return ViewId::Invalid;
        case RenderPassKind::ShadowDepth:
            return shadowDepth;
        case RenderPassKind::OpaqueScene:
            return opaqueScene;
        case RenderPassKind::GBufferGeometry:
            return gbufferGeometry;
        case RenderPassKind::DeferredLighting:
            return deferredLighting;
        case RenderPassKind::TransparentScene:
            return transparentScene;
        case RenderPassKind::EditorSelectionMask:
            return selectionMask;
        case RenderPassKind::PostProcessExposureReadback:
            return postProcessExposureReadback;
        case RenderPassKind::PostProcessMotionVectors:
            return postProcessMotionVectors;
        case RenderPassKind::PostProcessTaaResolve:
            return postProcessTaaResolve;
        case RenderPassKind::PostProcessBloomPrefilter:
            return postProcessBloomPrefilter;
        case RenderPassKind::PostProcessBloomBlurH:
            return postProcessBloomBlurH;
        case RenderPassKind::PostProcessBloomBlurV:
            return postProcessBloomBlurV;
        case RenderPassKind::PostProcessHdrCombine:
            return postProcessHdrCombine;
        case RenderPassKind::PostProcessHdrFinalize:
            return postProcessHdrFinalize;
        case RenderPassKind::EditorSceneOverlays:
            return sceneOverlays;
        case RenderPassKind::FinalComposite:
            return finalComposite;
        case RenderPassKind::EditorUiComposite:
            return editorUiComposite;
        case RenderPassKind::EditorGizmoOverlay:
            return editorGizmoOverlay;
        }
        return ViewId::Invalid;
    }

    [[nodiscard]] constexpr bool HasView(RenderPassKind kind) const noexcept {
        return kind != RenderPassKind::SceneTargetSetup;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return ViewId::IsValid(shadowDepth) && ViewId::IsValid(opaqueScene) &&
               ViewId::IsValid(gbufferGeometry) && ViewId::IsValid(deferredLighting) &&
               ViewId::IsValid(transparentScene) && ViewId::IsValid(selectionMask) &&
               ViewId::IsValid(postProcessExposureReadback) && ViewId::IsValid(postProcessBloomPrefilter) && ViewId::IsValid(postProcessBloomBlurH) &&
               ViewId::IsValid(postProcessMotionVectors) && ViewId::IsValid(postProcessTaaResolve) &&
               ViewId::IsValid(postProcessBloomBlurV) && BloomMipViewsAreValid(postProcessBloomDownsampleViews) &&
               BloomMipViewsAreValid(postProcessBloomMipBlurHViews) && BloomMipViewsAreValid(postProcessBloomMipBlurVViews) &&
               ViewId::IsValid(postProcessHdrCombine) && ViewId::IsValid(postProcessHdrFinalize) &&
               ViewId::IsValid(sceneOverlays) && ViewId::IsValid(finalComposite) && ViewId::IsValid(editorUiComposite) &&
               ViewId::IsValid(editorGizmoOverlay);
    }

private:
    [[nodiscard]] static constexpr bool BloomMipViewsAreValid(const std::array<std::uint16_t, kBloomPyramidExtraMipCount>& views) noexcept {
        for (const std::uint16_t view : views) {
            if (!ViewId::IsValid(view)) {
                return false;
            }
        }
        return true;
    }
};

class RenderViewportViewIdAllocator {
public:
    static constexpr std::size_t kMaxViewportCount =
        1U + (ViewId::Max - ViewId::DetachedViewportStart) / ViewId::DetachedViewportStride;

    [[nodiscard]] static constexpr RenderViewportViewIds ForViewportIndex(std::uint32_t viewportIndex) noexcept {
        if (viewportIndex == 0U) {
            return RenderViewportViewIds{
                .shadowDepth = ViewId::ShadowDepth,
                .opaqueScene = ViewId::Scene3D,
                .gbufferGeometry = ViewId::GBufferGeometry,
                .deferredLighting = ViewId::DeferredLighting,
                .transparentScene = ViewId::TransparentScene,
                .selectionMask = ViewId::EditorSelectionMask,
                .postProcessExposureReadback = ViewId::PostProcessExposureReadback,
                .postProcessMotionVectors = ViewId::PostProcessMotionVectors,
                .postProcessTaaResolve = ViewId::PostProcessTaaResolve,
                .postProcessBloomPrefilter = ViewId::PostProcessBloomPrefilter,
                .postProcessBloomBlurH = ViewId::PostProcessBloomBlurH,
                .postProcessBloomBlurV = ViewId::PostProcessBloomBlurV,
                .postProcessBloomDownsampleViews = BloomMipViews(ViewId::PostProcessBloomDownsampleStart),
                .postProcessBloomMipBlurHViews = BloomMipViews(ViewId::PostProcessBloomMipBlurHStart),
                .postProcessBloomMipBlurVViews = BloomMipViews(ViewId::PostProcessBloomMipBlurVStart),
                .postProcessHdrCombine = ViewId::PostProcessHdrCombine,
                .postProcessHdrFinalize = ViewId::PostProcessHdrFinalize,
                .sceneOverlays = ViewId::Overlay,
                .finalComposite = ViewId::FinalComposite,
                .editorUiComposite = ViewId::EditorUi,
                .editorGizmoOverlay = ViewId::EditorGizmoOverlay,
            };
        }

        const std::uint32_t base = ViewId::DetachedViewportStart + (viewportIndex - 1U) * ViewId::DetachedViewportStride;
        if (base + ViewId::DetachedViewportStride > ViewId::Max) {
            return {};
        }

        return RenderViewportViewIds{
            .shadowDepth = static_cast<std::uint16_t>(base),
            .opaqueScene = static_cast<std::uint16_t>(base + 1U),
            .gbufferGeometry = static_cast<std::uint16_t>(base + 2U),
            .deferredLighting = static_cast<std::uint16_t>(base + 3U),
            .transparentScene = static_cast<std::uint16_t>(base + 4U),
            .selectionMask = static_cast<std::uint16_t>(base + 5U),
            .postProcessExposureReadback = static_cast<std::uint16_t>(base + 6U),
            .postProcessMotionVectors = static_cast<std::uint16_t>(base + 7U),
            .postProcessTaaResolve = static_cast<std::uint16_t>(base + 8U),
            .postProcessBloomPrefilter = static_cast<std::uint16_t>(base + 9U),
            .postProcessBloomBlurH = static_cast<std::uint16_t>(base + 10U),
            .postProcessBloomBlurV = static_cast<std::uint16_t>(base + 11U),
            .postProcessBloomDownsampleViews = BloomMipViews(base + 17U),
            .postProcessBloomMipBlurHViews = BloomMipViews(base + 22U),
            .postProcessBloomMipBlurVViews = BloomMipViews(base + 27U),
            .postProcessHdrCombine = static_cast<std::uint16_t>(base + 12U),
            .postProcessHdrFinalize = static_cast<std::uint16_t>(base + 13U),
            .sceneOverlays = static_cast<std::uint16_t>(base + 14U),
            .finalComposite = static_cast<std::uint16_t>(base + 15U),
            .editorUiComposite = static_cast<std::uint16_t>(base + 16U),
            .editorGizmoOverlay = static_cast<std::uint16_t>(base + 32U),
        };
    }

private:
    [[nodiscard]] static constexpr std::array<std::uint16_t, RenderViewportViewIds::kBloomPyramidExtraMipCount> BloomMipViews(std::uint32_t start) noexcept {
        std::array<std::uint16_t, RenderViewportViewIds::kBloomPyramidExtraMipCount> views{};
        for (std::size_t index = 0; index < views.size(); ++index) {
            views[index] = static_cast<std::uint16_t>(start + index);
        }
        return views;
    }
};

} // namespace kb::render
