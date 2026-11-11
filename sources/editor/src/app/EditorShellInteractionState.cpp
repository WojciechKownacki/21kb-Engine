#include "app/EditorShellInteractionState.hpp"

#include "rendering/EditorToolbarLayout.hpp"

#include <utility>

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

void EditorShellInteractionState::SetLayoutMenu(EditorLayoutMenuModel menu) {
    layoutMenu_ = std::move(menu);
}

const EditorLayoutMenuModel& EditorShellInteractionState::LayoutMenu() const noexcept {
    return layoutMenu_;
}

int EditorShellInteractionState::MenuRowCount(EditorMenuCommand menu) const noexcept {
    if (menu == EditorMenuCommand::Layout) {
        return static_cast<int>(layoutMenu_.Rows().size());
    }
    if (menu == EditorMenuCommand::Scene) {
        return static_cast<int>(sceneMenuRows_.size()) + EditorToolbarLayout::FixedRowCount(menu);
    }
    if (menu == EditorMenuCommand::Publish) {
        return static_cast<int>(publishRows_.size()) + EditorToolbarLayout::FixedRowCount(menu);
    }
    if (menu == EditorMenuCommand::RecentProjects) {
        // "Back" above the projects.
        return static_cast<int>(recentProjectRows_.size()) + 1;
    }
    return EditorToolbarLayout::FixedRowCount(menu);
}

void EditorShellInteractionState::SetSceneMenu(std::vector<EditorPathMenuRow> scenes, bool documentActionsEnabled) {
    sceneMenuRows_ = std::move(scenes);
    sceneDocumentActionsEnabled_ = documentActionsEnabled;
}

void EditorShellInteractionState::SetRecentProjectsMenu(std::vector<EditorPathMenuRow> projects) {
    recentProjectRows_ = std::move(projects);
}

const std::vector<EditorPathMenuRow>& EditorShellInteractionState::SceneMenuRows() const noexcept {
    return sceneMenuRows_;
}

const std::vector<EditorPathMenuRow>& EditorShellInteractionState::RecentProjectRows() const noexcept {
    return recentProjectRows_;
}

bool EditorShellInteractionState::SceneDocumentActionsEnabled() const noexcept {
    return sceneDocumentActionsEnabled_;
}

void EditorShellInteractionState::SetRepositoryWindowOpen(bool open) noexcept {
    repositoryWindowOpen_ = open;
}

void EditorShellInteractionState::SetPlayMenu(EditorPlayPreferences preferences, bool canChange) noexcept {
    playMenuPreferences_ = preferences;
    playMenuCanChange_ = canChange;
}

const EditorPlayPreferences& EditorShellInteractionState::PlayMenuPreferences() const noexcept {
    return playMenuPreferences_;
}

bool EditorShellInteractionState::PlayMenuCanChange() const noexcept {
    return playMenuCanChange_;
}

void EditorShellInteractionState::SetPublishMenu(std::vector<EditorPathMenuRow> platforms) {
    publishRows_ = std::move(platforms);
}

const std::vector<EditorPathMenuRow>& EditorShellInteractionState::PublishRows() const noexcept {
    return publishRows_;
}

bool EditorShellInteractionState::RepositoryWindowOpen() const noexcept {
    return repositoryWindowOpen_;
}

bool EditorShellInteractionState::SetHoveredTopBarItem(EditorTopBarItem item) noexcept {
    if (hoveredTopBarItem_ == item) {
        return false;
    }
    hoveredTopBarItem_ = item;
    return true;
}

EditorTopBarItem EditorShellInteractionState::HoveredTopBarItem() const noexcept {
    return hoveredTopBarItem_;
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
