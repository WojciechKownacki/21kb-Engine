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
    RECT searchField{};    // Search box at the top of the sidebar.
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
    RECT lightingPathLabel{};
    RECT lightingPathForwardButton{};
    RECT lightingPathForwardPlusButton{};
    RECT lightingPathDeferredButton{};
    RECT postProcessLabel{};
    RECT postProcessCheckbox{};
    RECT antiAliasingLabel{};
    RECT antiAliasingNoneButton{};
    RECT antiAliasingFxaaButton{};
    RECT antiAliasingTaaButton{};
    RECT antiAliasingMsaaButton{};
    RECT bloomLabel{};
    RECT bloomCheckbox{};
    RECT shadowsLabel{};
    RECT shadowsCheckbox{};
    RECT selectionOutlineLabel{};
    RECT selectionOutlineCheckbox{};
    RECT gpuDrivenLabel{};
    RECT gpuDrivenCheckbox{};
    RECT msaaLabel{};
    RECT msaaOffButton{};
    RECT msaa2xButton{};
    RECT msaa4xButton{};
    RECT msaa8xButton{};
    RECT msaa16xButton{};
    // Lighting page: one row per quality setting (shadows and global illumination, desktop and mobile)
    // with one button per level (Auto, Low, Medium, High, Ultra).
    RECT qualityLabel[4]{};
    RECT qualityButton[4][5]{};
};

class ProjectSettingsPanelLayout {
public:
    ProjectSettingsPanelLayout() = delete;

    [[nodiscard]] static ProjectSettingsPanelLayoutRects Resolve(const RECT& content) noexcept;

    // The sidebar groups its categories under captions: Project from the first category,
    // Editor from kEditorGroupStart, Plugins from kPluginsGroupStart.
    static constexpr int kEditorGroupStart = 4;
    static constexpr int kPluginsGroupStart = 6;
    // A category row in the left sidebar (index 0 is the first category).
    [[nodiscard]] static RECT CategoryRow(const RECT& sidebar, int index) noexcept;
    // The caption above group 0 (Project), 1 (Editor) or 2 (Plugins).
    [[nodiscard]] static RECT GroupCaption(const RECT& sidebar, int group) noexcept;
    // The one-line description under a setting's label, across the page.
    [[nodiscard]] static RECT DescriptionRect(const ProjectSettingsPanelLayoutRects& rects, const RECT& label) noexcept;

    // The visible Mapping Context selector box, inset within its row cell.
    [[nodiscard]] static RECT MappingFieldBox(const ProjectSettingsPanelLayoutRects& rects) noexcept;

    // A row inside the open Mapping Context dropdown list, stacked under the box.
    [[nodiscard]] static RECT OptionRow(const RECT& fieldBox, int index) noexcept;

    // Bounding box enclosing `count` stacked option rows under the selector box.
    [[nodiscard]] static RECT OptionListBounds(const RECT& fieldBox, int count) noexcept;

    [[nodiscard]] static RECT BackendOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept;
    [[nodiscard]] static RECT LightingPathOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept;
    [[nodiscard]] static RECT GraphicsToggleCheckbox(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept;
    [[nodiscard]] static RECT GraphicsToggleLabel(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept;
    [[nodiscard]] static RECT AntiAliasingModeButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept;
    [[nodiscard]] static RECT MsaaOptionButton(const ProjectSettingsPanelLayoutRects& rects, int index) noexcept;
    [[nodiscard]] static RECT QualityOptionButton(const ProjectSettingsPanelLayoutRects& rects, int row, int level) noexcept;
};

#endif

} // namespace kb::editor
