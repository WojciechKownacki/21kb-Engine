#include "rendering/AnimationClipEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace kb::editor {
namespace {

[[nodiscard]] COLORREF TrackColor(AnimationClipTimelineTrackKind kind) noexcept {
    switch (kind) {
    case AnimationClipTimelineTrackKind::Bone: return RGB(103, 174, 255);
    case AnimationClipTimelineTrackKind::Transform: return RGB(136, 196, 151);
    case AnimationClipTimelineTrackKind::Morph: return RGB(220, 143, 231);
    case AnimationClipTimelineTrackKind::Curve: return RGB(245, 191, 94);
    case AnimationClipTimelineTrackKind::Event: return RGB(244, 111, 108);
    case AnimationClipTimelineTrackKind::RootMotion: return RGB(255, 127, 180);
    }
    return RGB(207, 214, 222);
}

void PaintTimeline(HDC dc, const RECT& rect, const AnimationClipTimelineState& timeline) {
    GdiDrawing::FillRectColor(dc, rect, RGB(25, 27, 31));
    const int left = static_cast<int>(rect.left);
    const int top = static_cast<int>(rect.top);
    const int right = static_cast<int>(rect.right);
    const int bottom = static_cast<int>(rect.bottom);
    const int outlinerWidth = std::clamp((right - left) / 4, 180, 320);
    const int tracksLeft = left + outlinerWidth;
    const int headerHeight = 24;
    const int rowHeight = 20;
    GdiDrawing::FillRectColor(dc, RECT{ left, top, tracksLeft, bottom }, RGB(31, 34, 39));
    GdiDrawing::FillRectColor(dc, RECT{ tracksLeft, top, right, top + headerHeight }, RGB(34, 37, 43));
    const ScopedFont font{ 12, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(161, 172, 186));
    RECT outlinerTitle{ left + 8, top, tracksLeft - 4, top + headerHeight };
    DrawTextA(dc, "Track", -1, &outlinerTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    const float duration = std::max(0.001F, timeline.DurationSeconds());
    const int trackWidth = std::max(1, right - tracksLeft - 1);
    for (int division = 0; division <= 4; ++division) {
        const int x = tracksLeft + (trackWidth * division) / 4;
        GdiDrawing::FillRectColor(dc, RECT{ x, top + headerHeight, x + 1, bottom }, RGB(49, 53, 61));
        const std::string label = std::to_string((duration * static_cast<float>(division)) / 4.0F) + "s";
        RECT tick{ x + 3, top, std::min(right, x + 60), top + headerHeight };
        DrawTextA(dc, label.c_str(), -1, &tick, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    const std::vector<AnimationClipTimelineTrack>& tracks = timeline.Tracks();
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        const int rowTop = top + headerHeight + static_cast<int>(index) * rowHeight;
        if (rowTop >= bottom) break;
        const int rowBottom = std::min(bottom, rowTop + rowHeight);
        if ((index & 1U) != 0U) GdiDrawing::FillRectColor(dc, RECT{ tracksLeft, rowTop, right, rowBottom }, RGB(29, 32, 37));
        SetTextColor(dc, TrackColor(tracks[index].kind));
        RECT label{ left + 8, rowTop, tracksLeft - 6, rowBottom };
        DrawTextA(dc, tracks[index].label.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        for (const AnimationClipTimelineKey& key : tracks[index].keys) {
            const float normalized = std::clamp(key.timeSeconds / duration, 0.0F, 1.0F);
            const int x = tracksLeft + static_cast<int>(normalized * static_cast<float>(trackWidth - 1));
            GdiDrawing::FillRectColor(dc, RECT{ x - 2, rowTop + 6, x + 3, std::min(rowBottom - 2, rowTop + 11) }, TrackColor(tracks[index].kind));
        }
    }
}

} // namespace

void AnimationClipEditorPanelRenderer::Paint(
    HDC dc,
    HWND host,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport) const {
    if (!sceneContext.HasAnimationClipEditorAsset() ||
        sceneContext.AnimationClipEditorPreviewScene() == nullptr) {
        GdiDrawing::FillRectColor(dc, content, RGB(27, 29, 33));
        const ScopedFont font{ 15, FW_NORMAL };
        const ScopedGdiObject selectedFont(dc, font.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(168, 178, 190));
        RECT text = content;
        DrawTextA(dc, "Open an Animation Clip asset to begin editing.", -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    GdiDrawing::FillRectColor(dc, RECT{ content.left, content.top, content.right, content.top + 30 }, RGB(34, 37, 43));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(219, 225, 233));
    RECT title{ content.left + 10, content.top, content.right - 10, content.top + 30 };
    const kb::assets::AssetMetadata* metadata =
        sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.AnimationClipEditorAssetId());
    const std::string name = metadata == nullptr ? std::string{ "Animation Clip" } : metadata->name;
    const AnimationPreviewTransport& transport = sceneContext.AnimationPreview().Transport();
    const std::string text = name + "  |  " + std::to_string(transport.DurationSeconds()) + " s";
    DrawTextA(dc, text.c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    const int timelineHeight = std::clamp((static_cast<int>(content.bottom) - static_cast<int>(content.top)) / 3, 150, 300);
    const RECT viewport{ content.left, content.top + 30, content.right, content.bottom - timelineHeight };
    PaintTimeline(dc, RECT{ content.left, viewport.bottom, content.right, content.bottom }, sceneContext.AnimationClipEditorTimeline());
    if (sceneViewport == nullptr) return;
    const std::uint64_t revision = sceneContext.AnimationClipEditorPreviewRevision();
    EditorSceneBgfxViewport::PresentSettings settings{};
    settings.viewportKey = panel.id;
    settings.editorSceneOverlaysEnabled = false;
    settings.sceneRevision = revision;
    settings.sceneDirtyBaseRevision = revision;
    settings.sceneFullSyncRequired = false;
    settings.msaaSamples = renderBackendSettings.MsaaSamples();
    settings.shadowPassEnabled = renderBackendSettings.ShadowsEnabled();
    settings.postProcessEnabled = true;
    settings.selectionMaskEnabled = false;
    settings.selectionOutlineEnabled = false;
    settings.gpuDrivenRuntimeDispatchEnabled = renderBackendSettings.GpuDrivenEnabled();
    sceneViewport->Present(dc, host, viewport, *sceneContext.AnimationClipEditorPreviewScene(), theme, settings);
}

} // namespace kb::editor

#endif
