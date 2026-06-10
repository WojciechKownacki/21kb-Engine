#include "rendering/ProjectSettingsPanelRenderer.hpp"

#if defined(_WIN32)
#include "rendering/GdiDrawing.hpp"
#include "rendering/gdi/ScopedFont.hpp"
#include "rendering/gdi/ScopedGdiObject.hpp"

#include <filesystem>
#include <string>

namespace kb::editor {
namespace {

constexpr int kPadding = 16;
constexpr int kHeaderHeight = 46;
constexpr int kSectionHeaderHeight = 26;
constexpr int kRowHeight = 34;
constexpr int kRowGap = 6;
constexpr int kLabelWidth = 150;
constexpr int kCheckboxSize = 18;

// Rects shared by Paint and HitTest so hover/click geometry never drifts.
struct ProjectSettingsLayout {
    RECT header{};
    RECT sectionHeader{};
    RECT mappingLabel{};
    RECT mappingField{};
    RECT enabledLabel{};
    RECT enabledCheckbox{};
};

[[nodiscard]] ProjectSettingsLayout ResolveLayout(const RECT& content) noexcept {
    ProjectSettingsLayout layout{};
    const int left = content.left + kPadding;
    const int right = content.right - kPadding;

    layout.header = RECT{ content.left, content.top, content.right, content.top + kHeaderHeight };

    const int sectionTop = layout.header.bottom + kPadding;
    layout.sectionHeader = RECT{ left, sectionTop, right, sectionTop + kSectionHeaderHeight };

    const int row1Top = layout.sectionHeader.bottom + kRowGap;
    layout.mappingLabel = RECT{ left, row1Top, left + kLabelWidth, row1Top + kRowHeight };
    layout.mappingField = RECT{ left + kLabelWidth, row1Top, right, row1Top + kRowHeight };

    const int row2Top = row1Top + kRowHeight + kRowGap;
    layout.enabledLabel = RECT{ left, row2Top, left + kLabelWidth, row2Top + kRowHeight };
    const int checkTop = row2Top + (kRowHeight - kCheckboxSize) / 2;
    layout.enabledCheckbox = RECT{ left + kLabelWidth, checkTop, left + kLabelWidth + kCheckboxSize, checkTop + kCheckboxSize };
    return layout;
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

// "/Game/Input/Player.21kbimc" -> "Player". Empty path -> "(None)".
[[nodiscard]] std::string MappingContextDisplayName(const std::string& virtualPath) {
    if (virtualPath.empty()) {
        return "(None)";
    }
    return std::filesystem::path{ virtualPath }.stem().string();
}

} // namespace

void ProjectSettingsPanelRenderer::Paint(
    HDC dc,
    const RECT& content,
    const EditorTheme& theme,
    const EditorSceneContext& sceneContext) const {
    static_cast<void>(theme);
    const ProjectSettingsLayout layout = ResolveLayout(content);
    const kb::project::ProjectDescriptor& project = sceneContext.Project();

    GdiDrawing::FillRectColor(dc, content, RGB(26, 28, 31));

    // Title bar.
    GdiDrawing::FillRectColor(dc, layout.header, RGB(32, 35, 39));
    GdiDrawing::FillRectColor(dc, RECT{ layout.header.left, layout.header.bottom - 1, layout.header.right, layout.header.bottom }, RGB(13, 14, 16));
    DrawText(dc, RECT{ layout.header.left + kPadding, layout.header.top, layout.header.right - kPadding, layout.header.bottom }, "Project Settings", RGB(226, 230, 235), 14, FW_SEMIBOLD);

    // Inputs section header.
    GdiDrawing::FillRectColor(dc, layout.sectionHeader, RGB(34, 37, 42));
    DrawText(dc, RECT{ layout.sectionHeader.left + 8, layout.sectionHeader.top, layout.sectionHeader.right - 8, layout.sectionHeader.bottom }, "INPUTS", RGB(150, 158, 168), 11, FW_SEMIBOLD);

    // Mapping context selector.
    DrawText(dc, RECT{ layout.mappingLabel.left, layout.mappingLabel.top, layout.mappingLabel.right - 8, layout.mappingLabel.bottom }, "Mapping Context", RGB(196, 205, 214), 12);
    const RECT field = GdiDrawing::Inset(layout.mappingField, 4);
    GdiDrawing::DrawSharpFrame(dc, field, RGB(34, 37, 42), RGB(58, 61, 66));
    const std::string display = MappingContextDisplayName(project.inputMappingContext);
    DrawText(dc, RECT{ field.left + 8, field.top, field.right - 22, field.bottom }, display.c_str(), project.inputMappingContext.empty() ? RGB(122, 130, 144) : RGB(210, 216, 222), 12);
    DrawText(dc, RECT{ field.right - 20, field.top, field.right - 4, field.bottom }, ">", RGB(138, 146, 156), 12, FW_SEMIBOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Enabled checkbox.
    DrawText(dc, RECT{ layout.enabledLabel.left, layout.enabledLabel.top, layout.enabledLabel.right - 8, layout.enabledLabel.bottom }, "Enabled", RGB(196, 205, 214), 12);
    const COLORREF boxFill = project.inputEnabled ? RGB(46, 95, 138) : RGB(34, 37, 42);
    const COLORREF boxBorder = project.inputEnabled ? RGB(79, 129, 184) : RGB(58, 61, 66);
    GdiDrawing::DrawSharpFrame(dc, layout.enabledCheckbox, boxFill, boxBorder);
    if (project.inputEnabled) {
        DrawText(dc, layout.enabledCheckbox, "x", RGB(232, 236, 240), 12, FW_BOLD, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

ProjectSettingsPanelRenderer::Hit ProjectSettingsPanelRenderer::HitTest(const RECT& content, int x, int y) noexcept {
    const ProjectSettingsLayout layout = ResolveLayout(content);
    if (PointInRect(layout.mappingField, x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::MappingContextField, .rect = layout.mappingField };
    }
    if (PointInRect(layout.enabledCheckbox, x, y) || PointInRect(layout.enabledLabel, x, y)) {
        return Hit{ .kind = ProjectSettingsHitKind::EnabledCheckbox, .rect = layout.enabledCheckbox };
    }
    return Hit{};
}

} // namespace kb::editor

#endif
