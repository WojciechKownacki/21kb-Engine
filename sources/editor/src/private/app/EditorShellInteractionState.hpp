#pragma once

#include <cstdint>
#include <optional>

namespace kb::editor {

enum class EditorMenuCommand : std::uint8_t {
    None,
    File,
    Edit,
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
    [[nodiscard]] bool SetHoveredTransport(EditorTransportCommand command) noexcept;
    [[nodiscard]] bool SetPressedTransport(EditorTransportCommand command) noexcept;

    void CloseMenu() noexcept;
    void ClearMenuHover() noexcept;
    void ClearTransportHover() noexcept;
    void ClearPressedTransport() noexcept;

    [[nodiscard]] EditorMenuCommand HoveredMenu() const noexcept;
    [[nodiscard]] EditorMenuCommand OpenMenu() const noexcept;
    [[nodiscard]] std::optional<int> HoveredMenuRow() const noexcept;
    [[nodiscard]] EditorTransportCommand HoveredTransport() const noexcept;
    [[nodiscard]] EditorTransportCommand PressedTransport() const noexcept;

private:
    EditorMenuCommand hoveredMenu_ = EditorMenuCommand::None;
    EditorMenuCommand openMenu_ = EditorMenuCommand::None;
    std::optional<int> hoveredMenuRow_{};
    EditorTransportCommand hoveredTransport_ = EditorTransportCommand::None;
    EditorTransportCommand pressedTransport_ = EditorTransportCommand::None;
};

} // namespace kb::editor
