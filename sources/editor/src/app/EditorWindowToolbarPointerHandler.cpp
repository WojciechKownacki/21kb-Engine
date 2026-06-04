#include "app/EditorWindowToolbarPointerHandler.hpp"

#if defined(_WIN32)
#include "app/EditorWindowInvalidator.hpp"
#include "rendering/EditorToolbarRenderer.hpp"

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

[[nodiscard]] bool HandleMenuLeftButtonDown(
    HWND mainWindow,
    HWND messageWindow,
    int x,
    int y,
    const DockLayout& layout,
    EditorShellInteractionState& shellInteraction) {
    const EditorMenuRects menu = EditorToolbarRenderer::ResolveMenu(ToRect(layout.menu), shellInteraction.OpenMenu());
    if (const EditorMenuCommand hitMenu = EditorToolbarRenderer::HitTestMenu(menu, x, y); hitMenu != EditorMenuCommand::None) {
        static_cast<void>(shellInteraction.SetHoveredMenu(hitMenu));
        static_cast<void>(shellInteraction.SetOpenMenu(hitMenu));
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, messageWindow);
        return true;
    }

    if (shellInteraction.OpenMenu() == EditorMenuCommand::None) {
        return false;
    }

    if (EditorToolbarRenderer::HitTestMenuRow(menu, x, y).has_value()) {
        shellInteraction.CloseMenu();
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, messageWindow);
        return true;
    }

    shellInteraction.CloseMenu();
    EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, messageWindow);
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

[[nodiscard]] TransportClickResult ExecuteTransportCommand(EditorTransportCommand command, EditorPlayModeState& playMode) noexcept {
    switch (command) {
    case EditorTransportCommand::Play:
        if (playMode.Mode() != EditorPlayMode::Stopped) {
            return {.handled = true, .invalidates = true};
        }
        playMode.Play();
        return {.handled = true, .invalidates = true};
    case EditorTransportCommand::Stop:
        if (!TransportCommandEnabled(command, playMode)) {
            return {.handled = true, .invalidates = false};
        }
        playMode.Stop();
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
    HWND messageWindow,
    int x,
    int y,
    const DockLayout& layout,
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

    const TransportClickResult result = ExecuteTransportCommand(transport, playMode);
    if (result.invalidates) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, messageWindow);
    }
    return result.handled;
}

} // namespace

bool EditorWindowToolbarPointerHandler::HandleLeftButtonDown(
    HWND mainWindow,
    HWND messageWindow,
    int x,
    int y,
    EditorDockModel& dockModel,
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

    if (HandleMenuLeftButtonDown(mainWindow, messageWindow, x, y, *layout, shellInteraction)) {
        return true;
    }

    return HandleTransportLeftButtonDown(mainWindow, messageWindow, x, y, *layout, playMode, shellInteraction);
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
    changed = shellInteraction.SetHoveredTransport(EditorToolbarRenderer::HitTestTransport(EditorToolbarRenderer::ResolveToolbar(ToRect(layout->toolbar)), x, y)) || changed;
    if (changed) {
        EditorWindowInvalidator::InvalidateMainAndSource(mainWindow, messageWindow);
    }
    return changed;
}

void EditorWindowToolbarPointerHandler::ClearHover(EditorShellInteractionState& shellInteraction) noexcept {
    shellInteraction.ClearMenuHover();
    shellInteraction.ClearTransportHover();
}

} // namespace kb::editor

#endif
