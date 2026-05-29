#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

enum class DockArea : std::uint8_t {
    Left,
    Center,
    Right,
    Bottom,
    Floating,
};

struct DockPanel {
    std::string title;
    DockArea area = DockArea::Center;
    bool visible = true;
    bool detachable = true;
};

class EditorDockModel {
public:
    EditorDockModel();

    [[nodiscard]] const std::vector<DockPanel>& Panels() const noexcept;
    [[nodiscard]] std::vector<DockPanel> PanelsInArea(DockArea area) const;

private:
    std::vector<DockPanel> panels_;
};

} // namespace kb::editor
