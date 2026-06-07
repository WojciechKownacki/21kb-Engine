#include "app/EditorEditCommandInputHandler.hpp"

#if defined(_WIN32)

#include "app/EditorEditCommandPolicy.hpp"

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
    return EditorEditCommandPolicy::Execute(sceneContext_, *command);
}

} // namespace kb::editor

#endif
