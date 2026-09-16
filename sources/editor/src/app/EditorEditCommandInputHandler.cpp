#include "app/EditorEditCommandInputHandler.hpp"

#if defined(_WIN32)

#include "app/EditorEditCommandPolicy.hpp"
#include "app/EditorPendingTextEditCommitter.hpp"
#include "inspection/InspectorPanelInteraction.hpp"
#include "scene/EditorSceneContext.hpp"

#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool ControlDown() noexcept {
    return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

[[nodiscard]] bool AltDown() noexcept {
    return (GetKeyState(VK_MENU) & 0x8000) != 0;
}

[[nodiscard]] std::optional<EditorEditCommand> CommandForKey(WPARAM key) noexcept {
    switch (key) {
    case 'Z':
        return EditorEditCommand::Undo;
    case 'Y':
        return EditorEditCommand::Redo;
    case 'D':
        return EditorEditCommand::Duplicate;
    case 'S':
        return EditorEditCommand::Save;
    default:
        return std::nullopt;
    }
}
} // namespace

EditorEditCommandInputHandler::EditorEditCommandInputHandler(EditorSceneContext& sceneContext) noexcept
    : sceneContext_(sceneContext) {}

bool EditorEditCommandInputHandler::HandleKeyDown(WPARAM key) const {
    if (!ControlDown() || AltDown()) {
        return false;
    }

    const std::optional<EditorEditCommand> command = CommandForKey(key);
    if (!command.has_value()) {
        return false;
    }
    return ExecuteShortcut(*command);
}

bool EditorEditCommandInputHandler::ExecuteShortcut(EditorEditCommand command) const {
    // Ctrl+S never types into a field, so a value still being edited is committed - exactly as Enter would -
    // and then saved. Refusing instead made Save do nothing at all whenever an Inspector field was left open.
    if (command == EditorEditCommand::Save) {
        if (sceneContext_.Inspector().IsTextEditing()) {
            static_cast<void>(InspectorPanelInteraction::HandleKeyDown(nullptr, sceneContext_, VK_RETURN));
        }
        static_cast<void>(EditorPendingTextEditCommitter{ sceneContext_ }.CommitPendingEdits());
    }
    return EditorEditCommandPolicy::Execute(sceneContext_, command);
}

} // namespace kb::editor

#endif
