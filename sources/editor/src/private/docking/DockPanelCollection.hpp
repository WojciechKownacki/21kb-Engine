#pragma once

#include "kb/editor/docking/DockTypes.hpp"

#include <cstdint>
#include <vector>

namespace kb::editor {

class DockPanelCollection {
public:
    DockPanelCollection() = default;
    explicit DockPanelCollection(std::vector<DockPanel> panels) noexcept;

    [[nodiscard]] const std::vector<DockPanel>& All() const noexcept;
    [[nodiscard]] std::vector<DockPanel> InArea(DockArea area) const;
    [[nodiscard]] const DockPanel* Find(std::uint32_t panelId) const noexcept;
    [[nodiscard]] DockPanel* Find(std::uint32_t panelId) noexcept;

    void Reset(std::vector<DockPanel> panels) noexcept;
    void MoveFloatingPanel(std::uint32_t panelId, int x, int y) noexcept;
    void ResizeFloatingPanel(std::uint32_t panelId, int width, int height) noexcept;

private:
    std::vector<DockPanel> panels_;
};

} // namespace kb::editor
