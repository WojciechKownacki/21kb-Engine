#pragma once

#include "settings/EditorLayoutMenuModel.hpp"

#include <cstdint>
#include <optional>

namespace kb::editor {

enum class EditorMenuCommand : std::uint8_t {
    None,
    File,
    Edit,
    Layout,
    Options,
    Help,
};

enum class EditorTransportCommand : std::uint8_t {
    None,
    Play,
    Stop,
    Pause,
};

class EditorShellInteractionState {
public:
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
    EditorMenuCommand hoveredMenu_ = EditorMenuCommand::None;
    EditorMenuCommand openMenu_ = EditorMenuCommand::None;
    std::optional<int> hoveredMenuRow_{};
    EditorLayoutMenuModel layoutMenu_{};
    bool hoveredSave_ = false;
    bool pressedSave_ = false;
    EditorTransportCommand hoveredTransport_ = EditorTransportCommand::None;
    EditorTransportCommand pressedTransport_ = EditorTransportCommand::None;
};

} // namespace kb::editor
