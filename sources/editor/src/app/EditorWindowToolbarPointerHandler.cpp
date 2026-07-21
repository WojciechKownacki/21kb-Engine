#include "app/EditorWindowToolbarPointerHandler.hpp"

#include "app/EditorEditCommandPolicy.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "platform/win32/EditorSceneFileDialog.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/EditorToolbarRenderer.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] RECT ToRect(const DockRect& rect) noexcept {
    return RECT{
        .left = rect.x,
        .top = rect.y,
        .right = rect.x + rect.width,
        .bottom = rect.y + rect.height,
    };
}

void IncludeRect(RECT& target, const RECT& source) noexcept {
    RECT combined{};
    UnionRect(&combined, &target, &source);
    target = combined;
}

[[nodiscard]] RECT MenuInvalidationRect(const DockLayout& layout, EditorMenuCommand openMenu) noexcept {
    RECT rect = ToRect(layout.menu);
    const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(rect, openMenu);
    if (openMenu != EditorMenuCommand::None) {
        IncludeRect(rect, menu.dropdown);
    }
    return rect;
}

void InvalidateMainRect(HWND mainWindow, const RECT& rect) noexcept {
    if (mainWindow != nullptr) {
        InvalidateRect(mainWindow, &rect, FALSE);
    }
}

void InvalidateMenuChange(HWND mainWindow, const DockLayout& layout, EditorMenuCommand oldMenu, EditorMenuCommand newMenu) noexcept {
    RECT rect = MenuInvalidationRect(layout, oldMenu);
    IncludeRect(rect, MenuInvalidationRect(layout, newMenu));
    InvalidateMainRect(mainWindow, rect);
}

void InvalidateToolbar(HWND mainWindow, const DockLayout& layout) noexcept {
    InvalidateMainRect(mainWindow, ToRect(layout.toolbar));
}

[[nodiscard]] std::optional<DockLayout> ResolveMainLayout(HWND mainWindow, EditorDockModel& dockModel, const EditorMetrics& metrics) {
    RECT client{};
    if (GetClientRect(mainWindow, &client) == 0) {
        return std::nullopt;
    }

    return dockModel.Queries().BuildLayout(
        client.right - client.left,
        client.bottom - client.top,
        metrics.menuHeight,
        metrics.toolbarHeight,
        metrics.tabStripHeight,
        metrics.tabMinWidth,
        metrics.tabWidth,
        metrics.splitterSize,
        metrics.panelPadding);
}

void ActivateProjectSettingsPanel(HWND mainWindow, EditorDockModel& dockModel) {
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        if (panel.kind == DockPanelKind::ProjectSettings) {
            dockModel.Commands().ActivatePanel(panel.id);
            InvalidateRect(mainWindow, nullptr, FALSE);
            return;
        }
    }
}

void ActivatePluginsPanel(HWND mainWindow, EditorDockModel& dockModel) {
    for (const DockPanel& panel : dockModel.Queries().Panels()) {
        if (panel.kind == DockPanelKind::Plugins) {
            dockModel.Commands().ActivatePanel(panel.id);
            InvalidateRect(mainWindow, nullptr, FALSE);
            return;
        }
    }
}

[[nodiscard]] bool HandleMenuLeftButtonDown(
    HWND mainWindow,
    int x,
    int y,
    const DockLayout& layout,
    EditorDockModel& dockModel,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorShellInteractionState& shellInteraction) {
    const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(ToRect(layout.menu), shellInteraction.OpenMenu());
    if (const EditorMenuCommand hitMenu = EditorToolbarRenderer::HitTestMenu(menu, x, y); hitMenu != EditorMenuCommand::None) {
        const EditorMenuCommand oldMenu = shellInteraction.OpenMenu();
        static_cast<void>(shellInteraction.SetHoveredMenu(hitMenu));
        static_cast<void>(shellInteraction.SetOpenMenu(hitMenu));
        InvalidateMenuChange(mainWindow, layout, oldMenu, hitMenu);
        return true;
    }

    if (shellInteraction.OpenMenu() == EditorMenuCommand::None) {
        return false;
    }

    if (const std::optional<int> row = EditorToolbarRenderer::HitTestMenuRow(menu, x, y); row.has_value()) {
        if (shellInteraction.OpenMenu() == EditorMenuCommand::File) {
            if (*row == 0) {
                const std::optional<EditorDirtySceneResolution> resolution =
                    EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(mainWindow, sceneContext, L"creating a new scene");
                if (resolution.has_value() && sceneContext.NewScene(*resolution)) {
                    sceneViewport.RequestPresent();
                }
            } else if (*row == 1) {
                const std::optional<EditorDirtySceneResolution> resolution =
                    EditorSceneLifecycleGuard::ConfirmDirtySceneTransition(mainWindow, sceneContext, L"opening another scene");
                const std::optional<std::filesystem::path> path = resolution.has_value()
                    ? EditorSceneFileDialog::Open(mainWindow, EditorProjectPaths::ScenesRoot())
                    : std::nullopt;
                if (path.has_value() && sceneContext.OpenScene(*path, *resolution)) {
                    sceneViewport.RequestPresent();
                }
            } else if (*row == 2) {
                // Same guard as Ctrl+S: a graph gesture owns the working copy until it ends, and saving
                // through it would write a half-finished document and re-base the clean snapshot.
                if (EditorEditCommandPolicy::ExecuteFromPointer(sceneContext, EditorEditCommand::Save)) {
                    sceneViewport.RequestPresent();
                }
            } else if (*row == 3) {
                const std::optional<std::filesystem::path> path = EditorSceneFileDialog::SaveAs(mainWindow, sceneContext.CurrentScenePath(), EditorProjectPaths::ScenesRoot());
                if (path.has_value() && sceneContext.SaveCurrentSceneAs(*path)) {
                    sceneViewport.RequestPresent();
                }
            }
        } else if (shellInteraction.OpenMenu() == EditorMenuCommand::Edit) {
            // Through the policy's pointer route: these rows used to call the context directly and so did
            // not honour the graph-gesture guard. The text-edit guard deliberately does not apply to a
            // click - the commands commit the pending edit themselves.
            if (*row == 0) {
                if (EditorEditCommandPolicy::ExecuteFromPointer(sceneContext, EditorEditCommand::Undo)) {
                    sceneViewport.RequestPresent();
                }
            } else if (*row == 1) {
                if (EditorEditCommandPolicy::ExecuteFromPointer(sceneContext, EditorEditCommand::Redo)) {
                    sceneViewport.RequestPresent();
                }
            } else if (*row == 2) {
                if (EditorEditCommandPolicy::ExecuteFromPointer(sceneContext, EditorEditCommand::Duplicate)) {
                    sceneViewport.RequestPresent();
                }
            } else if (*row == 3) {
                ActivatePluginsPanel(mainWindow, dockModel);
            }
        } else if (shellInteraction.OpenMenu() == EditorMenuCommand::Options) {
            if (*row == 2) {
                ActivateProjectSettingsPanel(mainWindow, dockModel);
            }
        }
        const EditorMenuCommand oldMenu = shellInteraction.OpenMenu();
        shellInteraction.CloseMenu();
        InvalidateMenuChange(mainWindow, layout, oldMenu, EditorMenuCommand::None);
        InvalidateRect(mainWindow, nullptr, FALSE);
        return true;
    }

    const EditorMenuCommand oldMenu = shellInteraction.OpenMenu();
    shellInteraction.CloseMenu();
    InvalidateMenuChange(mainWindow, layout, oldMenu, EditorMenuCommand::None);
    return false;
}

struct TransportClickResult {
    bool handled = false;
    bool invalidates = false;
};

[[nodiscard]] bool TransportCommandEnabled(EditorTransportCommand command, const EditorPlayModeState& playMode) noexcept {
    return (command == EditorTransportCommand::Play && playMode.Mode() == EditorPlayMode::Stopped) ||
           (command == EditorTransportCommand::Pause && (playMode.IsPlaying() || playMode.IsPaused())) ||
           (command == EditorTransportCommand::Stop && (playMode.IsPlaying() || playMode.IsPaused()));
}

[[nodiscard]] TransportClickResult ExecuteTransportCommand(EditorTransportCommand command, EditorPlayModeState& playMode, EditorSceneContext& sceneContext) {
    switch (command) {
    case EditorTransportCommand::Play:
        if (playMode.Mode() != EditorPlayMode::Stopped) {
            return {.handled = true, .invalidates = true};
        }
        if (!sceneContext.BeginPlayModeSceneSession()) {
            return {.handled = true, .invalidates = false};
        }
        playMode.Play();
        return {.handled = true, .invalidates = true};
    case EditorTransportCommand::Stop:
        if (!TransportCommandEnabled(command, playMode)) {
            return {.handled = true, .invalidates = false};
        }
        playMode.Stop();
        static_cast<void>(sceneContext.RestorePlayModeSceneSession());
        return {.handled = true, .invalidates = true};
    case EditorTransportCommand::Pause:
        if (!TransportCommandEnabled(command, playMode)) {
            return {.handled = true, .invalidates = false};
        }
        if (playMode.IsPaused()) {
            playMode.Resume();
        } else {
            playMode.Pause();
        }
        return {.handled = true, .invalidates = true};
    case EditorTransportCommand::None:
        return {};
    }
    return {};
}

[[nodiscard]] bool HandleTransportLeftButtonDown(
    HWND mainWindow,
    int x,
    int y,
    const DockLayout& layout,
    EditorSceneContext& sceneContext,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction) {
    const EditorToolbarRects toolbar = EditorToolbarRenderer::ResolveToolbar(ToRect(layout.toolbar));
    const EditorTransportCommand transport = EditorToolbarRenderer::HitTestTransport(toolbar, x, y);
    if (transport == EditorTransportCommand::None) {
        return false;
    }

    if (TransportCommandEnabled(transport, playMode)) {
        static_cast<void>(shellInteraction.SetPressedTransport(transport));
    }

    const TransportClickResult result = ExecuteTransportCommand(transport, playMode, sceneContext);
    if (result.invalidates) {
        InvalidateToolbar(mainWindow, layout);
        // Transport commands write to the Console — snapshot capture/restore and,
        // on Stop, every behaviour's Destroyed hook. The play loop only repaints
        // panels WHILE playing, so once play ends nothing redraws the Console
        // until the next input. Invalidate the whole window so those final log
        // lines appear on the click itself, not on the next mouse move.
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
    return result.handled;
}

[[nodiscard]] bool HandleSaveLeftButtonDown(
    HWND mainWindow,
    int x,
    int y,
    const DockLayout& layout,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorShellInteractionState& shellInteraction) {
    const EditorToolbarRects toolbar = EditorToolbarRenderer::ResolveToolbar(ToRect(layout.toolbar));
    if (!EditorToolbarRenderer::HitTestSave(toolbar, x, y)) {
        return false;
    }

    static_cast<void>(shellInteraction.SetPressedSave(true));
    if (EditorEditCommandPolicy::ExecuteFromPointer(sceneContext, EditorEditCommand::Save)) {
        sceneViewport.RequestPresent();
    }
    InvalidateToolbar(mainWindow, layout);
    // Save writes a "Saved scene ..." line to the Console; like the transport
    // commands, invalidate the whole window so it shows immediately (same
    // one-shot-action-vs-panel-repaint issue).
    InvalidateRect(mainWindow, nullptr, FALSE);
    return true;
}

} // namespace

bool EditorWindowToolbarPointerHandler::HandleLeftButtonDown(
    HWND mainWindow,
    HWND messageWindow,
    int x,
    int y,
    EditorDockModel& dockModel,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorPlayModeState& playMode,
    EditorShellInteractionState& shellInteraction,
    const EditorMetrics& metrics) {
    if (messageWindow != mainWindow) {
        return false;
    }

    const std::optional<DockLayout> layout = ResolveMainLayout(mainWindow, dockModel, metrics);
    if (!layout.has_value()) {
        return false;
    }

    if (HandleMenuLeftButtonDown(mainWindow, x, y, *layout, dockModel, sceneContext, sceneViewport, shellInteraction)) {
        return true;
    }

    if (HandleSaveLeftButtonDown(mainWindow, x, y, *layout, sceneContext, sceneViewport, shellInteraction)) {
        return true;
    }

    return HandleTransportLeftButtonDown(mainWindow, x, y, *layout, sceneContext, playMode, shellInteraction);
}

bool EditorWindowToolbarPointerHandler::HandleMouseMove(
    HWND mainWindow,
    HWND messageWindow,
    int x,
    int y,
    EditorDockModel& dockModel,
    EditorShellInteractionState& shellInteraction,
    const EditorMetrics& metrics) {
    if (messageWindow != mainWindow) {
        ClearHover(shellInteraction);
        return false;
    }

    const std::optional<DockLayout> layout = ResolveMainLayout(mainWindow, dockModel, metrics);
    if (!layout.has_value()) {
        return false;
    }

    const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(ToRect(layout->menu), shellInteraction.OpenMenu());
    bool changed = false;
    changed = shellInteraction.SetHoveredMenu(EditorToolbarRenderer::HitTestMenu(menu, x, y)) || changed;
    changed = shellInteraction.SetHoveredMenuRow(EditorToolbarRenderer::HitTestMenuRow(menu, x, y)) || changed;
    const EditorToolbarRects toolbar = EditorToolbarRenderer::ResolveToolbar(ToRect(layout->toolbar));
    changed = shellInteraction.SetHoveredSave(EditorToolbarRenderer::HitTestSave(toolbar, x, y)) || changed;
    changed = shellInteraction.SetHoveredTransport(EditorToolbarRenderer::HitTestTransport(toolbar, x, y)) || changed;
    if (changed) {
        RECT rect = MenuInvalidationRect(*layout, shellInteraction.OpenMenu());
        IncludeRect(rect, ToRect(layout->toolbar));
        InvalidateMainRect(mainWindow, rect);
    }
    return changed;
}

void EditorWindowToolbarPointerHandler::ClearHover(EditorShellInteractionState& shellInteraction) noexcept {
    shellInteraction.ClearMenuHover();
    shellInteraction.ClearSaveHover();
    shellInteraction.ClearTransportHover();
}

} // namespace kb::editor

#endif
