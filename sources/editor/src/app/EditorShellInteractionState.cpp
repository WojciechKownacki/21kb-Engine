#include "app/EditorShellInteractionState.hpp"

namespace kb::editor {

bool EditorShellInteractionState::SetHoveredMenu(EditorMenuCommand menu) noexcept {
    if (hoveredMenu_ == menu) {
        return false;
    }
    hoveredMenu_ = menu;
    return true;
}

bool EditorShellInteractionState::SetOpenMenu(EditorMenuCommand menu) noexcept {
    const EditorMenuCommand next = openMenu_ == menu ? EditorMenuCommand::None : menu;
    if (openMenu_ == next) {
        return false;
    }
    openMenu_ = next;
    hoveredMenuRow_.reset();
    return true;
}

bool EditorShellInteractionState::SetHoveredMenuRow(std::optional<int> row) noexcept {
    if (hoveredMenuRow_ == row) {
        return false;
    }
    hoveredMenuRow_ = row;
    return true;
}

bool EditorShellInteractionState::SetHoveredSave(bool hovered) noexcept {
    if (hoveredSave_ == hovered) {
        return false;
    }
    hoveredSave_ = hovered;
    return true;
}

bool EditorShellInteractionState::SetPressedSave(bool pressed) noexcept {
    if (pressedSave_ == pressed) {
        return false;
    }
    pressedSave_ = pressed;
    return true;
}

bool EditorShellInteractionState::SetHoveredTransport(EditorTransportCommand command) noexcept {
    if (hoveredTransport_ == command) {
        return false;
    }
    hoveredTransport_ = command;
    return true;
}

bool EditorShellInteractionState::SetPressedTransport(EditorTransportCommand command) noexcept {
    if (pressedTransport_ == command) {
        return false;
    }
    pressedTransport_ = command;
    return true;
}

void EditorShellInteractionState::CloseMenu() noexcept {
    openMenu_ = EditorMenuCommand::None;
    hoveredMenuRow_.reset();
}

void EditorShellInteractionState::ClearMenuHover() noexcept {
    hoveredMenu_ = EditorMenuCommand::None;
    hoveredMenuRow_.reset();
}

void EditorShellInteractionState::ClearSaveHover() noexcept {
    hoveredSave_ = false;
}

void EditorShellInteractionState::ClearPressedSave() noexcept {
    pressedSave_ = false;
}

void EditorShellInteractionState::ClearTransportHover() noexcept {
    hoveredTransport_ = EditorTransportCommand::None;
}

void EditorShellInteractionState::ClearPressedTransport() noexcept {
    pressedTransport_ = EditorTransportCommand::None;
}

EditorMenuCommand EditorShellInteractionState::HoveredMenu() const noexcept {
    return hoveredMenu_;
}

EditorMenuCommand EditorShellInteractionState::OpenMenu() const noexcept {
    return openMenu_;
}

std::optional<int> EditorShellInteractionState::HoveredMenuRow() const noexcept {
    return hoveredMenuRow_;
}

bool EditorShellInteractionState::HoveredSave() const noexcept {
    return hoveredSave_;
}

bool EditorShellInteractionState::PressedSave() const noexcept {
    return pressedSave_;
}

EditorTransportCommand EditorShellInteractionState::HoveredTransport() const noexcept {
    return hoveredTransport_;
}

EditorTransportCommand EditorShellInteractionState::PressedTransport() const noexcept {
    return pressedTransport_;
}

} // namespace kb::editor
