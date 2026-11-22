#include "rendering/GamePanelContentRenderer.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/HeroIconPainter.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarMetrics.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

using Metrics = SceneViewportToolbarMetrics;

constexpr int kReadoutWidth = 220;
constexpr COLORREF kScreenColor = RGB(0, 0, 0);

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] HeroIconKind DeviceIcon(EditorGameViewDevice device) noexcept {
    switch (device) {
    case EditorGameViewDevice::Computer:
        return HeroIconKind::Monitor;
    case EditorGameViewDevice::Tablet:
        return HeroIconKind::Tablet;
    case EditorGameViewDevice::Phone:
        return HeroIconKind::Smartphone;
    }
    return HeroIconKind::Monitor;
}

// A rectangle of the bar, kept inside the panel when the panel is too narrow for the whole bar.
[[nodiscard]] RECT BarRect(const RECT& content, int left, int width, int top) noexcept {
    const int limit = std::max<int>(content.left, content.right - Metrics::PaddingX);
    const int clippedLeft = std::min(left, limit);
    return RECT{ clippedLeft, top, std::min(left + width, limit), top + Metrics::ButtonHeight };
}

void FillEllipse(HDC dc, int centerX, int centerY, int radius, COLORREF fill, COLORREF line) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, centerX - radius, centerY - radius, centerX + radius + 1, centerY + radius + 1);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// A rim of the given width drawn inside the rectangle; the outermost pixel is lit, as a polished edge is.
void DrawRim(HDC dc, const RECT& body, int width, int radius, const EditorTheme& theme) {
    HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HGDIOBJ oldBrush = SelectObject(dc, hollow);
    const auto stroke = [&](int penWidth, COLORREF color, const RECT& rect) {
        LOGBRUSH brush{ BS_SOLID, color, 0 };
        HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_ROUND, penWidth, &brush, 0, nullptr);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int half = penWidth / 2;
        RoundRect(dc, rect.left + half, rect.top + half, rect.right - half, rect.bottom - half, radius, radius);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    };
    stroke(width, GdiDrawing::ToColorRef(theme.borderPanel), body);
    stroke(1, GdiDrawing::ToColorRef(theme.textDisabled), body);
    SelectObject(dc, oldBrush);
}

// The device around the screen: a thin frame for a computer, and for a tablet or a phone the metal rim, the dark
// bezel and the camera dot.
void DrawDevice(HDC dc, const EditorGameViewFrame& frame, EditorGameViewDevice device, const EditorTheme& theme) {
    if (device == EditorGameViewDevice::Computer) {
        GdiDrawing::FillRectColor(dc, frame.body, GdiDrawing::ToColorRef(theme.borderPanel));
        return;
    }
    const int bezel = static_cast<int>(frame.screen.left - frame.body.left) - frame.rim;
    const int radius = std::max(6, bezel * 3);
    const COLORREF bezelColor = GdiDrawing::ToColorRef(theme.chrome);
    HBRUSH brush = CreateSolidBrush(bezelColor);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, frame.body.left, frame.body.top, frame.body.right + 1, frame.body.bottom + 1, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    DrawRim(dc, frame.body, frame.rim, radius, theme);
    if (frame.cameraRadius > 0) {
        const COLORREF lens = GdiDrawing::ToColorRef(theme.toolbarButton);
        FillEllipse(dc, frame.camera.x, frame.camera.y, frame.cameraRadius, bezelColor, GdiDrawing::ToColorRef(theme.borderPanel));
        FillEllipse(dc, frame.camera.x, frame.camera.y, std::max(1, frame.cameraRadius / 2), lens, lens);
    }
}

// The rotate button of a device that cannot turn stays on the bar, dimmed.
// A bar button that does nothing for the chosen device stays on the bar, dimmed.
void DrawDeviceOptionButton(HDC dc, const RECT& rect, HeroIconKind icon, bool enabled, bool active, const EditorTheme& theme) {
    if (enabled) {
        SceneViewportToolbarDrawing::DrawIconButton(dc, rect, icon, theme, active);
        return;
    }
    const COLORREF fill = SceneViewportToolbarDrawing::Blend(
        GdiDrawing::ToColorRef(theme.toolbarButton), GdiDrawing::ToColorRef(theme.toolbar), 1, 2);
    SceneViewportToolbarDrawing::FillRound(dc, rect, fill, fill, SceneViewportToolbarMetrics::ButtonRadius);
    const int size = Metrics::IconSize;
    const RECT iconRect{ rect.left + (rect.right - rect.left - size) / 2, rect.top + (rect.bottom - rect.top - size) / 2,
        rect.left + (rect.right - rect.left - size) / 2 + size, rect.top + (rect.bottom - rect.top - size) / 2 + size };
    HeroIconPainter::Draw(dc, iconRect, icon, GdiDrawing::ToColorRef(theme.textDisabled), 1);
}

} // namespace

GameViewRects GamePanelContentRenderer::Resolve(const RECT& content) noexcept {
    GameViewRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = std::min(content.bottom, content.top + ToolbarHeight);
    const int top = rects.toolbar.top + (ToolbarHeight - Metrics::ButtonHeight) / 2;
    // The same buttons, sizes and gaps as the Scene View's bar: the devices, then the rotation and the safe area.
    int left = content.left + Metrics::PaddingX;
    const auto next = [&](int width) {
        const RECT rect = BarRect(content, left, width, top);
        left += width + Metrics::ButtonGap;
        return rect;
    };
    for (RECT& button : rects.devices) {
        button = next(Metrics::IconButtonWidth);
    }
    left += Metrics::GroupGap - Metrics::ButtonGap;
    rects.rotateButton = next(Metrics::IconButtonWidth);
    rects.safeAreaButton = next(Metrics::IconButtonWidth);
    left += Metrics::GroupGap - Metrics::ButtonGap;
    rects.readout = BarRect(content, std::max<int>(left, content.right - Metrics::PaddingX - kReadoutWidth), kReadoutWidth, top);
    rects.renderArea = content;
    rects.renderArea.top = rects.toolbar.bottom;
    return rects;
}

std::optional<EditorGameViewDevice> GamePanelContentRenderer::DeviceAt(const GameViewRects& rects, int x, int y) noexcept {
    for (std::size_t index = 0; index < rects.devices.size(); ++index) {
        if (PointInRect(rects.devices[index], x, y)) {
            return EditorGameView::kDevices[index];
        }
    }
    return std::nullopt;
}

bool GamePanelContentRenderer::ShowsGame(const EditorSceneContext& sceneContext) noexcept {
    return sceneContext.HasPlayModeSceneSession() && sceneContext.PlayCameraEntity().IsValid();
}

EditorViewportPreviewState GamePanelContentRenderer::PictureState(const EditorSceneContext& sceneContext) {
    const EditorPlayPreferences& play = sceneContext.EditorConfig().play;
    const EditorGameViewSize size = EditorGameView::Size(play.gameViewDevice, play.gameViewPortrait);
    EditorViewportPreviewState state;
    state.SetCustomResolution(size.width, size.height);
    state.SetProfile(EditorViewportProfileKind::Custom);
    state.SetFitMode(EditorViewportFitMode::Fit);
    return state;
}

GameViewPicture GamePanelContentRenderer::ResolvePicture(const RECT& renderArea, const EditorSceneContext& sceneContext) {
    const EditorPlayPreferences& play = sceneContext.EditorConfig().play;
    const EditorGameViewSize size = EditorGameView::Size(play.gameViewDevice, play.gameViewPortrait);
    return GameViewPicture{ .renderWidth = size.width, .renderHeight = size.height,
        .frame = EditorGameView::Fit(renderArea, play.gameViewDevice, play.gameViewPortrait) };
}

EditorSceneBgfxViewport::PresentSettings GamePanelContentRenderer::BuildSettings(
    const RECT& content, const DockPanel& panel, const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    const RECT screen = ResolvePicture(Resolve(content).renderArea, sceneContext).frame.screen;
    EditorSceneBgfxViewport::PresentSettings settings = ScenePanelContentRenderer::BuildGameSettings(
        content, screen, panel, PictureState(sceneContext), sceneContext, renderBackendSettings);
    const EditorPlayPreferences& play = sceneContext.EditorConfig().play;
    if (play.gameViewSafeArea) {
        // Under the messages, which are already in the list.
        std::vector<EditorSceneViewportOverlayShape> outlines = EditorGameView::UnsafeAreaShapes(
            play.gameViewDevice, play.gameViewPortrait, static_cast<std::uint32_t>(screen.right - screen.left),
            static_cast<std::uint32_t>(screen.bottom - screen.top));
        settings.viewportOverlayShapes.insert(settings.viewportOverlayShapes.begin(),
            std::make_move_iterator(outlines.begin()), std::make_move_iterator(outlines.end()));
    }
    return settings;
}

bool GamePanelContentRenderer::PresentViewport(
    EditorSceneBgfxViewport& sceneViewport,
    HWND sceneViewportHost,
    const RECT& content,
    const DockPanel& panel,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    if (sceneViewportHost == nullptr || !ShowsGame(sceneContext)) {
        return false;
    }
    const RECT screen = ResolvePicture(Resolve(content).renderArea, sceneContext).frame.screen;
    sceneViewport.Present(sceneViewportHost, screen, sceneContext.Scene(),
        BuildSettings(content, panel, sceneContext, renderBackendSettings));
    return true;
}

void GamePanelContentRenderer::Paint(
    HDC dc,
    const RECT& content,
    const DockPanel& panel,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings,
    EditorSceneBgfxViewport* sceneViewport,
    HWND sceneViewportHost) const {
    const GameViewRects rects = Resolve(content);
    const EditorPlayPreferences& play = sceneContext.EditorConfig().play;
    const GameViewPicture picture = ResolvePicture(rects.renderArea, sceneContext);
    const RECT& screen = picture.frame.screen;

    // The space around the device, then the device, then the bar with the choices.
    GdiDrawing::FillRectColor(dc, rects.renderArea, GdiDrawing::ToColorRef(theme.background));
    if (screen.right > screen.left) {
        DrawDevice(dc, picture.frame, play.gameViewDevice, theme);
    }
    GdiDrawing::FillRectColor(dc, rects.toolbar, SceneViewportToolbarDrawing::ToolbarRowColor(theme));
    for (std::size_t index = 0; index < rects.devices.size(); ++index) {
        const EditorGameViewDevice device = EditorGameView::kDevices[index];
        SceneViewportToolbarDrawing::DrawIconButton(dc, rects.devices[index], DeviceIcon(device), theme, device == play.gameViewDevice);
    }
    DrawDeviceOptionButton(dc, rects.rotateButton, HeroIconKind::Rotate, EditorGameView::CanRotate(play.gameViewDevice),
        play.gameViewPortrait, theme);
    DrawDeviceOptionButton(dc, rects.safeAreaButton, HeroIconKind::Scan, EditorGameView::HasSafeArea(play.gameViewDevice),
        play.gameViewSafeArea, theme);
    // The device's name, its screen's pixels and the scale, as the Scene View's bar reads its values.
    const std::string readout = EditorGameView::Label(play.gameViewDevice) + "  " + EditorGameView::FormatResolution(EditorGameViewSize{ picture.renderWidth, picture.renderHeight }) + "  " +
        EditorGameView::FormatScale(static_cast<std::uint32_t>(screen.right - screen.left), picture.renderWidth);
    GdiDrawing::DrawTabText(dc, rects.readout, readout, GdiDrawing::ToColorRef(theme.textSecondary));

    const bool showsGame = ShowsGame(sceneContext);
    if (screen.right > screen.left) {
        GdiDrawing::FillRectColor(dc, screen, kScreenColor);
    }
    if (!showsGame && screen.right > screen.left) {
        const std::string_view hint = sceneContext.HasPlayModeSceneSession()
            ? EditorText("gameView.noCamera", "No camera is rendering. Mark a camera in the scene as the primary camera.")
            : EditorText("gameView.pressPlay", "Press Play to see the game here.");
        GdiDrawing::DrawTabText(dc, screen, hint, GdiDrawing::ToColorRef(theme.textSecondary));
    }

    if (sceneViewport != nullptr && showsGame) {
        const EditorSceneBgfxViewport::PresentSettings settings = BuildSettings(content, panel, sceneContext, renderBackendSettings);
        if (sceneViewportHost != nullptr) {
            sceneViewport->Present(dc, sceneViewportHost, screen, sceneContext.Scene(), theme, settings);
        } else {
            sceneViewport->Present(dc, screen, sceneContext.Scene(), theme, settings);
        }
    }
}

} // namespace kb::editor

#endif
