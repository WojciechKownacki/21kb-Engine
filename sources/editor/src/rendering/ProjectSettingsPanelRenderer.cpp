#include "rendering/ProjectSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectSettingsPanelLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kPadding = 16;
constexpr int kCategoryCount = static_cast<int>(ProjectSettingsCategory::Count);

[[nodiscard]] const char* CategoryLabel(int index) noexcept {
    switch (static_cast<ProjectSettingsCategory>(index)) {
    case ProjectSettingsCategory::Inputs:
        return "Inputs";
    case ProjectSettingsCategory::Count:
    default:
        return "";
    }
}

[[nodiscard]] bool PointInRect(const RECT& rect, int x, int y) noexcept {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

void DrawText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 12, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
    ScopedFont font{ pointSize, weight };
    const ScopedGdiObject selectedFont(dc, font.handle);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text, -1, &rect, static_cast<int>(flags | DT_NOPREFIX));
}

// "/Game/Input/Player.21kbinputcontext" -> "Player". Empty path -> "(None)".
[[nodiscard]] std::string MappingContextDisplayName(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        return "(None)";
    }
    return std::filesystem::path{ virtualPath }.stem().string();
}

void DrawCategorySidebar(HDC dc, const ProjectSettingsPanelLayoutRects& rects, int selectedCategory) {
    GdiDrawing::FillRectColor(dc, rects.sidebar, RGB(22, 24, 27));
    GdiDrawing::FillRectColor(dc, rects.divider, RGB(13, 14, 16));
    for (int index = 0; index < kCategoryCount; ++index) {
        const RECT row = ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, index);
        const bool selected = index == selectedCategory;
        if (selected) {
            GdiDrawing::FillRectColor(dc, row, RGB(39, 70, 104));
            GdiDrawing::FillRectColor(dc, RECT{ row.left, row.top, row.left + 2, row.bottom }, RGB(79, 129, 184));
        }
        DrawText(dc, RECT{ row.left + 12, row.top, row.right - 8, row.bottom }, CategoryLabel(index), selected ? RGB(226, 232, 238) : RGB(176, 184, 194), 12, selected ? FW_SEMIBOLD : FW_NORMAL);
    }
}

void DrawSelectorBox(HDC dc, const RECT& box, const std::string& display, bool isNone, bool open) {
    GdiDrawing::DrawSharpFrame(dc, box, RGB(34, 37, 42), open ? RGB(79, 129, 184) : RGB(58, 61, 66));
    DrawText(dc, RECT{ box.left + 8, box.top, box.right - 22, box.bottom }, display.c_str(), isNone ? RGB(122, 130, 144) : RGB(210, 216, 222), 12);
    DrawText(dc, RECT{ box.right - 20, box.top, box.right - 4, box.bottom }, open ? "^" : "v", RGB(150, 158, 168), 11, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawDropdownList(HDC dc, const RECT& fieldBox, const std::vector<std::string>& options, const std::string& selected, int hoveredOption) {
    if (options.empty()) {
        return;
    }
    GdiDrawing::DrawSharpFrame(dc, ProjectSettingsPanelLayout::OptionListBounds(fieldBox, static_cast<int>(options.size())), RGB(30, 32, 36), RGB(79, 129, 184));
    for (std::size_t index = 0; index < options.size(); ++index) {
        const RECT row = ProjectSettingsPanelLayout::OptionRow(fieldBox, static_cast<int>(index));
        const bool hovered = static_cast<int>(index) == hoveredOption;
        if (hovered) {
            GdiDrawing::FillRectColor(dc, GdiDrawing::Inset(row, 1), RGB(51, 90, 130)); // Hover sits brighter than selection.
        } else if (options[index] == selected) {
            GdiDrawing::FillRectColor(dc, GdiDrawing::Inset(row, 1), RGB(39, 70, 104));
        }
        const std::string label = MappingContextDisplayName(options[index]);
        DrawText(dc, RECT{ row.left + 8, row.top, row.right - 8, row.bottom }, label.c_str(), options[index].empty() ? RGB(150, 158, 168) : RGB(214, 220, 226), 12);
    }
}

void DrawInputsPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorSceneContext& sceneContext) {
    const kb::project::ProjectDescriptor& project = sceneContext.Project();
    const bool dropdownOpen = sceneContext.ProjectSettings().IsMappingContextDropdownOpen();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, RGB(34, 37, 42));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, "INPUTS", RGB(150, 158, 168), 11, FW_SEMIBOLD);

    // Enabled first so an open dropdown can overlap it.
    DrawText(dc, RECT{ rects.enabledLabel.left, rects.enabledLabel.top, rects.enabledLabel.right - 8, rects.enabledLabel.bottom }, "Enabled", RGB(196, 205, 214), 12);
    const COLORREF boxFill = project.inputEnabled ? RGB(46, 95, 138) : RGB(34, 37, 42);
    const COLORREF boxBorder = project.inputEnabled ? RGB(79, 129, 184) : RGB(58, 61, 66);
    GdiDrawing::DrawSharpFrame(dc, rects.enabledCheckbox, boxFill, boxBorder);
    if (project.inputEnabled) {
        DrawText(dc, rects.enabledCheckbox, "x", RGB(232, 236, 240), 12, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    DrawText(dc, RECT{ rects.mappingLabel.left, rects.mappingLabel.top, rects.mappingLabel.right - 8, rects.mappingLabel.bottom }, "Mapping Context", RGB(196, 205, 214), 12);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    DrawSelectorBox(dc, fieldBox, MappingContextDisplayName(project.inputMappingContext), project.inputMappingContext.empty(), dropdownOpen);
    if (dropdownOpen) {
        DrawDropdownList(dc, fieldBox, sceneContext.ProjectInputMappingContextOptions(), project.inputMappingContext, sceneContext.ProjectSettings().HoveredOption());
    }
}

} // namespace

void ProjectSettingsPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext) const {
    static_cast<void>(theme);
    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(content);
    const int selectedCategory = sceneContext.ProjectSettings().SelectedCategory();

    GdiDrawing::FillRectColor(dc, content, RGB(26, 28, 31));

    // Title bar (full width).
    GdiDrawing::FillRectColor(dc, rects.header, RGB(32, 35, 39));
    GdiDrawing::FillRectColor(dc, RECT{ rects.header.left, rects.header.bottom - 1, rects.header.right, rects.header.bottom }, RGB(13, 14, 16));
    DrawText(dc, RECT{ rects.header.left + kPadding, rects.header.top, rects.header.right - kPadding, rects.header.bottom }, "Project Settings", RGB(226, 230, 235), 14, FW_SEMIBOLD);

    DrawCategorySidebar(dc, rects, selectedCategory);

    switch (static_cast<ProjectSettingsCategory>(selectedCategory)) {
    case ProjectSettingsCategory::Inputs:
        DrawInputsPage(dc, rects, sceneContext);
        break;
    case ProjectSettingsCategory::Count:
    default:
        break;
    }
}

ProjectSettingsPanelRenderer::Hit ProjectSettingsPanelRenderer::HitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(content);

    // Left sidebar: category selection.
    for (int index = 0; index < kCategoryCount; ++index) {
        const RECT row = ProjectSettingsPanelLayout::CategoryRow(rects.sidebar, index);
        if (PointInRect(row, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::CategoryItem, .index = index, .rect = row };
        }
    }

    // Right pane controls only exist for the Inputs category.
    if (static_cast<ProjectSettingsCategory>(sceneContext.ProjectSettings().SelectedCategory()) != ProjectSettingsCategory::Inputs) {
        return Hit{};
    }

    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    if (sceneContext.ProjectSettings().IsMappingContextDropdownOpen()) {
        const std::vector<std::string> options = sceneContext.ProjectInputMappingContextOptions();
        for (std::size_t index = 0; index < options.size(); ++index) {
            const RECT row = ProjectSettingsPanelLayout::OptionRow(fieldBox, static_cast<int>(index));
            if (PointInRect(row, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::MappingContextOption, .index = static_cast<int>(index), .rect = row };
            }
        }
    }

    if (PointInRect(fieldBox, x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::MappingContextField, .index = -1, .rect = fieldBox };
    }
    if (PointInRect(rects.enabledCheckbox, x, y) || PointInRect(rects.enabledLabel, x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::EnabledCheckbox, .index = -1, .rect = rects.enabledCheckbox };
    }
    return Hit{};
}

} // namespace kb::editor

#endif
