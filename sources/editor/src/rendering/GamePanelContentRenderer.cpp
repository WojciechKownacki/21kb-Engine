#include "rendering/GamePanelContentRenderer.hpp"

#if defined(_WIN32)
#include "localization/EditorLocalization.hpp"
#include "rendering/EditorSceneViewportGeometry.hpp"
#include "rendering/GdiDrawing.hpp"
#include "rendering/HeroIconKind.hpp"
#include "rendering/ScenePanelContentRenderer.hpp"
#include "rendering/scene_viewport_toolbar/SceneViewportToolbarDrawing.hpp"

#include <algorithm>
#include <string>

namespace kb::editor {
namespace {

constexpr int kPadding = 8;
constexpr int kButtonHeight = 24;
constexpr int kResolutionButtonWidth = 340;
constexpr int kScaleLabelWidth = 120;
constexpr int kListItemHeight = 24;
constexpr int kListWidth = 340;
constexpr COLORREF kLetterboxColor = RGB(10, 11, 13);

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

[[nodiscard]] EditorGameViewSize ProjectSize(const EditorSceneContext& sceneContext) noexcept {
    const kb::project::ProjectDisplaySettings& display = sceneContext.ProjectConfiguration().display;
    return EditorGameViewSize{ display.windowWidth, display.windowHeight };
}

} // namespace

GameViewRects GamePanelContentRenderer::Resolve(const RECT& content, bool listOpen) noexcept {
    GameViewRects rects{};
    rects.toolbar = content;
    rects.toolbar.bottom = std::min(content.bottom, content.top + ToolbarHeight);
    const int top = rects.toolbar.top + (ToolbarHeight - kButtonHeight) / 2;
    rects.resolutionButton = RECT{ content.left + kPadding, top,
        std::min(content.right - kPadding, content.left + kPadding + kResolutionButtonWidth), top + kButtonHeight };
    rects.scaleLabel = RECT{ std::max(rects.resolutionButton.right + kPadding, content.right - kPadding - kScaleLabelWidth), top,
        std::max(rects.resolutionButton.right + kPadding, content.right - kPadding), top + kButtonHeight };

    rects.renderArea = content;
    rects.renderArea.top = rects.toolbar.bottom;
    if (listOpen) {
        const int count = static_cast<int>(rects.items.size());
        rects.list = RECT{ content.left, rects.toolbar.bottom, std::min(content.right, content.left + kListWidth),
            std::min(content.bottom, rects.toolbar.bottom + count * kListItemHeight + kPadding) };
        for (int index = 0; index < count; ++index) {
            const int itemTop = rects.list.top + kPadding / 2 + index * kListItemHeight;
            rects.items[static_cast<std::size_t>(index)] = RECT{ rects.list.left + 4, itemTop, rects.list.right - 4, itemTop + kListItemHeight };
        }
        rects.renderArea.top = rects.list.bottom;
    }
    return rects;
}

bool GamePanelContentRenderer::ListOpen(const EditorSceneContext& sceneContext, std::uint64_t panelId) noexcept {
    return sceneContext.ViewportPreview(panelId).ToolbarDropdown() == EditorViewportToolbarDropdown::GameResolution;
}

std::optional<std::size_t> GamePanelContentRenderer::ItemAt(const GameViewRects& rects, int x, int y) noexcept {
    for (std::size_t index = 0; index < rects.items.size(); ++index) {
        if (PointInRect(rects.items[index], x, y)) {
            return index;
        }
    }
    return std::nullopt;
}

bool GamePanelContentRenderer::ShowsGame(const EditorSceneContext& sceneContext) noexcept {
    return sceneContext.HasPlayModeSceneSession() && sceneContext.PlayCameraEntity().IsValid();
}

EditorViewportPreviewState GamePanelContentRenderer::PictureState(const EditorSceneContext& sceneContext) {
    EditorViewportPreviewState state;
    const EditorGameViewSize size = EditorGameView::Size(sceneContext.EditorConfig().play.gameViewResolution, ProjectSize(sceneContext));
    if (!size.FollowsPanel()) {
        state.SetCustomResolution(size.width, size.height);
        state.SetProfile(EditorViewportProfileKind::Custom);
    }
    state.SetFitMode(EditorViewportFitMode::Fit);
    return state;
}

GameViewPicture GamePanelContentRenderer::ResolvePicture(const RECT& renderArea, const EditorSceneContext& sceneContext) {
    const EditorViewportPreviewState state = PictureState(sceneContext);
    GameViewPicture picture;
    picture.renderWidth = state.RenderWidthForPanel(EditorSceneViewportGeometry::RectWidth(renderArea));
    picture.renderHeight = state.RenderHeightForPanel(EditorSceneViewportGeometry::RectHeight(renderArea));
    picture.destination = EditorSceneViewportGeometry::CenteredRectFor(renderArea, picture.renderWidth, picture.renderHeight, state.FitMode());
    return picture;
}

EditorSceneBgfxViewport::PresentSettings GamePanelContentRenderer::BuildSettings(
    const RECT& content, const DockPanel& panel, const EditorSceneContext& sceneContext,
    const EditorRenderBackendSettings& renderBackendSettings) {
    const GameViewRects rects = Resolve(content, ListOpen(sceneContext, panel.id));
    return ScenePanelContentRenderer::BuildGameSettings(
        content, rects.renderArea, panel, PictureState(sceneContext), sceneContext, renderBackendSettings);
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
    const GameViewRects rects = Resolve(content, ListOpen(sceneContext, panel.id));
    sceneViewport.Present(sceneViewportHost, rects.renderArea, sceneContext.Scene(),
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
    const bool listOpen = ListOpen(sceneContext, panel.id);
    const GameViewRects rects = Resolve(content, listOpen);
    const EditorPlayPreferences& play = sceneContext.EditorConfig().play;
    const EditorGameViewSize project = ProjectSize(sceneContext);

    // The bars around the picture, then the bar with the resolution and the scale.
    GdiDrawing::FillRectColor(dc, rects.renderArea, kLetterboxColor);
    GdiDrawing::FillRectColor(dc, rects.toolbar, SceneViewportToolbarDrawing::ToolbarRowColor(theme));
    SceneViewportToolbarDrawing::DrawValueButton(
        dc, rects.resolutionButton, HeroIconKind::Gamepad2, EditorGameView::Label(play.gameViewResolution, project), theme, listOpen);

    const bool showsGame = ShowsGame(sceneContext);
    if (showsGame) {
        const GameViewPicture picture = ResolvePicture(rects.renderArea, sceneContext);
        const std::string scale = std::string{ EditorText("gameView.scale", "Scale") } + " " +
            EditorGameView::FormatScale(EditorSceneViewportGeometry::RectWidth(picture.destination), picture.renderWidth);
        GdiDrawing::DrawTabText(dc, rects.scaleLabel, scale, GdiDrawing::ToColorRef(theme.textSecondary));
    } else {
        const std::string_view hint = sceneContext.HasPlayModeSceneSession()
            ? EditorText("gameView.noCamera", "No camera is rendering. Mark a camera in the scene as the primary camera.")
            : EditorText("gameView.pressPlay", "Press Play to see the game here.");
        GdiDrawing::DrawTabText(dc, rects.renderArea, hint, GdiDrawing::ToColorRef(theme.textSecondary));
    }

    if (listOpen) {
        GdiDrawing::FillRectColor(dc, rects.list, SceneViewportToolbarDrawing::ToolbarRowColor(theme));
        for (std::size_t index = 0; index < rects.items.size(); ++index) {
            const EditorGameViewResolution resolution = EditorGameView::kResolutions[index];
            const bool selected = resolution == play.gameViewResolution;
            SceneViewportToolbarDrawing::FillRound(dc, rects.items[index],
                GdiDrawing::ToColorRef(selected ? theme.accent : theme.toolbarButton), GdiDrawing::ToColorRef(theme.borderPanel), 4);
            GdiDrawing::DrawTabText(dc, rects.items[index], EditorGameView::Label(resolution, project), GdiDrawing::ToColorRef(theme.textPrimary));
        }
    }

    if (sceneViewport != nullptr && showsGame) {
        const EditorSceneBgfxViewport::PresentSettings settings = BuildSettings(content, panel, sceneContext, renderBackendSettings);
        if (sceneViewportHost != nullptr) {
            sceneViewport->Present(dc, sceneViewportHost, rects.renderArea, sceneContext.Scene(), theme, settings);
        } else {
            sceneViewport->Present(dc, rects.renderArea, sceneContext.Scene(), theme, settings);
        }
    }
}

} // namespace kb::editor

#endif
