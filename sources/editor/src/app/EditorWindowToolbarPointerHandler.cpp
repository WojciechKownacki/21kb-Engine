#include "app/EditorWindowToolbarPointerHandler.hpp"

#include "app/EditorEditCommandPolicy.hpp"

#if defined(_WIN32)
#include "app/EditorSceneLifecycleGuard.hpp"
#include "app/EditorWorkspaceSession.hpp"
#include "docking/EditorWorkspaceArrangement.hpp"
#include "docking/EditorFloatingWindowSync.hpp"
#include "platform/win32/EditorChoiceDialog.hpp"
#include "platform/win32/EditorSceneFileDialog.hpp"
#include "platform/win32/EditorTextEntryDialog.hpp"
#include "settings/EditorLayoutLibrary.hpp"
#include "project/EditorProjectPaths.hpp"
#include "rendering/SceneViewportPresentationPolicy.hpp"
#include "rendering/EditorToolbarRenderer.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

[[nodiscard]] RECT MenuInvalidationRect(
    const DockLayout& layout, EditorMenuCommand openMenu, int rowCount) noexcept {
    RECT rect = ToRect(layout.menu);
    const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(rect, openMenu, rowCount);
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

void InvalidateMenuChange(
    HWND mainWindow,
    const DockLayout& layout,
    const EditorShellInteractionState& shellInteraction,
    EditorMenuCommand oldMenu,
    EditorMenuCommand newMenu) noexcept {
    RECT rect = MenuInvalidationRect(layout, oldMenu, shellInteraction.MenuRowCount(oldMenu));
    IncludeRect(rect, MenuInvalidationRect(layout, newMenu, shellInteraction.MenuRowCount(newMenu)));
    InvalidateMainRect(mainWindow, rect);
}

// The Layout menu lists what the project actually holds, so it is built the moment
// the menu opens rather than carried between openings.
void RefreshLayoutMenu(
    EditorShellInteractionState& shellInteraction,
    const EditorSceneContext& sceneContext,
    bool deleting) {
    const std::vector<std::string> saved =
        EditorLayoutLibrary::List(EditorProjectPaths::ProjectRoot());
    EditorLayoutMenuModel model;
    if (deleting) {
        model.RebuildForDelete(saved);
    } else {
        model.Rebuild(saved, sceneContext.EditorConfig().layoutName);
    }
    shellInteraction.SetLayoutMenu(std::move(model));
}

// Switching arrangements replaces the whole workspace, so the torn-off windows are
// brought back in line with it before the new arrangement is recorded as current.
void AdoptWorkspace(
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    std::string layoutName) {
    EditorFloatingWindowSync::Reconcile(dockModel, floatingWindows);
    EditorWorkspaceSession::SaveAs(dockModel, sceneContext, std::move(layoutName));
}

void ApplySavedLayout(
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    const std::string& name) {
    const std::optional<EditorLayoutPreset> preset =
        EditorLayoutLibrary::Load(EditorProjectPaths::ProjectRoot(), name);
    if (!preset.has_value()) {
        sceneContext.Console().Error("Layout", "Layout " + name + " could not be read.");
        return;
    }
    if (!EditorWorkspaceArrangement::Apply(dockModel, *preset)) {
        sceneContext.Console().Warning("Layout",
            "Layout " + name + " does not fit this build and was only partly applied.");
    }
    AdoptWorkspace(dockModel, floatingWindows, sceneContext, name);
    sceneContext.Console().Info("Layout", "Layout " + name + " applied.");
}

void SaveCurrentLayout(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorSceneContext& sceneContext) {
    const std::optional<std::string> name = EditorTextEntryDialog::Show(mainWindow, {
        .title = "Save Layout",
        .label = "Layout name",
        .value = sceneContext.EditorConfig().layoutName,
        .hint = "Letters, digits, spaces, dashes and underscores.",
        .acceptLabel = "Save",
        .icon = HeroIconKind::RectangleGroup,
    });
    if (!name.has_value()) {
        return;
    }
    if (!EditorLayoutLibrary::IsValidName(*name)) {
        sceneContext.Console().Error("Layout",
            "A layout name may only use letters, digits, spaces, dashes and underscores.");
        return;
    }
    const bool overwriting =
        EditorLayoutLibrary::Load(EditorProjectPaths::ProjectRoot(), *name).has_value();
    if (overwriting && EditorChoiceDialog::Show(mainWindow, {
            .title = "Save Layout",
            .message = "Replace the layout " + *name + "?",
            .supportingText = "Its saved arrangement is overwritten with the one on screen.",
            .primaryLabel = "Replace",
            .cancelLabel = "Cancel",
            .icon = HeroIconKind::RectangleGroup,
        }) != EditorChoiceDialogResult::Primary) {
        return;
    }

    std::string error;
    if (!EditorLayoutLibrary::Save(
            EditorProjectPaths::ProjectRoot(), *name,
            EditorWorkspaceArrangement::Capture(dockModel), error)) {
        sceneContext.Console().Error("Layout",
            error.empty() ? "The layout could not be saved." : error);
        return;
    }
    EditorWorkspaceSession::SaveAs(dockModel, sceneContext, *name);
    sceneContext.Console().Info("Layout", "Layout " + *name + " saved.");
}

void DeleteSavedLayout(
    HWND mainWindow,
    EditorDockModel& dockModel,
    EditorSceneContext& sceneContext,
    const std::string& name) {
    if (EditorChoiceDialog::Show(mainWindow, {
            .title = "Delete Layout",
            .message = "Delete the layout " + name + "?",
            .supportingText = "The arrangement on screen stays as it is; only the saved layout goes.",
            .primaryLabel = "Delete",
            .cancelLabel = "Cancel",
            .icon = HeroIconKind::RectangleGroup,
            .primaryTone = EditorDialogButtonTone::Destructive,
        }) != EditorChoiceDialogResult::Primary) {
        return;
    }
    if (!EditorLayoutLibrary::Delete(EditorProjectPaths::ProjectRoot(), name)) {
        sceneContext.Console().Error("Layout", "Layout " + name + " could not be deleted.");
        return;
    }
    // The workspace does not change, but it is no longer that layout's.
    if (sceneContext.EditorConfig().layoutName == name) {
        EditorWorkspaceSession::SaveAs(dockModel, sceneContext, {});
    }
    sceneContext.Console().Info("Layout", "Layout " + name + " deleted.");
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
        metrics.splitterSize);
}

void ActivateRightPanel(HWND mainWindow, EditorDockModel& dockModel, DockPanelKind kind) {
    if (dockModel.Commands().ActivatePanelKind(kind, DockArea::Right)) {
        InvalidateRect(mainWindow, nullptr, FALSE);
    }
}

[[nodiscard]] bool HandleMenuLeftButtonDown(
    HWND mainWindow,
    int x,
    int y,
    const DockLayout& layout,
    EditorDockModel& dockModel,
    EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorRenderBackendSettings& renderBackendSettings,
    EditorShellInteractionState& shellInteraction) {
    const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(
        ToRect(layout.menu), shellInteraction.OpenMenu(),
        shellInteraction.MenuRowCount(shellInteraction.OpenMenu()));
    if (const EditorMenuCommand hitMenu = EditorToolbarRenderer::HitTestMenu(menu, x, y); hitMenu != EditorMenuCommand::None) {
        const EditorMenuCommand oldMenu = shellInteraction.OpenMenu();
        const RECT oldRect = MenuInvalidationRect(layout, oldMenu, shellInteraction.MenuRowCount(oldMenu));
        static_cast<void>(shellInteraction.SetHoveredMenu(hitMenu));
        static_cast<void>(shellInteraction.SetOpenMenu(hitMenu));
        if (shellInteraction.OpenMenu() == EditorMenuCommand::Layout) {
            RefreshLayoutMenu(shellInteraction, sceneContext, false);
        }
        RECT rect = oldRect;
        IncludeRect(rect, MenuInvalidationRect(
            layout, shellInteraction.OpenMenu(),
            shellInteraction.MenuRowCount(shellInteraction.OpenMenu())));
        InvalidateMainRect(mainWindow, rect);
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
                ActivateRightPanel(mainWindow, dockModel, DockPanelKind::Plugins);
            }
        } else if (shellInteraction.OpenMenu() == EditorMenuCommand::Layout) {
            const EditorLayoutMenuRow* layoutRow = shellInteraction.LayoutMenu().Row(*row);
            if (layoutRow == nullptr || !layoutRow->enabled) {
                // The caption of the delete list: it says what the list is, and clicking
                // it must not put the list away.
                return true;
            }
            switch (layoutRow->action) {
            case EditorLayoutMenuAction::Default:
                dockModel.Commands().ResetWorkspace();
                AdoptWorkspace(dockModel, floatingWindows, sceneContext, {});
                sceneContext.Console().Info("Layout", "Default layout applied.");
                break;
            case EditorLayoutMenuAction::Apply:
                ApplySavedLayout(dockModel, floatingWindows, sceneContext, layoutRow->layoutName);
                break;
            case EditorLayoutMenuAction::Save:
                SaveCurrentLayout(mainWindow, dockModel, sceneContext);
                break;
            case EditorLayoutMenuAction::Delete: {
                // Second step of the same menu: which one goes.
                const RECT before = MenuInvalidationRect(
                    layout, EditorMenuCommand::Layout,
                    shellInteraction.MenuRowCount(EditorMenuCommand::Layout));
                RefreshLayoutMenu(shellInteraction, sceneContext, true);
                static_cast<void>(shellInteraction.SetHoveredMenuRow(std::nullopt));
                RECT rect = before;
                IncludeRect(rect, MenuInvalidationRect(
                    layout, EditorMenuCommand::Layout,
                    shellInteraction.MenuRowCount(EditorMenuCommand::Layout)));
                InvalidateMainRect(mainWindow, rect);
                return true;
            }
            case EditorLayoutMenuAction::Remove:
                DeleteSavedLayout(mainWindow, dockModel, sceneContext, layoutRow->layoutName);
                break;
            }
            sceneViewport.RequestPresent();
        } else if (shellInteraction.OpenMenu() == EditorMenuCommand::Options) {
            if (*row == 0) {
                // Renderer: step to the next graphics backend. The viewport rebuilds on
                // the settings generation, so the change lands on the next present.
                renderBackendSettings.CycleBackend();
                sceneContext.Console().Info("Renderer",
                    std::string{"Render backend: "} +
                        EditorRenderBackendLabel(renderBackendSettings.Backend()) +
                        ". Reopen the editor to apply.");
                sceneViewport.RequestPresent();
            } else if (*row == 1) {
                ActivateRightPanel(mainWindow, dockModel, DockPanelKind::ProjectSettings);
            } else if (*row == 2) {
                ActivateRightPanel(mainWindow, dockModel, DockPanelKind::EditorSettings);
            }
        }
        const EditorMenuCommand oldMenu = shellInteraction.OpenMenu();
        shellInteraction.CloseMenu();
        InvalidateMenuChange(mainWindow, layout, shellInteraction, oldMenu, EditorMenuCommand::None);
        InvalidateRect(mainWindow, nullptr, FALSE);
        return true;
    }

    const EditorMenuCommand oldMenu = shellInteraction.OpenMenu();
    shellInteraction.CloseMenu();
    InvalidateMenuChange(mainWindow, layout, shellInteraction, oldMenu, EditorMenuCommand::None);
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
    EditorSceneBgfxViewport& sceneViewport,
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

    const bool previousPlayModeSceneActive = sceneContext.HasPlayModeSceneSession();
    const TransportClickResult result = ExecuteTransportCommand(transport, playMode, sceneContext);
    if (SceneViewportPresentationPolicy::RequiresPresent(
            previousPlayModeSceneActive,
            sceneContext.HasPlayModeSceneSession())) {
        sceneViewport.RequestPresent();
    }
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
    EditorFloatingWindowManager& floatingWindows,
    EditorSceneContext& sceneContext,
    EditorSceneBgfxViewport& sceneViewport,
    EditorRenderBackendSettings& renderBackendSettings,
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

    if (HandleMenuLeftButtonDown(mainWindow, x, y, *layout, dockModel, floatingWindows, sceneContext, sceneViewport, renderBackendSettings, shellInteraction)) {
        return true;
    }

    if (HandleSaveLeftButtonDown(mainWindow, x, y, *layout, sceneContext, sceneViewport, shellInteraction)) {
        return true;
    }

    return HandleTransportLeftButtonDown(mainWindow, x, y, *layout, sceneContext, sceneViewport, playMode, shellInteraction);
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

    const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(
        ToRect(layout->menu), shellInteraction.OpenMenu(),
        shellInteraction.MenuRowCount(shellInteraction.OpenMenu()));
    bool changed = false;
    changed = shellInteraction.SetHoveredMenu(EditorToolbarRenderer::HitTestMenu(menu, x, y)) || changed;
    changed = shellInteraction.SetHoveredMenuRow(EditorToolbarRenderer::HitTestMenuRow(menu, x, y)) || changed;
    const EditorToolbarRects toolbar = EditorToolbarRenderer::ResolveToolbar(ToRect(layout->toolbar));
    changed = shellInteraction.SetHoveredSave(EditorToolbarRenderer::HitTestSave(toolbar, x, y)) || changed;
    changed = shellInteraction.SetHoveredTransport(EditorToolbarRenderer::HitTestTransport(toolbar, x, y)) || changed;
    if (changed) {
        RECT rect = MenuInvalidationRect(
            *layout, shellInteraction.OpenMenu(),
            shellInteraction.MenuRowCount(shellInteraction.OpenMenu()));
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
