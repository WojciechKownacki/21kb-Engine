#include "rendering/ProjectSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/ProjectSettingsPanelLayout.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>

namespace kb::editor {
namespace {

constexpr int kPadding = 16;
constexpr int kCategoryCount = static_cast<int>(ProjectSettingsCategory::Count);
constexpr int kTooltipWidth = 312;
constexpr int kTooltipPaddingX = 12;
constexpr int kTooltipPaddingY = 10;

[[nodiscard]] const char* CategoryLabel(int index) noexcept {
    switch (static_cast<ProjectSettingsCategory>(index)) {
    case ProjectSettingsCategory::Inputs:
        return "Inputs";
    case ProjectSettingsCategory::Graphics:
        return "Rendering";
    case ProjectSettingsCategory::Physics:
        return "Physics";
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

void DrawTooltipText(HDC dc, RECT rect, const char* text, COLORREF color, int pointSize = 11, int weight = FW_NORMAL, UINT flags = DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS) {
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

void DrawOptionButton(HDC dc, const RECT& rect, const char* label, bool selected, bool enabled = true) {
    const COLORREF fill = selected ? RGB(46, 95, 138) : (enabled ? RGB(34, 37, 42) : RGB(29, 31, 35));
    const COLORREF border = selected ? RGB(79, 129, 184) : (enabled ? RGB(58, 61, 66) : RGB(43, 46, 51));
    const COLORREF text = selected ? RGB(232, 236, 240) : (enabled ? RGB(196, 205, 214) : RGB(104, 111, 121));
    GdiDrawing::DrawSharpFrame(dc, rect, fill, border);
    DrawText(dc, RECT{ rect.left + 8, rect.top, rect.right - 8, rect.bottom }, label, text, 12, selected ? FW_SEMIBOLD : FW_NORMAL, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawCheckbox(HDC dc, const RECT& rect, bool checked) {
    const COLORREF boxFill = checked ? RGB(46, 95, 138) : RGB(34, 37, 42);
    const COLORREF boxBorder = checked ? RGB(79, 129, 184) : RGB(58, 61, 66);
    GdiDrawing::DrawSharpFrame(dc, rect, boxFill, boxBorder);
    if (checked) {
        DrawText(dc, rect, "x", RGB(232, 236, 240), 12, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
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

struct TooltipContent {
    const char* title = "";
    const char* body = "";
};

[[nodiscard]] TooltipContent TooltipForKind(ProjectSettingsTooltipKind kind) noexcept {
    switch (kind) {
    case ProjectSettingsTooltipKind::MappingContext:
        return TooltipContent{
            "Mapping Context",
            "Selects the project-wide input mapping asset used by the runtime input system.",
        };
    case ProjectSettingsTooltipKind::PhysicsLayers:
        return TooltipContent{
            "Collision Layers Asset",
            "Selects the project-wide Physics Layers asset used to configure collision and query interactions at runtime.",
        };
    case ProjectSettingsTooltipKind::InputEnabled:
        return TooltipContent{
            "Input Enabled",
            "Enables or disables project input processing. Disable it for scenes that should ignore gameplay input.",
        };
    case ProjectSettingsTooltipKind::LightingPath:
        return TooltipContent{
            "Lighting Path",
            "Chooses Forward or Deferred scene lighting for opaque geometry. Transparent objects still render in the forward transparent pass.",
        };
    case ProjectSettingsTooltipKind::None:
    default:
        return {};
    }
}

void DrawTooltip(HDC dc, const RECT& content, const EditorProjectSettingsState& state) {
    const TooltipContent tooltip = TooltipForKind(state.TooltipKind());
    if (tooltip.title[0] == '\0') {
        return;
    }

    constexpr int titleHeight = 19;
    constexpr int bodyHeight = 48;
    constexpr int gap = 5;
    const int width = kTooltipWidth;
    const int height = (kTooltipPaddingY * 2) + titleHeight + gap + bodyHeight;
    int left = state.TooltipX() + 16;
    int top = state.TooltipY() + 18;
    left = std::clamp(left, static_cast<int>(content.left) + 8, std::max(static_cast<int>(content.left) + 8, static_cast<int>(content.right) - width - 8));
    if (top + height > content.bottom - 8) {
        top = state.TooltipY() - height - 12;
    }
    top = std::clamp(top, static_cast<int>(content.top) + 8, std::max(static_cast<int>(content.top) + 8, static_cast<int>(content.bottom) - height - 8));

    const RECT popup{ left, top, left + width, top + height };
    GdiDrawing::DrawSharpFrame(dc, popup, RGB(20, 23, 28), RGB(74, 88, 108));
    GdiDrawing::FillRectColor(dc, RECT{ popup.left + 1, popup.top + 1, popup.right - 1, popup.top + 3 }, RGB(72, 102, 132));

    RECT title{
        popup.left + kTooltipPaddingX,
        popup.top + kTooltipPaddingY,
        popup.right - kTooltipPaddingX,
        popup.top + kTooltipPaddingY + titleHeight,
    };
    DrawTooltipText(dc, title, tooltip.title, RGB(230, 236, 244), 12, FW_SEMIBOLD, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT body{
        title.left,
        title.bottom + gap,
        title.right,
        title.bottom + gap + bodyHeight,
    };
    DrawTooltipText(dc, body, tooltip.body, RGB(177, 187, 199));
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

void DrawGraphicsPage(
    HDC dc,
    const ProjectSettingsPanelLayoutRects& rects,
    const EditorSceneContext& sceneContext) {
    GdiDrawing::FillRectColor(dc, rects.sectionHeader, RGB(34, 37, 42));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, "RENDERING", RGB(150, 158, 168), 11, FW_SEMIBOLD);

    DrawText(dc, RECT{ rects.lightingPathLabel.left, rects.lightingPathLabel.top, rects.lightingPathLabel.right - 8, rects.lightingPathLabel.bottom }, "Lighting Path", RGB(196, 205, 214), 12);
    const kb::project::ProjectSceneLightingPath lightingPath = sceneContext.Project().sceneLightingPath;
    DrawOptionButton(dc, rects.lightingPathForwardButton, "Forward", lightingPath == kb::project::ProjectSceneLightingPath::Forward);
    DrawOptionButton(dc, rects.lightingPathForwardPlusButton, "Forward+", lightingPath == kb::project::ProjectSceneLightingPath::ForwardPlus);
    DrawOptionButton(dc, rects.lightingPathDeferredButton, "Deferred", lightingPath == kb::project::ProjectSceneLightingPath::Deferred);

}

void DrawPhysicsPage(HDC dc, const ProjectSettingsPanelLayoutRects& rects, const EditorSceneContext& sceneContext) {
    const kb::project::ProjectDescriptor& project = sceneContext.Project();
    const bool dropdownOpen = sceneContext.ProjectSettings().IsPhysicsLayersDropdownOpen();

    GdiDrawing::FillRectColor(dc, rects.sectionHeader, RGB(34, 37, 42));
    DrawText(dc, RECT{ rects.sectionHeader.left + 8, rects.sectionHeader.top, rects.sectionHeader.right - 8, rects.sectionHeader.bottom }, "PHYSICS", RGB(150, 158, 168), 11, FW_SEMIBOLD);
    DrawText(dc, RECT{ rects.mappingLabel.left, rects.mappingLabel.top, rects.mappingLabel.right - 8, rects.mappingLabel.bottom }, "Collision Layers Asset", RGB(196, 205, 214), 12);
    const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
    DrawSelectorBox(dc, fieldBox, MappingContextDisplayName(project.physicsLayersAsset), project.physicsLayersAsset.empty(), dropdownOpen);
    if (dropdownOpen) {
        DrawDropdownList(dc, fieldBox, sceneContext.ProjectPhysicsLayersAssetOptions(), project.physicsLayersAsset, sceneContext.ProjectSettings().HoveredOption());
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
    case ProjectSettingsCategory::Graphics:
        DrawGraphicsPage(dc, rects, sceneContext);
        break;
    case ProjectSettingsCategory::Physics:
        DrawPhysicsPage(dc, rects, sceneContext);
        break;
    case ProjectSettingsCategory::Count:
    default:
        break;
    }

    DrawTooltip(dc, content, sceneContext.ProjectSettings());
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

    const ProjectSettingsCategory category = static_cast<ProjectSettingsCategory>(sceneContext.ProjectSettings().SelectedCategory());
    if (category == ProjectSettingsCategory::Graphics) {
        for (int index = 0; index < 3; ++index) {
            const RECT button = ProjectSettingsPanelLayout::LightingPathOptionButton(rects, index);
            if (PointInRect(button, x, y)) {
                return Hit{ .kind = ProjectSettingsHitKind::LightingPathOption, .index = index, .rect = button };
            }
        }
        return Hit{};
    }

    if (category == ProjectSettingsCategory::Physics) {
        const RECT fieldBox = ProjectSettingsPanelLayout::MappingFieldBox(rects);
        if (sceneContext.ProjectSettings().IsPhysicsLayersDropdownOpen()) {
            const std::vector<std::string> options = sceneContext.ProjectPhysicsLayersAssetOptions();
            for (std::size_t index = 0; index < options.size(); ++index) {
                const RECT row = ProjectSettingsPanelLayout::OptionRow(fieldBox, static_cast<int>(index));
                if (PointInRect(row, x, y)) {
                    return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersOption, .index = static_cast<int>(index), .rect = row };
                }
            }
        }
        if (PointInRect(fieldBox, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersField, .index = -1, .rect = fieldBox };
        }
        return Hit{};
    }

    // Remaining right pane controls belong to the Inputs category.
    if (category != ProjectSettingsCategory::Inputs) {
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

ProjectSettingsPanelRenderer::Hit ProjectSettingsPanelRenderer::TooltipHitTest(const RECT& content, const EditorSceneContext& sceneContext, int x, int y) {
    const Hit direct = HitTest(content, sceneContext, x, y);
    if (direct.kind != ProjectSettingsHitKind::None && direct.kind != ProjectSettingsHitKind::CategoryItem && direct.kind != ProjectSettingsHitKind::MappingContextOption && direct.kind != ProjectSettingsHitKind::PhysicsLayersOption) {
        return direct;
    }

    const ProjectSettingsPanelLayoutRects rects = ProjectSettingsPanelLayout::Resolve(content);
    const ProjectSettingsCategory category = static_cast<ProjectSettingsCategory>(sceneContext.ProjectSettings().SelectedCategory());
    if (category == ProjectSettingsCategory::Inputs) {
        if (PointInRect(rects.mappingLabel, x, y) || PointInRect(rects.mappingField, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::MappingContextField, .index = -1, .rect = rects.mappingField };
        }
        if (PointInRect(rects.enabledLabel, x, y) || PointInRect(rects.enabledCheckbox, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::EnabledCheckbox, .index = -1, .rect = rects.enabledCheckbox };
        }
        return direct;
    }

    if (category == ProjectSettingsCategory::Graphics) {
        if (PointInRect(rects.lightingPathLabel, x, y)) {
            return Hit{ .kind = ProjectSettingsHitKind::LightingPathOption, .index = -1, .rect = rects.lightingPathLabel };
        }
    }

    if (category == ProjectSettingsCategory::Physics && (PointInRect(rects.mappingLabel, x, y) || PointInRect(rects.mappingField, x, y))) {
        return Hit{ .kind = ProjectSettingsHitKind::PhysicsLayersField, .index = -1, .rect = rects.mappingField };
    }

    return direct;
}

} // namespace kb::editor

#endif
