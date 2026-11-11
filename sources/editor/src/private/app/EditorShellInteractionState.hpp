#pragma once

#include "app/EditorPlayPreferences.hpp"
#include "settings/EditorLayoutMenuModel.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::editor {

enum class EditorMenuCommand : std::uint8_t {
    None,
    File,
    Edit,
    Layout,
    Options,
    Help,
    World,
    Project,
    Scene,
    // Project's recent list, shown in place of Project under the same button.
    RecentProjects,
    PlayOptions,
    Publish,
};

enum class EditorTransportCommand : std::uint8_t {
    None,
    Play,
    Stop,
    Pause,
};

// The top bar's controls other than the menus and the transport.
enum class EditorTopBarItem : std::uint8_t {
    None,
    Project,
    Scene,
    Branch,
    PlayOptions,
    Build,
    Publish,
    Minimize,
    Maximize,
    Close,
};

// A row that stands for a file: a scene of the project or a project on this computer.
// An empty path is a caption that cannot be chosen.
struct EditorPathMenuRow {
    std::string label;
    std::string detail;
    std::filesystem::path path;
    bool active = false;
};

class EditorShellInteractionState {
public:
    // The scenes above the Scene menu's actions, and whether those that change the open
    // scene's file can run; built when the menu opens.
    void SetSceneMenu(std::vector<EditorPathMenuRow> scenes, bool documentActionsEnabled);
    void SetRecentProjectsMenu(std::vector<EditorPathMenuRow> projects);
    [[nodiscard]] const std::vector<EditorPathMenuRow>& SceneMenuRows() const noexcept;
    [[nodiscard]] const std::vector<EditorPathMenuRow>& RecentProjectRows() const noexcept;
    [[nodiscard]] bool SceneDocumentActionsEnabled() const noexcept;
    // True while the repository window hangs from the branch label.
    void SetRepositoryWindowOpen(bool open) noexcept;
    [[nodiscard]] bool RepositoryWindowOpen() const noexcept;
    // The options under the arrow beside Play, and the platforms under Publish: read when the menu opens.
    void SetPlayMenu(EditorPlayPreferences preferences, bool canChange) noexcept;
    [[nodiscard]] const EditorPlayPreferences& PlayMenuPreferences() const noexcept;
    [[nodiscard]] bool PlayMenuCanChange() const noexcept;
    // A platform's `active` says it can be built as it is; its `detail` says why not otherwise.
    void SetPublishMenu(std::vector<EditorPathMenuRow> platforms);
    [[nodiscard]] const std::vector<EditorPathMenuRow>& PublishRows() const noexcept;
    [[nodiscard]] bool SetHoveredTopBarItem(EditorTopBarItem item) noexcept;
    [[nodiscard]] EditorTopBarItem HoveredTopBarItem() const noexcept;
    [[nodiscard]] bool SetHoveredMenu(EditorMenuCommand menu) noexcept;
    [[nodiscard]] bool SetOpenMenu(EditorMenuCommand menu) noexcept;
    [[nodiscard]] bool SetHoveredMenuRow(std::optional<int> row) noexcept;
    // The Layout menu is built from the layouts a project holds, so its rows arrive
    // with the menu instead of being fixed by the build.
    void SetLayoutMenu(EditorLayoutMenuModel menu);
    [[nodiscard]] bool SetHoveredSave(bool hovered) noexcept;
    [[nodiscard]] bool SetPressedSave(bool pressed) noexcept;
    [[nodiscard]] bool SetHoveredTransport(EditorTransportCommand command) noexcept;
    [[nodiscard]] bool SetPressedTransport(EditorTransportCommand command) noexcept;

    void CloseMenu() noexcept;
    void ClearMenuHover() noexcept;
    void ClearSaveHover() noexcept;
    void ClearPressedSave() noexcept;
    void ClearTransportHover() noexcept;
    void ClearPressedTransport() noexcept;

    [[nodiscard]] EditorMenuCommand HoveredMenu() const noexcept;
    [[nodiscard]] EditorMenuCommand OpenMenu() const noexcept;
    [[nodiscard]] std::optional<int> HoveredMenuRow() const noexcept;
    [[nodiscard]] const EditorLayoutMenuModel& LayoutMenu() const noexcept;
    // How many rows the given menu shows. Every menu but Layout has a fixed four.
    [[nodiscard]] int MenuRowCount(EditorMenuCommand menu) const noexcept;
    [[nodiscard]] bool HoveredSave() const noexcept;
    [[nodiscard]] bool PressedSave() const noexcept;
    [[nodiscard]] EditorTransportCommand HoveredTransport() const noexcept;
    [[nodiscard]] EditorTransportCommand PressedTransport() const noexcept;

private:
    EditorTopBarItem hoveredTopBarItem_ = EditorTopBarItem::None;
    EditorMenuCommand hoveredMenu_ = EditorMenuCommand::None;
    EditorMenuCommand openMenu_ = EditorMenuCommand::None;
    std::optional<int> hoveredMenuRow_{};
    EditorLayoutMenuModel layoutMenu_{};
    std::vector<EditorPathMenuRow> sceneMenuRows_{};
    std::vector<EditorPathMenuRow> recentProjectRows_{};
    bool sceneDocumentActionsEnabled_ = false;
    bool repositoryWindowOpen_ = false;
    EditorPlayPreferences playMenuPreferences_{};
    bool playMenuCanChange_ = true;
    std::vector<EditorPathMenuRow> publishRows_{};
    bool hoveredSave_ = false;
    bool pressedSave_ = false;
    EditorTransportCommand hoveredTransport_ = EditorTransportCommand::None;
    EditorTransportCommand pressedTransport_ = EditorTransportCommand::None;
};

} // namespace kb::editor
