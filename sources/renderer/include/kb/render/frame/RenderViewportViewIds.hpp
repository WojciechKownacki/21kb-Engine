#pragma once

#include "kb/render/ViewIdPolicy.hpp"
#include "kb/render/frame/RenderPassKind.hpp"

#include <cstdint>

namespace kb::render {

struct RenderViewportViewIds {
    std::uint16_t shadowDepth = ViewId::Invalid;
    std::uint16_t opaqueScene = ViewId::Invalid;
    std::uint16_t transparentScene = ViewId::Invalid;
    std::uint16_t selectionMask = ViewId::Invalid;
    std::uint16_t postProcessBloomPrefilter = ViewId::Invalid;
    std::uint16_t postProcessBloomBlurH = ViewId::Invalid;
    std::uint16_t postProcessBloomBlurV = ViewId::Invalid;
    std::uint16_t postProcessHdrCombine = ViewId::Invalid;
    std::uint16_t postProcessHdrFinalize = ViewId::Invalid;
    std::uint16_t sceneOverlays = ViewId::Invalid;
    std::uint16_t finalComposite = ViewId::Invalid;
    std::uint16_t editorUiComposite = ViewId::Invalid;

    [[nodiscard]] constexpr std::uint16_t ViewFor(RenderPassKind kind) const noexcept {
        switch (kind) {
        case RenderPassKind::SceneTargetSetup:
            return ViewId::Invalid;
        case RenderPassKind::ShadowDepth:
            return shadowDepth;
        case RenderPassKind::OpaqueScene:
            return opaqueScene;
        case RenderPassKind::TransparentScene:
            return transparentScene;
        case RenderPassKind::EditorSelectionMask:
            return selectionMask;
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
        }
        return ViewId::Invalid;
    }

    [[nodiscard]] constexpr bool HasView(RenderPassKind kind) const noexcept {
        return kind != RenderPassKind::SceneTargetSetup;
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return ViewId::IsValid(shadowDepth) && ViewId::IsValid(opaqueScene) && ViewId::IsValid(transparentScene) && ViewId::IsValid(selectionMask) &&
               ViewId::IsValid(postProcessBloomPrefilter) && ViewId::IsValid(postProcessBloomBlurH) &&
               ViewId::IsValid(postProcessBloomBlurV) && ViewId::IsValid(postProcessHdrCombine) &&
               ViewId::IsValid(postProcessHdrFinalize) && ViewId::IsValid(sceneOverlays) &&
               ViewId::IsValid(finalComposite) && ViewId::IsValid(editorUiComposite);
    }
};

class RenderViewportViewIdAllocator {
public:
    [[nodiscard]] static constexpr RenderViewportViewIds ForViewportIndex(std::uint32_t viewportIndex) noexcept {
        if (viewportIndex == 0U) {
            return RenderViewportViewIds{
                .shadowDepth = ViewId::ShadowDepth,
                .opaqueScene = ViewId::Scene3D,
                .transparentScene = ViewId::TransparentScene,
                .selectionMask = ViewId::EditorSelectionMask,
                .postProcessBloomPrefilter = ViewId::PostProcessBloomPrefilter,
                .postProcessBloomBlurH = ViewId::PostProcessBloomBlurH,
                .postProcessBloomBlurV = ViewId::PostProcessBloomBlurV,
                .postProcessHdrCombine = ViewId::PostProcessHdrCombine,
                .postProcessHdrFinalize = ViewId::PostProcessHdrFinalize,
                .sceneOverlays = ViewId::Overlay,
                .finalComposite = ViewId::FinalComposite,
                .editorUiComposite = ViewId::EditorUi,
            };
        }

        const std::uint32_t base = ViewId::DetachedViewportStart + (viewportIndex - 1U) * ViewId::DetachedViewportStride;
        if (base + ViewId::DetachedViewportStride > ViewId::Max) {
            return {};
        }

        return RenderViewportViewIds{
            .shadowDepth = static_cast<std::uint16_t>(base),
            .opaqueScene = static_cast<std::uint16_t>(base + 1U),
            .transparentScene = static_cast<std::uint16_t>(base + 2U),
            .selectionMask = static_cast<std::uint16_t>(base + 3U),
            .postProcessBloomPrefilter = static_cast<std::uint16_t>(base + 4U),
            .postProcessBloomBlurH = static_cast<std::uint16_t>(base + 5U),
            .postProcessBloomBlurV = static_cast<std::uint16_t>(base + 6U),
            .postProcessHdrCombine = static_cast<std::uint16_t>(base + 7U),
            .postProcessHdrFinalize = static_cast<std::uint16_t>(base + 8U),
            .sceneOverlays = static_cast<std::uint16_t>(base + 9U),
            .finalComposite = static_cast<std::uint16_t>(base + 10U),
            .editorUiComposite = static_cast<std::uint16_t>(base + 11U),
        };
    }
};

} // namespace kb::render
