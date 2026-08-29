#include "rendering/AnimationClipEditorPanelRenderer.hpp"

#if defined(_WIN32)
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

namespace kb::editor {
namespace {

constexpr int kHeaderHeight = 30;
constexpr int kTransportControlWidth = 28;
constexpr int kTransportControlCount = 9;

[[nodiscard]] COLORREF ThemeColor(EditorColor color) noexcept {
    return RGB(color.r, color.g, color.b);
}

[[nodiscard]] COLORREF Blend(COLORREF a, COLORREF b, int percentB) noexcept {
    const int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

[[nodiscard]] RECT TransportControlRect(const RECT& content, std::uint8_t index) noexcept {
    const int right = static_cast<int>(content.right) - 8 -
        static_cast<int>(index) * kTransportControlWidth;
    return RECT{ right - kTransportControlWidth + 2, static_cast<int>(content.top) + 4,
        right, static_cast<int>(content.top) + kHeaderHeight - 4 };
}

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

void PaintTimeline(HDC dc, const RECT& rect, const AnimationClipTimelineState& timeline, const EditorTheme& theme) {
    const COLORREF background = ThemeColor(theme.background);
    const COLORREF panel = ThemeColor(theme.panel);
    const COLORREF strip = ThemeColor(theme.strip);
    const COLORREF accent = ThemeColor(theme.accent);
    GdiDrawing::FillRectColor(dc, rect, background);
    const int left = static_cast<int>(rect.left);
    const int top = static_cast<int>(rect.top);
    const int right = static_cast<int>(rect.right);
    const int bottom = static_cast<int>(rect.bottom);
    const int outlinerWidth = std::clamp((right - left) / 4, 180, 320);
    const int tracksLeft = left + outlinerWidth;
    const int headerHeight = 24;
    const int rowHeight = 20;
    GdiDrawing::FillRectColor(dc, RECT{ left, top, tracksLeft, bottom }, panel);
    GdiDrawing::FillRectColor(dc, RECT{ tracksLeft, top, right, top + headerHeight }, strip);
    const ScopedFont font{ 12, FW_NORMAL };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ThemeColor(theme.textSecondary));
    RECT outlinerTitle{ left + 8, top, tracksLeft - 4, top + headerHeight };
    DrawTextA(dc, "Track", -1, &outlinerTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    const float visibleDuration = std::max(0.001F, timeline.VisibleDurationSeconds());
    const float visibleStart = timeline.PanSeconds();
    const int trackWidth = std::max(1, right - tracksLeft - 1);
    for (int division = 0; division <= 4; ++division) {
        const int x = tracksLeft + (trackWidth * division) / 4;
        GdiDrawing::FillRectColor(dc, RECT{ x, top + headerHeight, x + 1, bottom }, ThemeColor(theme.gridLine));
        const std::string label = std::to_string(visibleStart + (visibleDuration * static_cast<float>(division)) / 4.0F) + "s";
        RECT tick{ x + 3, top, std::min(right, x + 60), top + headerHeight };
        DrawTextA(dc, label.c_str(), -1, &tick, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    const std::vector<AnimationClipTimelineTrack>& tracks = timeline.Tracks();
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        const int rowTop = top + headerHeight + static_cast<int>(index) * rowHeight;
        if (rowTop >= bottom) break;
        const int rowBottom = std::min(bottom, rowTop + rowHeight);
        if (timeline.SelectedTrack() == index) {
            GdiDrawing::FillRectColor(dc, RECT{ left, rowTop, right, rowBottom }, Blend(panel, accent, 24));
            GdiDrawing::FillRectColor(dc, RECT{ left, rowTop, left + 3, rowBottom }, accent);
        } else if ((index & 1U) != 0U) {
            GdiDrawing::FillRectColor(dc, RECT{ tracksLeft, rowTop, right, rowBottom }, Blend(background, panel, 48));
        }
        SetTextColor(dc, TrackColor(tracks[index].kind));
        RECT label{ left + 8, rowTop, tracksLeft - 6, rowBottom };
        DrawTextA(dc, tracks[index].label.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        for (const AnimationClipTimelineKey& key : tracks[index].keys) {
            const float normalized = std::clamp((key.timeSeconds - visibleStart) / visibleDuration, 0.0F, 1.0F);
            if (key.timeSeconds < visibleStart || key.timeSeconds > visibleStart + visibleDuration) continue;
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
        GdiDrawing::FillRectColor(dc, content, ThemeColor(theme.background));
        const ScopedFont font{ 15, FW_NORMAL };
        const ScopedGdiObject selectedFont(dc, font.handle);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, ThemeColor(theme.textSecondary));
        RECT text = content;
        DrawTextA(dc, "Open an Animation Clip asset to begin editing.", -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    GdiDrawing::FillRectColor(dc, RECT{ content.left, content.top, content.right, content.top + kHeaderHeight }, ThemeColor(theme.strip));
    GdiDrawing::FillRectColor(dc, RECT{ content.left, content.top, content.left + 3, content.top + kHeaderHeight }, ThemeColor(theme.accent));
    const ScopedFont titleFont{ 13, FW_SEMIBOLD };
    const ScopedGdiObject selectedTitleFont(dc, titleFont.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ThemeColor(theme.textPrimary));
    RECT title{ content.left + 10, content.top, content.right - kTransportControlCount * kTransportControlWidth - 12, content.top + kHeaderHeight };
    const kb::assets::AssetMetadata* metadata =
        sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.AnimationClipEditorAssetId());
    const kb::assets::AssetMetadata* previewMeshMetadata =
        sceneContext.Scene().Assets().Manager().Registry().Find(sceneContext.AnimationPreview().SkeletalMeshAsset());
    const std::string name = metadata == nullptr ? std::string{ "Animation Clip" } : metadata->name;
    const AnimationPreviewTransport& transport = sceneContext.AnimationPreview().Transport();
    const std::uint64_t frame = static_cast<std::uint64_t>(transport.NormalizedTime() * transport.DurationSeconds() * transport.FrameRate() + 0.5F);
    const std::uint64_t frameCount = static_cast<std::uint64_t>(transport.DurationSeconds() * transport.FrameRate() + 0.5F);
    char transportText[96]{};
    std::snprintf(transportText, sizeof(transportText), "  |  Frame %llu / %llu  |  %.3f / %.3f s",
        static_cast<unsigned long long>(frame), static_cast<unsigned long long>(frameCount),
        transport.NormalizedTime() * transport.DurationSeconds(), transport.DurationSeconds());
    const SkeletalMeshEditorDetailsModel details = sceneContext.SkeletalMeshEditorDetails();
    const std::string selection = sceneContext.SelectedSkeletalMeshEditorBone() == 0U
        ? std::string{}
        : "  |  " + details.title;
    const std::string preview = previewMeshMetadata == nullptr ? std::string{} : "  |  Preview " + previewMeshMetadata->name;
    const std::string text = name + transportText + preview + selection;
    DrawTextA(dc, text.c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    const char* labels[kTransportControlCount] = { "]", "[", "+", "-", "S", "L", ">|", transport.IsPlaying() ? "||" : ">", "|<" };
    for (std::uint8_t index = 0U; index < kTransportControlCount; ++index) {
        const RECT button = TransportControlRect(content, index);
        const bool active = (index == 4U && sceneContext.AnimationClipEditorTimeline().SnappingEnabled()) ||
            (index == 5U && transport.Loops());
        const COLORREF buttonFill = active ? ThemeColor(theme.accent) : ThemeColor(theme.toolbarButton);
        GdiDrawing::DrawSharpFrame(dc, button, buttonFill,
            active ? ThemeColor(theme.accent) : ThemeColor(theme.borderPanel));
        SetTextColor(dc, ThemeColor(theme.textPrimary));
        RECT textRect = button;
        DrawTextA(dc, labels[index], -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    const int timelineHeight = std::clamp((static_cast<int>(content.bottom) - static_cast<int>(content.top)) / 3, 150, 300);
    const RECT viewport{ content.left, content.top + kHeaderHeight, content.right, content.bottom - timelineHeight };
    PaintTimeline(dc, RECT{ content.left, viewport.bottom, content.right, content.bottom },
        sceneContext.AnimationClipEditorTimeline(), theme);
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

std::optional<std::uint8_t> AnimationClipEditorPanelRenderer::TransportControlAt(const RECT& content, int x, int y) noexcept {
    for (std::uint8_t index = 0U; index < kTransportControlCount; ++index) {
        const RECT button = TransportControlRect(content, index);
        if (x >= button.left && x < button.right && y >= button.top && y < button.bottom) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> AnimationClipEditorPanelRenderer::TimelineTrackAt(
    const RECT& content, const AnimationClipTimelineState& timeline, int x, int y) noexcept {
    const int timelineHeight = std::clamp((static_cast<int>(content.bottom) - static_cast<int>(content.top)) / 3, 150, 300);
    const int timelineTop = static_cast<int>(content.bottom) - timelineHeight;
    const int outlinerWidth = std::clamp((static_cast<int>(content.right) - static_cast<int>(content.left)) / 4, 180, 320);
    const int tracksLeft = static_cast<int>(content.left) + outlinerWidth;
    constexpr int kTimelineHeaderHeight = 24;
    constexpr int kRowHeight = 20;
    if (x < content.left || x >= tracksLeft || y < timelineTop + kTimelineHeaderHeight || y >= content.bottom) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>((y - (timelineTop + kTimelineHeaderHeight)) / kRowHeight);
    return index < timeline.Tracks().size() ? std::optional<std::size_t>{ index } : std::nullopt;
}

std::optional<float> AnimationClipEditorPanelRenderer::TimelineTimeAt(
    const RECT& content, const AnimationClipTimelineState& timeline, int x, int y) noexcept {
    const int timelineHeight = std::clamp((static_cast<int>(content.bottom) - static_cast<int>(content.top)) / 3, 150, 300);
    const int timelineTop = static_cast<int>(content.bottom) - timelineHeight;
    const int outlinerWidth = std::clamp((static_cast<int>(content.right) - static_cast<int>(content.left)) / 4, 180, 320);
    const int tracksLeft = static_cast<int>(content.left) + outlinerWidth;
    if (x < tracksLeft || x >= content.right || y < timelineTop || y >= content.bottom) return std::nullopt;
    const int width = std::max(1, static_cast<int>(content.right) - tracksLeft - 1);
    const float normalized = std::clamp(static_cast<float>(x - tracksLeft) / static_cast<float>(width), 0.0F, 1.0F);
    return timeline.PanSeconds() + normalized * timeline.VisibleDurationSeconds();
}

std::optional<kb::scene::SkeletonBoneId> AnimationClipEditorPanelRenderer::BoneAt(
    const RECT& content, const EditorSceneContext& sceneContext, int x, int y) noexcept {
    const int timelineHeight = std::clamp((static_cast<int>(content.bottom) - static_cast<int>(content.top)) / 3, 150, 300);
    const RECT viewport{ content.left, content.top + kHeaderHeight, content.right, content.bottom - timelineHeight };
    if (x < viewport.left || x >= viewport.right || y < viewport.top || y >= viewport.bottom) return std::nullopt;
    const EditorViewportCameraAxes camera = sceneContext.AnimationPreviewCamera().Axes();
    const float width = static_cast<float>(std::max(1L, viewport.right - viewport.left));
    const float height = static_cast<float>(std::max(1L, viewport.bottom - viewport.top));
    const float tangent = std::tan(sceneContext.AnimationPreviewCamera().VerticalFovDegrees() * 0.00872664626F);
    const float aspect = width / height;
    auto project = [&](kb::scene::Vec3 point, float& screenX, float& screenY) {
        const kb::scene::Vec3 relative = point - camera.position;
        const float depth = kb::math::Dot(relative, camera.forward);
        if (depth <= 0.001F) return false;
        const float horizontal = kb::math::Dot(relative, camera.right) / (depth * tangent * aspect);
        const float vertical = kb::math::Dot(relative, camera.up) / (depth * tangent);
        screenX = static_cast<float>(viewport.left) + (horizontal + 1.0F) * width * 0.5F;
        screenY = static_cast<float>(viewport.top) + (1.0F - vertical) * height * 0.5F;
        return true;
    };
    std::optional<kb::scene::SkeletonBoneId> closest;
    float closestDistance = 100.0F;
    for (const AnimationPreviewOverlayLine& line : sceneContext.AnimationPreviewOverlays().lines) {
        if (line.boneId == 0U) continue;
        float fromX = 0.0F, fromY = 0.0F, toX = 0.0F, toY = 0.0F;
        if (!project(line.from, fromX, fromY) || !project(line.to, toX, toY)) continue;
        const float dx = toX - fromX;
        const float dy = toY - fromY;
        const float squaredLength = dx * dx + dy * dy;
        const float parameter = squaredLength <= 0.0001F ? 0.0F : std::clamp(
            ((static_cast<float>(x) - fromX) * dx + (static_cast<float>(y) - fromY) * dy) / squaredLength, 0.0F, 1.0F);
        const float distanceX = static_cast<float>(x) - (fromX + parameter * dx);
        const float distanceY = static_cast<float>(y) - (fromY + parameter * dy);
        const float distance = distanceX * distanceX + distanceY * distanceY;
        if (distance < closestDistance) {
            closestDistance = distance;
            closest = line.boneId;
        }
    }
    return closest;
}

} // namespace kb::editor

#endif
