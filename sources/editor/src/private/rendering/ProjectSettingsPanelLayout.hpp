#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace kb::editor {

#if defined(_WIN32)

// Pure geometry of the Project Settings panel (master-detail: category sidebar
// on the left, selected category's content on the right). Kept separate from the
// renderer so the layout can be shared by painting, hit-testing, and tests
// without touching any GDI device.
struct ProjectSettingsPanelLayoutRects {
    RECT header{};         // Title bar (full width).
    RECT sidebar{};        // Left category column.
    RECT divider{};        // 1px line between sidebar and content.
    RECT content{};        // Right content pane.
    RECT sectionHeader{};  // Inputs page: section title.
    RECT mappingLabel{};
    RECT mappingField{};   // Mapping Context row cell (selector lives inset within).
    RECT enabledLabel{};
    RECT enabledCheckbox{};
    RECT backendLabel{};
    RECT backendAutoButton{};
    RECT backendDx12Button{};
    RECT backendVulkanButton{};
};

class ProjectSettingsPanelLayout {
public:
    ProjectSettingsPanelLayout() = delete;

    [[nodiscard]] static ProjectSettingsPanelLayoutRects Resolve(const RECT& content) noexcept;

    // A category row in the left sidebar (index 0 is the first category).
    [[nodiscard]] static RECT CategoryRow(const RECT& sidebar, int index) noexcept;

    // The visible Mapping Context selector box, inset within its row cell.
    [[nodiscard]] static RECT MappingFieldBox(const ProjectSettingsPanelLayoutRects& rects) noexcept;

    // A row inside the open Mapping Context dropdown list, stacked under the box.
    [[nodiscard]] static RECT OptionRow(const RECT& fieldBox, int index) noexcept;

    // Bounding box enclosing `count` stacked option rows under the selector box.
    [[nodiscard]] static RECT OptionListBounds(const RECT& fieldBox, int count) noexcept;

    [[nodiscard]] static RECT BackendOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept;
};

#endif

} // namespace kb::editor
